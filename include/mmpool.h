#ifndef __MPOOL_H__
#define __MPOOL_H__
#include <cstring>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <xmmintrin.h>
#include "diffstate.h"

#define MEMPOOL_SIZE   16384 * 1024 // 16M memory
#define MEMBLOCK_SIZE  4096         // 4K packge
#define NUM_BLOCKS     (MEMPOOL_SIZE / MEMBLOCK_SIZE)
#define REM_NUM_BLOCKS (NUM_BLOCKS - 1)
#define MAX_WINDOW_SIZE 256

class MemoryChunk{
public:
  std::atomic<size_t> memblock_idx;
  std::atomic<bool>   is_free;
  MemoryChunk() : memblock_idx(0), is_free(true) {}

  MemoryChunk(MemoryChunk &&other) noexcept
    : memblock_idx(other.memblock_idx.load()), is_free(other.is_free.load()) {}

  MemoryChunk(const MemoryChunk &other)
    : memblock_idx(other.memblock_idx.load()), is_free(other.is_free.load()) {}
};

class MemoryBlock {
public:
  std::unique_ptr<char[], std::function<void(char *)>> data;
  std::atomic<bool> is_free;
  uint64_t mem_block_size = MEMBLOCK_SIZE;
  // Default constructor
  MemoryBlock() : MemoryBlock(MEMBLOCK_SIZE) {}

  // Parameterized constructor
  MemoryBlock(uint64_t size) : is_free(true) {
    mem_block_size = size < MEMBLOCK_SIZE ? MEMBLOCK_SIZE : size;
    //printf("MemoryBlock size: %lu\n", mem_block_size);
    void *ptr = nullptr;
    if (posix_memalign(&ptr, 4096, mem_block_size) != 0) {
      throw std::runtime_error("Failed to allocate aligned memory");
    }
    memset(ptr, 0, mem_block_size);
    data = std::unique_ptr<char[], std::function<void(char *)>>(static_cast<char *>(ptr), [](char *p) { free(p); });
  }
  ~MemoryBlock() {
    data.reset();
  }

  // Move constructors
  MemoryBlock(MemoryBlock &&other) noexcept : data(std::move(other.data)), is_free(other.is_free.load()) {}

  // Move assignment operator
  MemoryBlock &operator=(MemoryBlock &&other) noexcept {
    if (this != &other) {
      data = std::move(other.data);
      is_free.store(other.is_free.load());
    }
    return *this;
  }

  // Disable the copy constructor and copy assignment operator
  MemoryBlock(const MemoryBlock &) = delete;
  MemoryBlock &operator=(const MemoryBlock &) = delete;
};

class SpinLock {
    std::atomic_flag locked = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (locked.test_and_set(std::memory_order_acquire)) 
            _mm_pause();
    }
    void unlock() {
        locked.clear(std::memory_order_release);
    }
};

class MemoryPool {
public:
  // Constructor to allocate aligned memory blocks
  MemoryPool() {
    init_memory_pool();
  }

  ~MemoryPool() {
    cleanup_memory_pool();
  }
  // Disable copy constructors and copy assignment operators
  MemoryPool(const MemoryPool &) = delete;
  MemoryPool &operator=(const MemoryPool &) = delete;

  void init_memory_pool();

  // Cleaning up memory pools
  void cleanup_memory_pool();
  // Releasing locks manually
  void unlock_thread();

  // Detect a free block and lock the memory that returns the free block
  char *get_free_chunk();
  // Set block data valid and locked
  void set_busy_chunk();

  // Gets the latest block of memory
  const char *get_busy_chunk();
  // Invalidate and lock the block
  void set_free_chunk();

private:
  std::vector<MemoryBlock> memory_pool;              // Mempool
  std::vector<std::mutex> block_mutexes{NUM_BLOCKS}; // Partition lock array
  std::atomic<size_t> empty_blocks{NUM_BLOCKS};      // Free block count
  std::atomic<size_t> filled_blocks;                 // Filled blocks count
  std::atomic<size_t> write_index;
  std::atomic<size_t> read_index;
  std::condition_variable cv_empty;  // Free block condition variable
  std::condition_variable cv_filled; // Filled block condition variable
  size_t page_head = 0;
  size_t page_end = 0;
};

// Split the memory pool into sliding Windows based on the index width
// Support multi-thread out-of-order write sequential read
class MemoryIdxPool {
private:
  const size_t MAX_IDX = 256;
  const size_t MAX_GROUPING_IDX = NUM_BLOCKS / MAX_IDX;
  const size_t MAX_GROUP_READ = MAX_GROUPING_IDX - 2; //The window needs to reserve two free Spaces
  const size_t REM_MAX_IDX = (MAX_IDX - 1);
  const size_t REM_MAX_GROUPING_IDX = (MAX_GROUPING_IDX - 1);
  uint64_t mem_block_size = MEMBLOCK_SIZE;

public:
  MemoryIdxPool(uint64_t block_size) : mem_block_size(block_size) {
    initMemoryPool();
  }
  MemoryIdxPool() : mem_block_size(MEMBLOCK_SIZE) {
  }
  ~MemoryIdxPool() {
  }
  // Disable copy constructors and copy assignment operators
  MemoryIdxPool(const MemoryIdxPool &) = delete;
  MemoryIdxPool &operator=(const MemoryIdxPool &) = delete;

  void initMemoryPool() {
    memory_pool.clear();
    memory_pool.reserve(mem_block_size);
    memory_order_ptr.clear();
    memory_order_ptr.resize(NUM_BLOCKS);
    printf("MemoryIdxPool block_size %ld\n", mem_block_size);
    for (size_t i = 0; i < MEMBLOCK_SIZE; ++i) {
      memory_pool.emplace_back(mem_block_size);
    }
    for (size_t i = 0; i < NUM_BLOCKS; i++)
    {
      memory_order_ptr[i].is_free.store(true);
      printf("MemoryIdxPool init %ld\n", i);
    }
    
  }
  // Get free block pointer increment is returned from the heap
  char *get_free_chunk(size_t *mem_idx);

  // Write a specified free block of a free window
  bool write_free_chunk(uint8_t idx, size_t mem_idx);

  // Get the head memory
  char *read_busy_chunk();

  // Set the block data valid and locked
  void set_free_chunk();

  // Wait for the data to be free
  size_t wait_next_free_group();

  // Wait for the data to be readable
  size_t wait_next_full_group();

  // Check if there is a window to read
  bool check_group();

  // Wait mempool have data
  void wait_mempool_start();

private:
  std::vector<MemoryBlock> memory_pool; // Mempool
  SpinLock offset_mutexes;           // w/r offset protection

  std::vector<MemoryChunk> memory_order_ptr;
  SpinLock order_mutex;

  size_t group_r_offset = 0; // The offset used by the current consumer

  std::atomic<size_t> wait_setfree_mem_idx {0};
  std::atomic<size_t> wait_setfree_ptr_idx {0};
  std::atomic<size_t> read_count {0};
  std::atomic<size_t> mem_chunk_idx {0};
  std::atomic<size_t> group_w_offset {0}; // The offset used by the current producer
  std::atomic<size_t> write_count {0};
  std::atomic<size_t> write_next_count {0};
  std::atomic<size_t> empty_blocks{MAX_GROUP_READ};
  std::atomic<size_t> group_w_idx{1};
  std::atomic<size_t> group_r_idx{1};
};

#endif
