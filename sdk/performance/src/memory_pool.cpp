/**
 * @file memory_pool.cpp
 * @brief 内存池实现
 * 
 * 性能优化: 减少malloc/free调用，降低延迟
 */

#include "memory_pool.h"
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <new>
#include <vector>

// ============================================================================
// 内存块
// ============================================================================

struct MemBlock {
    MemBlock* next;
    bool in_use;
    size_t size;
};

struct MemoryArena {
    char* memory;
    size_t total_size;
    size_t num_blocks;
};

struct MemoryPool {
    MemoryPoolConfig config;
    size_t block_stride;
    size_t total_size;
    size_t num_blocks;
    
    MemBlock* free_list;
    std::atomic<size_t> allocated_blocks;
    std::atomic<size_t> free_blocks;
    std::atomic<uint64_t> total_allocated;
    std::atomic<uint64_t> total_freed;
    
    std::mutex mutex;
    std::vector<MemoryArena> arenas;
    
    MemoryPool() : block_stride(0), total_size(0), num_blocks(0), free_list(nullptr) {}
};

static constexpr size_t kPoolAlignment = 64;

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static char* allocate_aligned_bytes(size_t size) {
    void* ptr = nullptr;
    const size_t aligned_size = align_up(size, kPoolAlignment);
    if (posix_memalign(&ptr, kPoolAlignment, aligned_size) != 0) {
        return nullptr;
    }
    return static_cast<char*>(ptr);
}

static bool add_arena(MemoryPool* pool, size_t block_count) {
    if (!pool || block_count == 0) {
        return false;
    }

    const size_t arena_size = pool->block_stride * block_count;
    char* memory = allocate_aligned_bytes(arena_size);
    if (!memory) {
        return false;
    }

    MemoryArena arena{memory, arena_size, block_count};
    pool->arenas.push_back(arena);
    pool->total_size += arena_size;

    for (size_t index = 0; index < block_count; ++index) {
        char* block_mem = memory + index * pool->block_stride;
        auto* block = reinterpret_cast<MemBlock*>(block_mem);
        block->next = pool->free_list;
        block->in_use = false;
        block->size = pool->config.block_size;
        pool->free_list = block;
    }

    pool->num_blocks += block_count;
    pool->free_blocks += block_count;
    return true;
}

// ============================================================================
// 生命周期
// ============================================================================

