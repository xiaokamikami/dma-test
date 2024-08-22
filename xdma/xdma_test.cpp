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

#include "diffstate.h"
DiffTestState diffteststate;

#define  BlockSize 4096
#define  TestTime 10
uint64_t stream_receiver_cout = 0;

class StreamReceiver {
public:
    // 构造函数：初始化StreamReceiver对象
    StreamReceiver(const char* xdma_path, size_t fifo_size = 16 * 1024 * 1024, size_t block_size = BlockSize)
        : BLOCK_SIZE(block_size), 
          FIFO_SIZE(fifo_size),
          NUM_BLOCKS(FIFO_SIZE / BLOCK_SIZE),
          empty_blocks(NUM_BLOCKS),
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

        xdma_fd = open(xdma_path.c_str(), O_RDONLY);
        if (xdma_fd < 0) {
            throw std::runtime_error("Failed to open XDMA device");
        }

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

        close(xdma_fd);
    }

private:
    // 内存块结构
    struct MemoryBlock {
        std::unique_ptr<char[]> data;
        bool is_free;

        MemoryBlock(size_t size) : data(std::make_unique<char[]>(size)), is_free(true) {}
    };

    const size_t BLOCK_SIZE;    // 单个内存块大小
    const size_t FIFO_SIZE;     // FIFO总大小
    const size_t NUM_BLOCKS;    // 内存块数量

    std::vector<MemoryBlock> memory_pool;  // 内存池
    std::atomic<size_t> empty_blocks;      // 空闲块计数
    std::atomic<size_t> filled_blocks;     // 已填充块计数
    std::mutex fifo_mutex;                 // FIFO互斥锁
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
            memory_pool.emplace_back(BLOCK_SIZE);
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
                std::unique_lock<std::mutex> lock(fifo_mutex);
                cv_empty.wait(lock, [this] { return empty_blocks > 0 || !running; });
                if (!running) break;
                --empty_blocks;
            }

            size_t current_block = write_index.fetch_add(1) % NUM_BLOCKS;

            // 从XDMA读取数据
            ssize_t bytes_read = read(xdma_fd, memory_pool[current_block].data.get(), BLOCK_SIZE);
            if (bytes_read <= 0) {
                // 处理错误或流结束
                break;
            }
            memory_pool[current_block].is_free = false;

            // 通知有新的已填充块
            {
                std::lock_guard<std::mutex> lock(fifo_mutex);
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
                std::unique_lock<std::mutex> lock(fifo_mutex);
                cv_filled.wait(lock, [this] { return filled_blocks > 0 || !running; });
                if (!running) break;
                --filled_blocks;
            }

            size_t current_block = read_index.fetch_add(1) % NUM_BLOCKS;

            // 处理数据
            std::cout << "Processing block " << current_block << std::endl;
            // 引入difftest 寄存器cpy耗时
            memcpy(&diffteststate, memory_pool[current_block].data.get(), sizeof(diffteststate));
            stream_receiver_cout ++;
            // 模拟处理时间
            // std::this_thread::sleep_for(std::chrono::milliseconds(1));

            memory_pool[current_block].is_free = true;

            // 通知有新的空闲块
            {
                std::lock_guard<std::mutex> lock(fifo_mutex);
                ++empty_blocks;
                cv_empty.notify_one();
            }
        }
    }
};

int main() {
    try {
        StreamReceiver receiver("/dev/xdma0_c2h_0");
        receiver.start();

        // 等待一段时间或直到满足停止条件
        std::this_thread::sleep_for(std::chrono::seconds(TestTime));

        receiver.stop();

        // Pref
        uint64_t receive_bytes = BlockSize * stream_receiver_cout;
        double rate_bytes = (double)receive_bytes / TestTime;
        std::cout << "XDMA Stream rate = " << rate_bytes << "MB/s" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
