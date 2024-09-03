#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <memory>
#include <functional>
#define FIFO_SIZE 4096 * 1024 // 4M fifo
#define PAGE_SIZE 4096  // 4K packge
#define BLOCK_SIZE PAGE_SIZE * 32// one receive size 32KB
#define NUM_BLOCKS (FIFO_SIZE / BLOCK_SIZE)
#define REM_NUM_BLOCKS (NUM_BLOCKS - 1)

extern bool running;
class MemoryPool {
public:
    // 构造函数，分配对齐的内存块
    MemoryPool() {
        initMemoryPool();
    }

    ~MemoryPool() {
        cleanupMemoryPool();
    }
    // 禁止拷贝构造函数和拷贝赋值操作符
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    // 初始化内存池
    void initMemoryPool() {
        memory_pool.reserve(NUM_BLOCKS);
        for (size_t i = 0; i < NUM_BLOCKS; ++i) {
            memory_pool.emplace_back();
            block_mutexes[i].unlock();
        }
    }

    // 清理内存池
    void cleanupMemoryPool() {
        cv_empty.notify_all();
        cv_filled.notify_all();
        memory_pool.clear();
    }

    void stopMemoryPool() {
        cv_empty.notify_all();
        cv_filled.notify_all();
    }


    // 检测一个空闲块并上锁 返回空闲块的指针
    char *get_free_chunk() {
        page_head = (write_index++) & REM_NUM_BLOCKS;
        {
            std::unique_lock<std::mutex> lock(block_mutexes[page_head]);
            cv_empty.wait(lock, [this] { return empty_blocks > 0;});
        }

        --empty_blocks;
        block_mutexes[page_head].lock();
        return memory_pool[page_head].data.get();
    }

    void set_busy_chunk() {
        memory_pool[page_head].is_free = false;
        block_mutexes[page_head].unlock();
        cv_filled.notify_one();
        ++filled_blocks;
    }

    // 获取最新块的内存
    const char *get_busy_chunk() {
        page_end = (read_index++) & REM_NUM_BLOCKS;
        {
            std::unique_lock<std::mutex> lock(block_mutexes[page_end]);
            cv_filled.wait(lock, [this] { return filled_blocks > 0; });
        }
        --filled_blocks;
        block_mutexes[page_end].lock();
        return memory_pool[page_end].data.get();
    }

    void set_free_chunk() {
        memory_pool[page_end].is_free = true;
        block_mutexes[page_end].unlock();
        cv_empty.notify_one();
        ++empty_blocks;
    }
private:
    // 内存块结构
    struct MemoryBlock {
        std::unique_ptr<char, std::function<void(char*)>> data;
        bool is_free;

        MemoryBlock() : is_free(true) {
            void* ptr = nullptr;
            if (posix_memalign(&ptr, 4096, 4096 + BLOCK_SIZE) != 0) {
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
    std::atomic<size_t> empty_blocks = NUM_BLOCKS;      // 空闲块计数
    std::atomic<size_t> filled_blocks;     // 已填充块计数
    std::atomic<size_t> write_index;       // 写入索引
    std::atomic<size_t> read_index;        // 读取索引
    std::condition_variable cv_empty;      // 空闲块条件变量
    std::condition_variable cv_filled;     // 已填充块条件变量
    size_t page_head = 0;
    size_t page_end = 0;
};