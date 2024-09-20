#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <memory>
#include <functional>
#define FIFO_SIZE 8192 * 1024 // 8M fifo
#define PAGE_SIZE 4096  // 4K packge
#define BLOCK_SIZE PAGE_SIZE // one receive size 32KB
#define NUM_BLOCKS (FIFO_SIZE / BLOCK_SIZE)
#define REM_NUM_BLOCKS (NUM_BLOCKS - 1) //需要2的倍数

extern bool running;
    // 内存块结构
    struct MemoryBlock {
        std::unique_ptr<char[], std::function<void(char*)>> data;
        std::atomic<bool> is_free;

        MemoryBlock() : is_free(true) {
            void* ptr = nullptr;
            if (posix_memalign(&ptr, 4096, 4096) != 0) {
                throw std::runtime_error("Failed to allocate aligned memory");
            }
            memset(ptr, 0, 4096);
            data = std::unique_ptr<char[], std::function<void(char*)>>(
                static_cast<char*>(ptr),
                [](char* p) { free(p); }
            );
        }
        // Disable copy operations
        MemoryBlock(const MemoryBlock&) = delete;
        MemoryBlock& operator=(const MemoryBlock&) = delete;

        // Enable move operations
        MemoryBlock(MemoryBlock&&) = default;
        MemoryBlock& operator=(MemoryBlock&&) = default;
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
    std::atomic<size_t> empty_blocks {NUM_BLOCKS};      // 空闲块计数
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
    void initMemoryPool() { }

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
    bool write_free_chunk(uint8_t idx, const char *data) {
        size_t page_w_idx;
    {
        std::lock_guard <std::mutex> lock(offset_mutexes);
        // 计算真实写入位置
        page_w_idx = idx + grouping_w_offset;
        //printf("get chunk %d counst %d\n", idx, page_w_idx);
        // 当前窗口对应位置已满 写入下一个窗口
        if (memory_pool[page_w_idx].is_free.load() == false) {
            size_t this_group_w_idx = grouping_w_idx.load();
            size_t offset = ((this_group_w_idx & REM_MAX_GROUPING_IDX) * MAX_IDX);
            page_w_idx = idx + offset;
            write_next_index ++;
            //printf("waring need group %d %d w_offset %d\n", (this_group_w_idx & REM_MAX_GROUPING_IDX), this_group_w_idx, grouping_w_offset);
            // 第二次查找失败
            //printf("waring write chunk idx_offset %d idx %d\n", page_w_idx, idx);
            if (memory_pool[page_w_idx].is_free.load() == false) {
                printf("This block has been written, and there is a duplicate packge idx %d\t",idx);
                return false;
            }
        } else {
            write_index ++;
            // 进入到下一个分组
            if (write_index == MAX_IDX) {
                size_t next_w_idx = wait_next_free_group();
                grouping_w_offset = (next_w_idx & REM_MAX_GROUPING_IDX) * MAX_IDX;
                //printf("[write difftest packge] group_w is full,get next %d, next cont %d\n", next_w_idx & REM_MAX_GROUPING_IDX, write_next_index.load());
                write_index = write_next_index;
                write_next_index = 0;
            }
        }
    }
        memcpy(memory_pool[page_w_idx].data.get(), data, 4096);
        memory_pool[page_w_idx].is_free.store(false);
        return true;
    }

    // 获取最前面的内存
    bool read_busy_chunk(char *data) {
        size_t page_r_idx = read_index.fetch_add(1) + grouping_r_offset;
        size_t this_r_idx = read_index.load();
        if (this_r_idx == MAX_IDX) {
            read_index.store(0);
            size_t next_r_idx = wait_next_group();
            //printf("grouping_r_idx %d \n", next_r_idx & REM_MAX_GROUPING_IDX);
            grouping_r_offset = ((next_r_idx & REM_MAX_GROUPING_IDX) * MAX_IDX);
        }
        memory_pool[page_r_idx].is_free.store(true);
        memcpy(data, memory_pool[page_r_idx].data.get(), 4096);
        return true;
    }

    // 等待空闲内存块
    size_t wait_next_free_group() {
        empty_blocks.fetch_sub(1);
        size_t free_num = empty_blocks.load();
        cv_filled.notify_all();

        printf("get free w_idx - r_idx %d \n", free_num);
        if (free_num <= 1) {
            //printf("lock [next_free_group]\n");
            std::unique_lock<std::mutex> lock(shift_mutexes);
            cv_empty.wait(lock, [this] { return empty_blocks.load() > 1;});
        }
        return grouping_w_idx.fetch_add(1);
    }

    // 等待数据可用
    size_t wait_next_group() {
        empty_blocks.fetch_add(1);
        size_t free_num = empty_blocks.load();
        cv_empty.notify_all();

        //printf("get full w_idx - r_idx %d \n",free_num);
        if (free_num >= MAX_GROUPING_IDX) {
            //printf("lock [wait_next_group]\n");
            std::unique_lock<std::mutex> lock(shift_mutexes);
            cv_filled.wait(lock, [this] { return empty_blocks.load() < MAX_GROUPING_IDX;});
        }
        return grouping_r_idx.fetch_add(1);
    }

    //检查有无数据块可用
    bool check_group() {
        bool result = (grouping_w_idx.load() > grouping_r_idx.load()) ? true : false;
        return result;
    }

private:
    MemoryBlock memory_pool[NUM_BLOCKS];  // 内存池
    std::mutex shift_mutexes; // 窗口滑动保护
    std::mutex offset_mutexes; // 偏移量计算保护
    std::condition_variable cv_empty;      // 空闲块条件变量
    std::condition_variable cv_filled;     // 已填充块条件变量

    size_t grouping_r_offset = 0; // 当前消费者使用的偏移量
    size_t grouping_w_offset = 0; // 当前生产者使用的偏移量
    size_t write_index;           // 写入索引
    size_t write_next_index;

    std::atomic<size_t> empty_blocks{MAX_GROUPING_IDX};     // 空闲块计数
    std::atomic<size_t> grouping_w_idx{1};
    std::atomic<size_t> grouping_r_idx{1};
    std::atomic<size_t> read_index;        // 读取索引
};