extern "C" {

MemoryPool* mem_pool_create(const MemoryPoolConfig* config) {
    if (!config || config->block_size == 0 || config->initial_blocks == 0) return nullptr;
    
    auto* pool = new (std::nothrow) MemoryPool();
    if (!pool) return nullptr;
    
    pool->config = *config;
    pool->block_stride = align_up(sizeof(MemBlock) + config->block_size, kPoolAlignment);
    pool->allocated_blocks = 0;
    pool->free_blocks = 0;
    pool->total_allocated = 0;
    pool->total_freed = 0;

    if (!add_arena(pool, config->initial_blocks)) {
        delete pool;
        return nullptr;
    }
    
    return pool;
}

void mem_pool_destroy(MemoryPool* pool) {
    if (pool) {
        for (const auto& arena : pool->arenas) {
            std::free(arena.memory);
        }
        delete pool;
    }
}

void mem_pool_reset(MemoryPool* pool) {
    if (!pool) return;
    
    std::lock_guard<std::mutex> lock(pool->mutex);
    
    pool->free_list = nullptr;
    for (const auto& arena : pool->arenas) {
        for (size_t index = 0; index < arena.num_blocks; ++index) {
            char* block_mem = arena.memory + index * pool->block_stride;
            auto* block = reinterpret_cast<MemBlock*>(block_mem);
            block->next = pool->free_list;
            block->in_use = false;
            block->size = pool->config.block_size;
            pool->free_list = block;
        }
    }
    
    pool->free_blocks = pool->num_blocks;
    pool->allocated_blocks = 0;
}

// ============================================================================
// 分配
// ============================================================================

void* mem_pool_alloc(MemoryPool* pool, size_t size) {
    if (!pool || size == 0) return nullptr;
    
    // 如果请求大小超过块大小，直接malloc
    if (size > pool->config.block_size) {
        char* ptr = allocate_aligned_bytes(sizeof(MemBlock) + size);
        if (!ptr) return nullptr;
        
        auto* block = reinterpret_cast<MemBlock*>(ptr);
        block->in_use = true;
        block->size = size;
        block->next = nullptr;
        
        pool->total_allocated++;
        return block + 1;
    }
    
    std::lock_guard<std::mutex> lock(pool->mutex);
    
    // 从空闲列表获取
    if (!pool->free_list) {
        // 尝试扩展 (如果允许)
        if (pool->config.max_blocks == 0 || 
            pool->num_blocks < pool->config.max_blocks) {
            size_t grow_blocks = pool->num_blocks > 0 ? pool->num_blocks : pool->config.initial_blocks;
            if (pool->config.max_blocks > 0) {
                const size_t remaining = pool->config.max_blocks - pool->num_blocks;
                if (grow_blocks > remaining) {
                    grow_blocks = remaining;
                }
            }
            add_arena(pool, grow_blocks);
        }
    }
    
    if (!pool->free_list) {
        return nullptr;  // 无法分配
    }
    
    // 获取空闲块
    MemBlock* block = pool->free_list;
    pool->free_list = block->next;
    
    block->in_use = true;
    pool->allocated_blocks++;
    pool->free_blocks--;
    pool->total_allocated++;
    
    return block + 1;
}

void mem_pool_free(MemoryPool* pool, void* ptr) {
    if (!pool || !ptr) return;
    
    auto* block = reinterpret_cast<MemBlock*>(ptr) - 1;
    
    if (block->size > pool->config.block_size) {
        std::free(block);
        pool->total_freed++;
        return;
    }
    
    std::lock_guard<std::mutex> lock(pool->mutex);
    
    block->in_use = false;
    block->next = pool->free_list;
    pool->free_list = block;
    pool->allocated_blocks--;
    pool->free_blocks++;
    pool->total_freed++;
}

// ============================================================================
// 统计
// ============================================================================

void mem_pool_get_stats(MemoryPool* pool,
                       size_t* allocated_blocks,
                       size_t* free_blocks,
                       uint64_t* total_allocated,
                       uint64_t* total_freed,
                       size_t* wasted_bytes) {
    if (!pool) return;
    
    if (allocated_blocks) *allocated_blocks = pool->allocated_blocks.load();
    if (free_blocks) *free_blocks = pool->free_blocks.load();
    if (total_allocated) *total_allocated = pool->total_allocated.load();
    if (total_freed) *total_freed = pool->total_freed.load();
    
    if (wasted_bytes) {
        size_t used = pool->allocated_blocks.load() * pool->block_stride;
        size_t total_alloc = pool->num_blocks * pool->block_stride;
        *wasted_bytes = total_alloc - used;
    }
}

// ============================================================================
// 帧缓冲区池
// ============================================================================

struct FrameBlock {
    FrameBlock* next;
    bool in_use;
    char data[];
};

struct FrameBufferPool {
    size_t frame_size;
    size_t block_stride;
    size_t total_frames;
    std::atomic<size_t> available_frames;
    
    char* memory;
    FrameBlock* free_list;
    std::mutex mutex;
    
    FrameBufferPool() : memory(nullptr), free_list(nullptr) {}
};

FrameBufferPool* frame_pool_create(size_t frame_size, size_t frame_count) {
    if (frame_size == 0 || frame_count == 0) return nullptr;
    
    auto* pool = new (std::nothrow) FrameBufferPool();
    if (!pool) return nullptr;
    
    pool->frame_size = frame_size;
    pool->block_stride = align_up(offsetof(FrameBlock, data) + frame_size, kPoolAlignment);
    pool->total_frames = frame_count;
    pool->available_frames = frame_count;
    
    const size_t total_size = pool->block_stride * frame_count;
    
    pool->memory = allocate_aligned_bytes(total_size);
    if (!pool->memory) {
        delete pool;
        return nullptr;
    }
    
    // 初始化空闲列表
    pool->free_list = nullptr;
    for (size_t i = 0; i < frame_count; i++) {
        auto* block = reinterpret_cast<FrameBlock*>(pool->memory + i * pool->block_stride);
        block->next = pool->free_list;
        block->in_use = false;
        pool->free_list = block;
    }
    
    return pool;
}

void frame_pool_destroy(FrameBufferPool* pool) {
    if (pool) {
        std::free(pool->memory);
        delete pool;
    }
}

void* frame_pool_acquire(FrameBufferPool* pool) {
    if (!pool) return nullptr;
    
    std::lock_guard<std::mutex> lock(pool->mutex);
    
    if (!pool->free_list) return nullptr;
    
    FrameBlock* block = pool->free_list;
    pool->free_list = block->next;
    block->in_use = true;
    pool->available_frames--;
    
    return block->data;
}

void frame_pool_release(FrameBufferPool* pool, void* frame) {
    if (!pool || !frame) return;
    
    std::lock_guard<std::mutex> lock(pool->mutex);
    
    auto* block = reinterpret_cast<FrameBlock*>(
        reinterpret_cast<char*>(frame) - offsetof(FrameBlock, data)
    );
    
    block->next = pool->free_list;
    block->in_use = false;
    pool->free_list = block;
    pool->available_frames++;
}

size_t frame_pool_acquire_batch(FrameBufferPool* pool, void** frames, size_t count) {
    if (!pool || !frames || count == 0) return 0;
    
    std::lock_guard<std::mutex> lock(pool->mutex);
    
    size_t acquired = 0;
    while (acquired < count && pool->free_list) {
        FrameBlock* block = pool->free_list;
        pool->free_list = block->next;
        block->in_use = true;
        frames[acquired++] = block->data;
        pool->available_frames--;
    }
    
    return acquired;
}

void frame_pool_get_status(FrameBufferPool* pool, size_t* total, size_t* available) {
    if (!pool) return;
    
    if (total) *total = pool->total_frames;
    if (available) *available = pool->available_frames.load();
}

} // extern "C"
