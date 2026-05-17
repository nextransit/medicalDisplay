#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 内存池配置
// ============================================================================
typedef struct {
    size_t block_size;       // 块大小
    size_t initial_blocks;    // 初始块数
    size_t max_blocks;        // 最大块数 (0=无限制)
    bool thread_safe;         // 线程安全
} MemoryPoolConfig;

// ============================================================================
// 内存池句柄
// ============================================================================
typedef struct MemoryPool MemoryPool;

// ============================================================================
// 生命周期
// ============================================================================

/**
 * 创建内存池
 * @param config 池配置
 * @return 池句柄，失败返回NULL
 */
MemoryPool* mem_pool_create(const MemoryPoolConfig* config);

/**
 * 销毁内存池
 * @param pool 池句柄
 */
void mem_pool_destroy(MemoryPool* pool);

/**
 * 重置内存池 (释放所有空闲块)
 * @param pool 池句柄
 */
void mem_pool_reset(MemoryPool* pool);

// ============================================================================
// 内存分配
// ============================================================================

/**
 * 分配内存 (从池)
 * @param pool 池句柄
 * @param size 大小
 * @return 内存指针，失败返回NULL
 */
void* mem_pool_alloc(MemoryPool* pool, size_t size);

/**
 * 释放内存 (返回池)
 * @param pool 池句柄
 * @param ptr 内存指针
 */
void mem_pool_free(MemoryPool* pool, void* ptr);

// ============================================================================
// 统计
// ============================================================================

/**
 * 获取统计信息
 * @param pool 池句柄
 * @param allocated_blocks 输出: 已分配块数
 * @param free_blocks 输出: 空闲块数
 * @param total_allocated 输出: 累计分配次数
 * @param total_freed 输出: 累计释放次数
 * @param wasted_bytes 输出: 浪费内存 (字节)
 */
void mem_pool_get_stats(MemoryPool* pool,
                      size_t* allocated_blocks,
                      size_t* free_blocks,
                      uint64_t* total_allocated,
                      uint64_t* total_freed,
                      size_t* wasted_bytes);

// ============================================================================
// 帧缓冲区池 (专用)
// ============================================================================

typedef struct FrameBufferPool FrameBufferPool;

/**
 * 创建帧缓冲区池 (用于视频处理)
 * @param frame_size 单帧大小 (字节)
 * @param frame_count 帧数
 * @return 池句柄
 */
FrameBufferPool* frame_pool_create(size_t frame_size, size_t frame_count);

/**
 * 销毁帧缓冲区池
 * @param pool 池句柄
 */
void frame_pool_destroy(FrameBufferPool* pool);

/**
 * 获取帧缓冲
 * @param pool 池句柄
 * @return 帧缓冲指针，NULL表示无可用缓冲
 */
void* frame_pool_acquire(FrameBufferPool* pool);

/**
 * 释放帧缓冲
 * @param pool 池句柄
 * @param frame 帧缓冲指针
 */
void frame_pool_release(FrameBufferPool* pool, void* frame);

/**
 * 批量获取帧缓冲
 * @param pool 池句柄
 * @param frames 输出数组
 * @param count 数量
 * @return 实际获取数量
 */
size_t frame_pool_acquire_batch(FrameBufferPool* pool, void** frames, size_t count);

/**
 * 获取池状态
 * @param pool 池句柄
 * @param total 输出: 总帧数
 * @param available 输出: 可用帧数
 */
void frame_pool_get_status(FrameBufferPool* pool, size_t* total, size_t* available);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_POOL_H
