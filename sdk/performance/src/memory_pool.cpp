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
#include <mutex>
#include <vector>

// ============================================================================
// 内存块
// ============================================================================

struct MemBlock {
    MemBlock* next;
    bool in_use;
    size_t size;
};

struct MemoryPool {
    MemoryPoolConfig config;
    size_t block_size;
    char* memory;
    size_t total_size;
    size_t num_blocks;
    
    MemBlock* free_list;
    std::atomic<size_t> allocated_blocks;
    std::atomic<size_t> free_blocks;
    std::atomic<uint64_t> total_allocated;
    std::atomic<uint64_t> total_freed;
    
    std::mutex mutex;
    
    MemoryPool() : memory(nullptr), free_list(nullptr) {}
};

// ============================================================================
// 生命周期
// ============================================================================

extern "C" {

MemoryPool* mem_pool_create(const MemoryPoolConfig* config) {
    if (!config || config->block_size == 0) return nullptr;
    
    auto* pool = new (std::nothrow) MemoryPool();
    if (!pool) return nullptr;
    
    pool->config = *config;
    pool->block_size = config->block_size + sizeof(MemBlock);
    pool->num_blocks = config->initial_blocks;
    pool->total_size = pool->block_size * pool->num_blocks;
    
    // 分配内存
    pool->memory = (char*)std::aligned_alloc(64, pool->total_size);
    if (!pool->memory) {
        delete pool;
        return nullptr;
    }
    
    // 初始化内存块链表
    pool->free_list = nullptr;
    for (size_t i = 0; i < pool->num_blocks; i++) {
        char* block_mem = pool->memory + i * pool->block_size;
        auto* block = reinterpret_cast<MemBlock*>(block_mem);
        block->next = pool->free_list;
        block->in_use = false;
        block->size = config->block_size;
        pool->free_list = block;
    }
    
    pool->allocated_blocks = 0;
    pool->free_blocks = pool->num_blocks;
    pool->total_allocated = 0;
    pool->total_freed = 0;
    
    return pool;
}

void mem_pool_destroy(MemoryPool* pool) {
    if (pool) {
        std::free(pool->memory);
        delete pool;
    }
}

void mem_pool_reset(MemoryPool* pool) {
    if (!pool) return;
    
    std::lock_guard<std::mutex> lock(pool->mutex);
    
    pool->free_list = nullptr;
    for (size_t i = 0; i < pool->num_blocks; i++) {
        char* block_mem = pool->memory + i * pool->block_size;
        auto* block = reinterpret_cast<MemBlock*>(block_mem);
        block->next = pool->free_list;
        block->in_use = false;
        pool->free_list = block;
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
        void* ptr = std::aligned_alloc(64, size + sizeof(MemBlock));
        if (!ptr) return nullptr;
        
        auto* block = reinterpret_cast<MemBlock*>(ptr);
        block->in_use = true;
        block->size = size;
        block->next = nullptr;  // 标记为直接分配
        
        pool->total_allocated++;
        return block + 1;
    }
    
    std::lock_guard<std::mutex> lock(pool->mutex);
    
    // 从空闲列表获取
    if (!pool->free_list) {
        // 尝试扩展 (如果允许)
        if (pool->config.max_blocks == 0 || 
            pool->num_blocks < pool->config.max_blocks) {
            // 简单扩展: 翻倍
            size_t new_blocks = pool->num_blocks * 2;
            if (pool->config.max_blocks > 0 && new_blocks > pool->config.max_blocks) {
                new_blocks = pool->config.max_blocks;
            }
            
            size_t new_size = pool->block_size * new_blocks;
            char* new_mem = (char*)std::aligned_alloc(64, new_size);
            if (new_mem) {
                // 复制原有数据
                std::memcpy(new_mem, pool->memory, pool->total_size);
                std::free(pool->memory);
                pool->memory = new_mem;
                
                // 初始化新块
                for (size_t i = pool->num_blocks; i < new_blocks; i++) {
                    char* block_mem = pool->memory + i * pool->block_size;
                    auto* block = reinterpret_cast<MemBlock*>(block_mem);
                    block->next = pool->free_list;
                    block->in_use = false;
                    block->size = pool->config.block_size;
                    pool->free_list = block;
                    pool->free_blocks++;
                }
                
                pool->num_blocks = new_blocks;
                pool->total_size = new_size;
            }
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
    
    // 检查是否是直接分配 (next==nullptr)
    if (block->next == nullptr && block->size > pool->config.block_size) {
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
        size_t used = pool->allocated_blocks.load() * pool->block_size;
        size_t total_alloc = pool->num_blocks * pool->block_size;
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
    pool->total_frames = frame_count;
    pool->available_frames = frame_count;
    
    // 对齐帧大小到64字节
    size_t aligned_size = (frame_size + 63) & ~63;
    size_t total_size = aligned_size * frame_count;
    
    pool->memory = (char*)std::aligned_alloc(64, total_size);
    if (!pool->memory) {
        delete pool;
        return nullptr;
    }
    
    // 初始化空闲列表
    pool->free_list = nullptr;
    for (size_t i = 0; i < frame_count; i++) {
        auto* block = reinterpret_cast<FrameBlock*>(pool->memory + i * aligned_size);
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
        reinterpret_cast<char*>(frame) - sizeof(FrameBlock)
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
