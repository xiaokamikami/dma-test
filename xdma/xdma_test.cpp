#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <memory>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <functional>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <shared_mutex>
#include <cassert>
#include <fstream>

#include "dma_mmpool.cpp"
#include "diffstate.h"

#define DEVICE_C2H_NAME "/dev/xdma0_c2h_"
#define DEVICE_H2C_NAME "/dev/xdma0_h2c_"
//#define TEST_NUM (16000000ll / (BLOCK_SIZE / 4096))
#define MAX_H2C_SIZE (128 / DMA_CHANNS)

#define TEST_NUM  8192000
//#define LOOP_BACK

#define DMA_QUEUE_SIZE 32
#define DMA_QUEUE_MAX_SIZE 255

typedef struct {
    uint8_t pack_indx;
    char data[4095];
} DmaPackge;


MemoryPool memory_pool;
MemoryIdxPool memory_idx_pool;

std::mutex test_mtx;
std::condition_variable test_cv;
std::atomic <uint64_t> stream_receiver_cout{0};
unsigned char *dma_mem = NULL;
char workload_path[256] = {};
bool use_workload = false;

class StreamReceiver {
public:
    // 构造函数：初始化StreamReceiver对象
    StreamReceiver()
        : running(false) {
#ifdef HAVE_FPGA
    for(int i = 0; i < DMA_CHANNS;i ++) {
        char c2h_device[64];
        sprintf(c2h_device,"%s%d",DEVICE_C2H_NAME,i);
        xdma_c2h_fd[i] = open(c2h_device, O_RDONLY );
        if (xdma_c2h_fd[i] == -1) {
            std::cout << c2h_device << std::endl;
            perror("Failed to open XDMA device");
            exit(-1);
        }
        std::cout << "XDMA link " << c2h_device << std::endl;
    }
    char h2c_device[64];
    sprintf(h2c_device,"%s%d",DEVICE_H2C_NAME,0);
    xdma_h2c_fd = open(h2c_device, O_WRONLY);
    if (xdma_h2c_fd == -1) {
        std::cout << h2c_device << std::endl;
        perror("Failed to open XDMA device");
        exit(-1);
    }
    std::cout << "XDMA link " << h2c_device << std::endl;
#endif
    }

    // 析构函数：确保资源被正确释放
    ~StreamReceiver() {
        stop();
    }

    // 启动流接收和处理
    void start() {
        if (running.exchange(true)) return; // 如果已经运行，直接返回
        printf("DMA packge Size %ld \n", sizeof(DmaPackge));
#ifdef HAVE_FPGA
        // 下载workload
        if (use_workload) {
            device_write("/dev/xdma0_user", 0x100000, 1, false, nullptr);
            device_write("/dev/xdma0_user", 0x10000, 0X8, false, nullptr);
            device_write("/dev/xdma0_bypass", 0x0, 0, true, workload_path);
            device_write("/dev/xdma0_user", 0x100000, 0, false, nullptr);
        }
#endif

        for(int i = 0; i < DMA_CHANNS;i ++) {
            printf("start c2h channel %d \n", i);
            receive_thread[i] = std::thread(&StreamReceiver::receiveStream, this, i);
        }
        process_thread = std::thread(&StreamReceiver::processData, this);
        printf("start h2c\n");
    }

    // 停止流接收和处理
    void stop() {
        if (!running.exchange(false)) return; // 如果已经停止，直接返回

        // 通知线程停止
        for(int i = 0; i < DMA_CHANNS;i ++) {
            if (receive_thread[i].joinable()) receive_thread[i].join();
            printf("receive %d STOP \n",i);
#ifdef HAVE_FPGA
            close(xdma_c2h_fd[i]);
#endif
        }
#ifdef HAVE_FPGA
        close(xdma_h2c_fd);
#endif
        if (process_thread.joinable()) process_thread.join();
        printf("process STOP \n");
        memory_idx_pool.cleanupMemoryPool();
        printf("MEM STOP \n");
    }

private:
    std::atomic<bool> running;   // 运行标志

    std::thread receive_thread[DMA_CHANNS];  // 接收线程
    std::thread process_thread;  // 处理线程

    int xdma_c2h_fd[DMA_CHANNS];             // XDMA文件描述符
    int xdma_h2c_fd;

    uint64_t send_flow_cout[DMA_CHANNS] = {0};
    // 生成测速数据包
    uint8_t encapsulation_packge() {
        static std::atomic<uint8_t> pack_indx{0};
        return pack_indx.fetch_add(1);
    }

    void send_flow_control(int channel) {
        bool flow_control = true;
        uint64_t send_flow_cout_i = send_flow_cout[channel];
        do{
            for (int j = 0; j < DMA_CHANNS; j++) {
                if (j == channel) continue; // 跳过当前通道
                uint64_t diff = send_flow_cout_i - send_flow_cout[j];
                if (diff < DMA_CHANNS) {
                    flow_control = false;
                } else {
                    flow_control = true;
                }
            }
        } while (flow_control == true);//发送流量控制
    }

