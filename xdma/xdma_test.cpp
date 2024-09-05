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

#include "dma_mmpool.cpp"
#include "diffstate.h"

#define DEVICE_C2H_NAME "/dev/xdma0_c2h_"
#define DEVICE_H2C_NAME "/dev/xdma0_h2c_"
#define TEST_NUM (16000000ll / (BLOCK_SIZE / 4096))
//#define TEST_NUM  255
#define LOOP_BACK

#define DMA_QUEUE_SIZE 32
#define DMA_QUEUE_MAX_SIZE 255

typedef struct {
    uint8_t pack_indx;
    DiffTestState diffteststate;
} DmaPackge;
DmaPackge test_packge;

MemoryPool memory_pool;
MemoryIdxPool memory_idx_pool;

std::mutex test_mtx;
std::condition_variable test_cv;
std::atomic <uint64_t> stream_receiver_cout = 0;
unsigned char *dma_mem = NULL;

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
        printf("DiffTestState Size %d \n",sizeof(DiffTestState));
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
        memory_pool.stopMemoryPool();
        if (process_thread.joinable()) process_thread.join();
        for(int i = 0; i < DMA_CHANNS;i ++) {
            if (receive_thread[i].joinable()) receive_thread[i].join();
#ifdef HAVE_FPGA
            close(xdma_c2h_fd[i]);
#endif
        }
#ifdef HAVE_FPGA
        close(xdma_h2c_fd);
#endif
    }

private:
    std::atomic<bool> running;   // 运行标志

    std::thread receive_thread[DMA_CHANNS];  // 接收线程
    std::thread process_thread;  // 处理线程

    int xdma_c2h_fd[DMA_CHANNS];             // XDMA文件描述符
    int xdma_h2c_fd;

    // 生成测速数据包
    void encapsulation_packge(DmaPackge *send_packg) {
        static std::atomic<uint8_t> pack_indx = 0;
        send_packg->pack_indx = pack_indx;
        pack_indx ++;
    }

    // 接收流数据
    void receiveStream(int channel) {
        #ifdef HAVE_FPGA
            size_t w_bytes = write(xdma_h2c_fd, "1", 1);
        #endif
        char *rdata = (char *)malloc(BLOCK_SIZE);
        while (running) {
    #ifdef HAVE_FPGA
            //uint64_t lseek_size = channel * sizeof(DmaPackge);
            //lseek(xdma_c2h_fd[channel], lseek_size, SEEK_SET);
            size_t size = read(xdma_c2h_fd[channel], rdata, sizeof(DmaPackge));
        #ifdef LOOP_BACK
            // 还原数据包
            DmaPackge* packet = reinterpret_cast<DmaPackge*>(rdata); // rdata现在是指向包含DmaPackge的内存的指针  
            uint8_t idx = packet->pack_indx;
            //printf("get dma packge channel-%d idx %d \n", channel, idx);
            char *memory = memory_idx_pool.get_free_chunk(idx);
            memcpy(memory, &packet->diffteststate, sizeof(DmaPackge));
        #endif
    #else
            memset(rdata, 1, BLOCK_SIZE);
    #endif
        }
    }

    // 处理接收到的数据
    void processData() {
        static int get_diff_count = 0;
        while (running) {
        #if defined (HAVE_FPGA) && defined(LOOP_BACK)
            DmaPackge send_packg[4];
            encapsulation_packge(&send_packg[0]);encapsulation_packge(&send_packg[1]);
            encapsulation_packge(&send_packg[2]);encapsulation_packge(&send_packg[3]);
            write(xdma_h2c_fd, send_packg, sizeof(send_packg));
            //printf("send pack id %d \n", send_packg.pack_indx);
            if(memory_idx_pool.check_group() == true) {
                const char *memory = memory_idx_pool.get_busy_chunk();
                memcpy(&test_packge, memory, sizeof(DiffTestState));
                printf("get difftest %d\n",get_diff_count);
                get_diff_count ++;
            }

        #else
            const char *memory = memory_pool.get_busy_chunk();
            memcpy(&test_packge, memory, sizeof(DmaPackge));
            memory_pool.set_free_chunk();
        #endif

            stream_receiver_cout ++;

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
};

int main() {
    struct timespec ts_start, ts_end;
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
        std::cout << "The C2H theoretical rate of a thread = "; 
#endif
        std::cout << rate_bytes / 1024 / 1024 << "MB/s" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
