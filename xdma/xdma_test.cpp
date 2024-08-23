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

#include "diffstate.h"

#define DEVICE_NAME_DEFAULT "/dev/xdma0_c2h_0"
#define FIFO_SIZE (16 * 1024 * 1024) // 16M
#define BLOCK_SIZE 4096  // 4K 对齐
#define NUM_BLOCKS (FIFO_SIZE / BLOCK_SIZE)
#define TEST_NUM  (NUM_BLOCKS * 40000)

DiffTestState diffteststate;

std::mutex test_mtx;
std::condition_variable test_cv;

uint64_t stream_receiver_cout = 0;

class StreamReceiver {
public:
    // 构造函数：初始化StreamReceiver对象
    StreamReceiver(const char* xdma_path)
        : empty_blocks(NUM_BLOCKS),
          filled_blocks(0),
          write_index(0),
          read_index(0),
          running(false),
          xdma_path(xdma_path) {
        
        initMemoryPool();
    }

    // 析构函数：确保资源被正确释放
    ~StreamReceiver() {
        stop();
        cleanupMemoryPool();
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
        cv_empty.notify_all();
        cv_filled.notify_all();

        if (receive_thread.joinable()) receive_thread.join();
        if (process_thread.joinable()) process_thread.join();
#ifdef HAVE_FPGA
        close(xdma_fd);
#endif
    }

private:
    // 内存块结构
    struct MemoryBlock {
        std::unique_ptr<char, std::function<void(char*)>> data;
        bool is_free;

        MemoryBlock() : is_free(true) {
            void* ptr = nullptr;
            if (posix_memalign(&ptr, BLOCK_SIZE, BLOCK_SIZE * 2) != 0) {
                throw std::runtime_error("Failed to allocate aligned memory");
            }
            data = std::unique_ptr<char, std::function<void(char*)>>(
                static_cast<char*>(ptr),
                [](char* p) { free(p); }
            );
        }
    };

    std::vector<MemoryBlock> memory_pool;  // 内存池
    std::vector<std::mutex> block_mutexes{NUM_BLOCKS}; // 分区锁数组
    std::atomic<size_t> empty_blocks;      // 空闲块计数
    std::atomic<size_t> filled_blocks;     // 已填充块计数
    std::condition_variable cv_empty;      // 空闲块条件变量
    std::condition_variable cv_filled;     // 已填充块条件变量
    std::atomic<size_t> write_index;       // 写入索引
    std::atomic<size_t> read_index;        // 读取索引
    std::atomic<bool> running;             // 运行标志

    std::thread receive_thread;  // 接收线程
    std::thread process_thread;  // 处理线程

    std::string xdma_path;       // XDMA设备路径
    int xdma_fd;                 // XDMA文件描述符

    // 初始化内存池
    void initMemoryPool() {
        memory_pool.reserve(NUM_BLOCKS);
        for (size_t i = 0; i < NUM_BLOCKS; ++i) {
            memory_pool.emplace_back();
        }
    }

    // 清理内存池
    void cleanupMemoryPool() {
        memory_pool.clear();
    }

    // 接收流数据
    void receiveStream() {
        while (running) {
            // 等待空闲块
            {
                std::unique_lock<std::mutex> lock(block_mutexes[write_index % NUM_BLOCKS]);
                cv_empty.wait(lock, [this] { return empty_blocks > 0 || !running; });
                if (!running) break;
                --empty_blocks;
            }

            size_t current_block = write_index.fetch_add(1) % NUM_BLOCKS;

            // 从XDMA读取数据
#ifdef HAVE_FPGA
            ssize_t bytes_read = read(xdma_fd, memory_pool[current_block].data.get(), BLOCK_SIZE);
            if (bytes_read <= 0) {
                // 处理错误或流结束
                break;
            }
#else
            memset(memory_pool[current_block].data.get(), 1, BLOCK_SIZE);
#endif
            memory_pool[current_block].is_free = false;

            // 通知有新的已填充块
            {
                std::lock_guard<std::mutex> lock(block_mutexes[current_block]);
                ++filled_blocks;
                cv_filled.notify_one();
            }
        }
    }

    // 处理接收到的数据
    void processData() {
        while (running) {
            // 等待已填充块
            {
                std::unique_lock<std::mutex> lock(block_mutexes[read_index % NUM_BLOCKS]);
                cv_filled.wait(lock, [this] { return filled_blocks > 0 || !running; });
                if (!running) break;
                --filled_blocks;
            }

            size_t current_block = read_index.fetch_add(1) % NUM_BLOCKS;

            // 处理数据
            // 引入diff 寄存器cpy耗时
            memcpy(&diffteststate, memory_pool[current_block].data.get(), sizeof(diffteststate));
            stream_receiver_cout ++;
            // Checking exit conditions
            if (stream_receiver_cout >= TEST_NUM) {
                printf("Reaching the statistical upper limit\n");
                std::lock_guard<std::mutex> lock(test_mtx);
                test_cv.notify_all();
            }

            memory_pool[current_block].is_free = true;

            // 通知有新的空闲块
            {
                std::lock_guard<std::mutex> lock(block_mutexes[current_block]);
                ++empty_blocks;
                cv_empty.notify_one();
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