    // 接收流数据
    void receiveStream(int channel) {
    #ifdef HAVE_FPGA
        char *rdata = (char *)malloc(4096);
    #else
        DmaPackge send_packg;
        memset(send_packg.data, 0, sizeof(send_packg.data));
    #endif
        while (running) {
    #ifdef HAVE_FPGA
            uint64_t lseek_size = channel * sizeof(DmaPackge);
            uint64_t addr = lseek(xdma_c2h_fd[channel], lseek_size, SEEK_SET);
            size_t size = read(xdma_c2h_fd[channel], rdata, sizeof(DmaPackge));

            // 还原数据包
            DmaPackge* packet = reinterpret_cast<DmaPackge*>(rdata); // rdata现在是指向包含DmaPackge的内存的指针  
            uint8_t idx = packet->pack_indx;
            printf("[receiveStream] get dma_ch-%d idx %d\n", channel, idx);
            if (memory_idx_pool.write_free_chunk(idx, rdata) == false) {
                stream_receiver_cout == TEST_NUM;
                printf("It should not be the case that no available block can be found\n");
                stop();
                assert(0);
            }
    #else // NO FPGA
            send_flow_control(channel);

            send_packg.pack_indx = encapsulation_packge();
            //printf("[receive] get dma_ch-%d idx %d\n", channel, send_packg.pack_indx);
            //写入伪数据包
            if (memory_idx_pool.write_free_chunk(send_packg.pack_indx, (char *)&send_packg) == false) {
                stream_receiver_cout = TEST_NUM;
                printf("It should not be the case that no available block can be found\n");
                test_cv.notify_all();
                assert(0);
            }
            send_flow_cout[channel] ++;
    #endif
         if (stream_receiver_cout >= TEST_NUM)
            return;
        }
    }

    // 处理接收到的数据
    void processData() {
        static size_t recv_count = 256;
        DmaPackge test_packge;
        #ifdef HAVE_FPGA
            memory_idx_pool.wait_mempool_start();
        #endif
        while (running) {
            if (recv_count == 256) {
                #ifdef HAVE_FPGA
                    if(memory_idx_pool.check_group() == false)
                        continue;
                #else
                    while(memory_idx_pool.check_group() == false) {}
                #endif
                recv_count = 0;
            }
            if (memory_idx_pool.read_busy_chunk((char *)&test_packge) == false) {
                printf("[processData]read_busy_chunk\n");
                stop();
                assert(0);
            }
            if (test_packge.pack_indx != recv_count) {
                printf("[processData]difftest idx check faile packge=%d, need=%ld\n", test_packge.pack_indx, recv_count);
                stop();
                assert(0);
            }
            stream_receiver_cout ++;
            recv_count ++;

            // if (stream_receiver_cout % 1000 == 0 && stream_receiver_cout > 0)
            //     printf("stream_receiver_cout %d \n",stream_receiver_cout.load());
            if (stream_receiver_cout >= TEST_NUM) {
                printf("Reaching the statistical upper limit\n");
                std::lock_guard<std::mutex> lock(test_mtx);
                test_cv.notify_all();
            }
        }
    }

    void sim_difftest() {
        while(running) {

        }
    }

    int device_write(const char *dev_name, uint64_t addr, uint64_t value, bool is_workload, const char *workload) {
        size_t size = !is_workload ? 0x1000 : 0x10000;
        uint64_t aligned_size = (size + 0xffful) & ~0xffful;
        uint64_t base = addr & ~0xffful;
        uint32_t offset = addr & 0xfffu;
        std::ifstream workload_fd;
        size_t pg_size = sysconf(_SC_PAGE_SIZE);

        if (base % pg_size != 0) {
            printf("base must be a multiple of system page size");
            return -1;
        }

        int m_fd = open(dev_name, O_RDWR | O_SYNC);
        if (m_fd < 0) {
            printf("failed to open %s\n", dev_name);
            return -1;
        }
        if (is_workload) {
            workload_fd.open(workload);
            if (!workload_fd.is_open()) {
                printf("failed to open %s\n", dev_name);
                return -1;
            }
        }

        void *m_ptr = mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, base);
        if (m_ptr == MAP_FAILED) {
            close(m_fd);
            printf("failed to mmap %s", dev_name);
        }

        if (workload) {
            workload_fd.read(((char *)m_ptr) + offset, size);
            size_t bytes_read = workload_fd.gcount();
            if (bytes_read < size)
                memset(static_cast<char*>(m_ptr) + offset + bytes_read, 0, size - bytes_read);
        } else {
            ((volatile uint32_t *)m_ptr)[offset >> 2] = value;
        }

        munmap(m_ptr, aligned_size);
        close(m_fd);

        return 0;
    }

};

int main(int argc, const char *argv[]) {
    struct timespec ts_start, ts_end;
    size_t path_len = strlen(argv[0]);
    if (path_len > 256)
        printf("Handle error: path is too long");
    else if(path_len > 0) {
        memcpy(workload_path, argv[0], strlen(argv[0]));
        use_workload = true;
    }

    
    try {
        StreamReceiver receiver;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        receiver.start();

        // 等待一段时间或直到满足停止条件
        {
            std::unique_lock<std::mutex> lock(test_mtx);
            test_cv.wait(lock, [&]() {return stream_receiver_cout >= TEST_NUM;});
            clock_gettime(CLOCK_MONOTONIC, &ts_end);
        }
        receiver.stop();
		// subtract the start time from the end time
		double time_consum = (ts_end.tv_sec - ts_start.tv_sec) + 
                     ((ts_end.tv_nsec - ts_start.tv_nsec) / (double)1000000000);
        std::cout << "Program time consumption: " << time_consum << " S"
                  << " Transfer block: " << stream_receiver_cout << " Size per receive: " << BLOCK_SIZE
                  << std::endl;
        // Pref
        uint64_t receive_bytes = BLOCK_SIZE * stream_receiver_cout;
        uint64_t rate_bytes = receive_bytes / time_consum;
#ifdef HAVE_FPGA
        std::cout << "XDMA Stream rate = "; 
#else
        std::cout << "The DMA HAVE_CHANNS:" << DMA_CHANNS << ", C2H theoretical rate of a thread = "; 
#endif
        std::cout << rate_bytes / 1024 / 1024 << "MB/s" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
