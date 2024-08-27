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

#include "dma_mmpool.cpp"
#include "diffstate.h"

#define DEVICE_NAME_DEFAULT "/dev/xdma0_c2h_0"

DiffTestState diffteststate;
MemoryPool memory_pool;

std::mutex test_mtx;
std::condition_variable test_cv;

uint64_t stream_receiver_cout = 0;

class StreamReceiver {
public:
    // 构造函数：初始化StreamReceiver对象
    StreamReceiver(const char* xdma_path)
        : running(false),
          xdma_path(xdma_path) {
    }

    // 析构函数：确保资源被正确释放
    ~StreamReceiver() {
        stop();
    }

    // 启动流接收和处理
    void start() {
        if (running.exchange(true)) return; // 如果已经运行，直接返回

#ifdef HAVE_FPGA
        xdma_fd = open(xdma_path.c_str(), O_RDONLY);
        if (xdma_fd < 0) {
            throw std::runtime_error("Failed to open XDMA device");
        }
#endif

        receive_thread = std::thread(&StreamReceiver::receiveStream, this);
        process_thread = std::thread(&StreamReceiver::processData, this);
    }

    // 停止流接收和处理
    void stop() {
        if (!running.exchange(false)) return; // 如果已经停止，直接返回

        // 通知线程停止
        memory_pool.stopMemoryPool();

        if (receive_thread.joinable()) receive_thread.join();
        if (process_thread.joinable()) process_thread.join();
#ifdef HAVE_FPGA
        close(xdma_fd);
#endif
    }

private:
    std::atomic<bool> running;             // 运行标志

    std::thread receive_thread;  // 接收线程
    std::thread process_thread;  // 处理线程

    std::string xdma_path;       // XDMA设备路径
    int xdma_fd;                 // XDMA文件描述符

    // 接收流数据
    void receiveStream() {
        while (running) {
            char *memory = memory_pool.get_free_chunk();
        #ifdef HAVE_FPGA
            read(xdma_fd, memory, BLOCK_SIZE);
        #else
            memset(memory, 1, BLOCK_SIZE);
        #endif
            memory_pool.set_busy_chunk();
        }
    }

    // 处理接收到的数据
    void processData() {
        while (running) {
            const char *memory = memory_pool.get_busy_chunk();
            memcpy(&diffteststate, memory, sizeof(diffteststate));

            stream_receiver_cout ++;
            memory_pool.set_free_chunk();
            if (stream_receiver_cout >= TEST_NUM) {
            printf("Reaching the statistical upper limit\n");
            std::lock_guard<std::mutex> lock(test_mtx);
            test_cv.notify_all();
            }
        }
    }
};

int main() {
    struct timespec ts_start, ts_end;
    try {
        StreamReceiver receiver(DEVICE_NAME_DEFAULT);
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        receiver.start();

        // 等待一段时间或直到满足停止条件
        {
            std::unique_lock<std::mutex> lock(test_mtx);
            test_cv.wait(lock, [&]() {return stream_receiver_cout >= TEST_NUM;});
        }
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        receiver.stop();
		// subtract the start time from the end time
		double time_consum = (ts_end.tv_sec - ts_start.tv_sec) + 
                     ((ts_end.tv_nsec - ts_start.tv_nsec) / (double)1000000000);
        std::cout << "Program time consumption: " << time_consum << " S"
                  << " Transfer block: " << stream_receiver_cout << std::endl;
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
