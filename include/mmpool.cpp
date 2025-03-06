#include "mmpool.h"

#if defined(__GNUC__) || defined(__clang__)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define unlikely(x) (x)
#endif

void MemoryPool::init_memory_pool() {
  memory_pool.reserve(NUM_BLOCKS);
  for (size_t i = 0; i < NUM_BLOCKS; ++i) {
    memory_pool.emplace_back();
    block_mutexes[i].unlock();
  }
}

void MemoryPool::cleanup_memory_pool() {
  cv_empty.notify_all();
  cv_filled.notify_all();
  memory_pool.clear();
}

void MemoryPool::unlock_thread() {
  cv_empty.notify_all();
  cv_filled.notify_all();
}

char *MemoryPool::get_free_chunk() {
  page_head = (write_index++) & REM_NUM_BLOCKS;
  {
    std::unique_lock<std::mutex> lock(block_mutexes[page_head]);
    cv_empty.wait(lock, [this] { return empty_blocks > 0; });
  }

  --empty_blocks;
  block_mutexes[page_head].lock();
  return memory_pool[page_head].data.get();
}

void MemoryPool::set_busy_chunk() {
  memory_pool[page_head].is_free = false;
  block_mutexes[page_head].unlock();
  cv_filled.notify_one();
  ++filled_blocks;
}

const char *MemoryPool::get_busy_chunk() {
  page_end = (read_index++) & REM_NUM_BLOCKS;
  {
    std::unique_lock<std::mutex> lock(block_mutexes[page_end]);
    cv_filled.wait(lock, [this] { return filled_blocks > 0; });
  }
  --filled_blocks;
  block_mutexes[page_end].lock();
  return memory_pool[page_end].data.get();
}

void MemoryPool::set_free_chunk() {
  memory_pool[page_end].is_free = true;
  block_mutexes[page_end].unlock();
  cv_empty.notify_one();
  ++empty_blocks;
}

// Cleaning up memory pools
void MemoryIdxPool::cleanupMemoryPool() {
  cv_empty.notify_all();
  cv_filled.notify_all();
}

// Write a specified free block of a free window
bool MemoryIdxPool::write_free_chunk(uint8_t idx, const char *data) {
  size_t page_w_idx;
  {
    std::lock_guard<SpinLock> lock(offset_mutexes);

    page_w_idx = idx + group_w_offset.load(std::memory_order_relaxed);
    // Processing of winding data at the boundary
    if (unlikely(memory_pool[page_w_idx].is_free.load(std::memory_order_relaxed) == false)) {
      size_t this_group = group_w_idx.load(std::memory_order_relaxed);
      size_t offset = ((this_group & REM_MAX_GROUPING_IDX) * MAX_IDX);
      page_w_idx = idx + offset;
      write_next_count.fetch_add(1, std::memory_order_relaxed);
      // Lookup failed
      if (memory_pool[page_w_idx].is_free.load(std::memory_order_relaxed) == false) {
        printf("This block has been written, and there is a duplicate packge idx %d\n", idx);
        return false;
      }
    } else {
      write_count.fetch_add(1, std::memory_order_relaxed);
      // Proceed to the next group
      if (unlikely(write_count.load(std::memory_order_relaxed) == MAX_IDX)) {
        memcpy(memory_pool[page_w_idx].data.get(), data, mem_block_size);
        memory_pool[page_w_idx].is_free.store(false);
        size_t next_w_idx = wait_next_free_group();
        group_w_offset.store((next_w_idx & REM_MAX_GROUPING_IDX) * MAX_IDX);
        write_count.store(write_next_count);
        write_next_count.store(0);
        return true;
      }
    }
    memory_pool[page_w_idx].is_free.store(false);
  }
  memcpy(memory_pool[page_w_idx].data.get(), data, mem_block_size);

  return true;
}

void MemoryIdxPool::wait_mempool_start() {
  std::unique_lock<std::mutex> lock(window_mutexes);
  cv_filled.wait(lock);
}

bool MemoryIdxPool::read_busy_chunk(char *data) {
  size_t page_r_idx = read_count + group_r_offset;
  size_t this_r_idx = ++read_count;

  if (this_r_idx == MAX_IDX) {
    read_count = 0;
    size_t next_r_idx = wait_next_full_group();
    group_r_offset = ((next_r_idx & REM_MAX_GROUPING_IDX) * MAX_IDX);
  }
  if (memory_pool[page_r_idx].is_free.load() == true) {
    printf("An attempt was made to read the block of free %zu\n", page_r_idx);
    return false;
  }

  memcpy(data, memory_pool[page_r_idx].data.get(), mem_block_size);
  __builtin_prefetch(memory_pool[page_r_idx + 1].data.get(), 0, 2);
  memory_pool[page_r_idx].is_free.store(true);
  return true;
}

size_t MemoryIdxPool::wait_next_free_group() {
  size_t free_num = empty_blocks.fetch_sub(1, std::memory_order_relaxed) - 1;
  cv_filled.notify_all();
  //Reserve at least two free blocks
  if (free_num <= 2) {
    std::unique_lock<std::mutex> lock(window_mutexes);
    cv_empty.wait(lock, [this] { return empty_blocks.load() > 1; });
  }
  return group_w_idx.fetch_add(1);
}

size_t MemoryIdxPool::wait_next_full_group() {
  size_t free_num = empty_blocks.fetch_add(1, std::memory_order_relaxed) + 1;
  cv_empty.notify_all();

  if (free_num >= MAX_GROUP_READ) {
    std::unique_lock<std::mutex> lock(window_mutexes);
    cv_filled.wait(lock, [this] { return empty_blocks.load() < MAX_GROUP_READ; });
  }
  return group_r_idx.fetch_add(1);
}

bool MemoryIdxPool::check_group() {
  bool result = (group_w_idx.load() > group_r_idx.load()) ? true : false;
  return result;
}
