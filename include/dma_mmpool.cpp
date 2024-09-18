#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <memory>
#include <functional>
#define FIFO_SIZE 8192 * 1024 // 4M fifo
#define PAGE_SIZE 4096  // 4K packge
#define BLOCK_SIZE PAGE_SIZE // one receive size 32KB
#define NUM_BLOCKS (FIFO_SIZE / BLOCK_SIZE)
#define REM_NUM_BLOCKS (NUM_BLOCKS - 1) //需要2的倍数

extern bool running;
    // 内存块结构
    struct MemoryBlock {
        std::unique_ptr<char, std::function<void(char*)>> data;
        std::atomic<bool> is_free;

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
        for (size_t i = 0; i < NUM_BLOCKS; ++i) {
            block_mutexes[i].unlock();
        }
    }

    // 清理内存池
    void cleanupMemoryPool() {
        cv_empty.notify_all();
        cv_filled.notify_all();
    }

    void stopMemoryPool() {
        cv_empty.notify_all();
        cv_filled.notify_all();
    }

    // 获取下一个块并上锁 返回空闲块的指针
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
        block_mutexes[page_end].unlock();
        cv_empty.notify_one();
        ++empty_blocks;
    }
private:
    MemoryBlock memory_pool[NUM_BLOCKS];  // 内存池
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



#define MAX_IDX 256
#define MAX_GROUPING_IDX  NUM_BLOCKS / MAX_IDX
#define REM_MAX_IDX  (MAX_IDX - 1)
#define REM_MAX_GROUPING_IDX  (MAX_GROUPING_IDX - 1)

// 根据索引宽度拆分内存池
class MemoryIdxPool {
public:
    // 构造函数，分配对齐的内存块
    MemoryIdxPool() {
        initMemoryPool();
    }

    ~MemoryIdxPool() {
        cleanupMemoryPool();
    }
    // 禁止拷贝构造函数和拷贝赋值操作符
    MemoryIdxPool(const MemoryIdxPool&) = delete;
    MemoryIdxPool& operator=(const MemoryIdxPool&) = delete;

    // 初始化内存池
    void initMemoryPool() {
        for (size_t i = 0; i < NUM_BLOCKS; ++i) {
            memory_pool[i].is_free = true;
        }
    }

    // 清理内存池
    void cleanupMemoryPool() {
        cv_empty.notify_all();
        cv_filled.notify_all();
    }

    void stopMemoryPool() {
        cv_empty.notify_all();
        cv_filled.notify_all();
    }

    // 获得一个空闲组的指定空闲块 返回空闲块的指针
    char *get_free_chunk(uint8_t idx) {
        // 计算真实写入位置
        size_t page_w_idx = idx + grouping_w_offset;
        uint64_t this_group_w_idx = grouping_w_idx.load();
        //printf("get chunk %d counst %d\n", idx, page_w_idx);

        // 当前窗口对应位置已满 写入下一个窗口
        if (memory_pool[page_w_idx].is_free.load() == false) {
            write_next_index.fetch_add(1);
            size_t offset = (((this_group_w_idx + 1) & REM_MAX_GROUPING_IDX) * MAX_IDX);
            page_w_idx = idx + offset;
            printf("waring need group %d w_offset %d\n", ((this_group_w_idx + 1) & REM_MAX_GROUPING_IDX), grouping_w_offset.load());
            // 第二次查找失败
            printf("write chunk idx_offset %d idx %d\n", page_w_idx, idx);
            if (memory_pool[page_w_idx].is_free.load() == false) {
                printf("This block has been written, and there is a duplicate packge idx %d\t",idx);
                return nullptr;
            }
        } else {
            write_index.fetch_add(1);
        }
        memory_pool[page_w_idx].is_free.store(false);
        // 进入到下一个分组
        if (write_index.load() == MAX_IDX) {
            wait_next_free_group();
            grouping_w_offset = (grouping_w_idx.load() & REM_MAX_GROUPING_IDX) * MAX_IDX;
            printf("[write difftest packge] grouping_w_idx is full,get next %d\n", grouping_w_idx.load() & REM_MAX_GROUPING_IDX);
            write_index = write_next_index.load();
            write_next_index = 0;
        }
        return memory_pool[page_w_idx].data.get();
    }

    // 获取最前面的内存
    const char *get_busy_chunk() {
        size_t page_r_idx = read_index.load() + grouping_r_offset;
        read_index.fetch_add(1);
        size_t this_r_idx = read_index.load();
        if (this_r_idx == MAX_IDX) {
            read_index.store(0);
            wait_next_group();
            printf("grouping_r_idx %d \n", grouping_r_idx & REM_MAX_GROUPING_IDX);
            grouping_r_offset = (grouping_r_idx & REM_MAX_GROUPING_IDX) * MAX_IDX;
        }
        memory_pool[page_r_idx].is_free.store(true);
        return memory_pool[page_r_idx].data.get();
    }

    // 等待空闲内存块
    bool wait_next_free_group() {
        empty_blocks.fetch_sub(1);
        size_t free_num = empty_blocks.load();
        printf("w_idx - r_idx %d \n", free_num);
        if (free_num < 1) {
            printf("loack [next_free_group]\n");
            std::unique_lock<std::mutex> lock(block_mutexes[free_num]);
            cv_empty.wait(lock, [this] { return empty_blocks.load() >= 1;});
        }
        cv_filled.notify_all();
        grouping_w_idx.fetch_add(1);
        return true;
    }

    // 等待数据可用
    bool wait_next_group() {
        empty_blocks.fetch_add(1);
        size_t free_num = empty_blocks.load();
        printf("w_idx - r_idx %d \n",free_num);
        if (free_num >= MAX_GROUPING_IDX) {
            printf("lock [wait_next_group]\n");
            std::unique_lock<std::mutex> lock(block_mutexes[free_num]);
            cv_filled.wait(lock, [this] { return empty_blocks.load() < MAX_GROUPING_IDX;});
        }
        cv_empty.notify_all();
        grouping_r_idx.fetch_add(1);

        return true;
    }

    //检查有无数据块可用
    bool check_group() {
        bool result = (grouping_w_idx.load() > grouping_r_idx.load()) ? true : false;
        if(result) printf("check ture = w_idx %ld r_idx %ld\n",grouping_w_idx.load(), grouping_r_idx.load());
        return result;
    }

private:

    MemoryBlock memory_pool[NUM_BLOCKS];  // 内存池
    std::vector<std::mutex> block_mutexes{MAX_GROUPING_IDX}; // 分区锁数组
    
    std::condition_variable cv_empty;      // 空闲块条件变量
    std::condition_variable cv_filled;     // 已填充块条件变量
    std::atomic<size_t> empty_blocks = MAX_GROUPING_IDX;     // 空闲块计数

    std::atomic<size_t> grouping_r_offset = 0; // 当前消费者使用的偏移量
    std::atomic<size_t> grouping_w_offset = 0; // 当前生产者使用的偏移量
    std::atomic<size_t> grouping_w_idx = 0;
    std::atomic<size_t> grouping_r_idx = 0;
    std::atomic<size_t> write_index;       // 写入索引
    std::atomic<size_t> write_next_index;
    std::atomic<size_t> read_index;        // 读取索引
};
