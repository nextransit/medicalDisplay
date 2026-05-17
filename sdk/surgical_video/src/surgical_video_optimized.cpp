/**
 * @file surgical_video_optimized.cpp
 * @brief 术野视频优化处理
 * 
 * 集成 SIMD 加速和内存池优化
 */

#include "surgical_video.h"
#include "memory_pool.h"
#include "simd_processing.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <thread>
#include <future>
#include <chrono>

// ============================================================================
// 优化配置
// ============================================================================

struct OptimizedSurgicalContext {
    // 内存池
    MemoryPool* frame_pool;
    
    // 预处理缓冲区 (预分配)
    std::vector<uint8_t> yuv_work;
    std::vector<uint8_t> rgb_work1;
    std::vector<uint8_t> rgb_work2;
    
    // SIMD 后端信息
    SIMDBackend simd_backend;
    const char* simd_backend_name;
    
    // 性能统计
    uint64_t total_pixels_processed;
    uint64_t simd_accelerated_pixels;
    
    OptimizedSurgicalContext() : frame_pool(nullptr), 
                                simd_backend(SIMD_NONE),
                                total_pixels_processed(0),
                                simd_accelerated_pixels(0) {
        simd_backend = simd_get_backend();
        simd_backend_name = simd_get_backend_name(simd_backend);
    }
};

static OptimizedSurgicalContext* g_context = nullptr;

// ============================================================================
// 初始化
// ============================================================================

void surgical_init_optimization(size_t max_frame_size, size_t num_buffers) {
    if (g_context) return;
    
    g_context = new OptimizedSurgicalContext();
    
    // 创建帧缓冲区池 (预分配16个4K帧缓冲)
    MemoryPoolConfig config = {};
    config.block_size = max_frame_size;
    config.initial_blocks = num_buffers;
    config.max_blocks = num_buffers * 2;
    config.thread_safe = true;
    
    g_context->frame_pool = mem_pool_create(&config);
    
    // 预分配工作缓冲区
    g_context->yuv_work.resize(max_frame_size);
    g_context->rgb_work1.resize(max_frame_size);
    g_context->rgb_work2.resize(max_frame_size);
}

// ============================================================================
// 优化后的帧处理
// ============================================================================

int surgical_process_frame_optimized(const uint8_t* input,
                                   int width, int height,
                                   const EnhancementParams* params,
                                   uint8_t* output) {
    if (!input || !output || !params) return -1;
    
    // 如果没有初始化，自动初始化
    if (!g_context) {
        surgical_init_optimization(width * height * 4, 16);
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    int pixel_count = width * height;
    size_t rgb_size = pixel_count * 3;
    
    // 使用 SIMD 加速的亮度/对比度/饱和度调整
    if (params->brightness != 0.0f || params->contrast != 1.0f) {
        // 使用 SIMD 加速
        if (g_context->simd_backend != SIMD_NONE) {
            // 调整亮度和对比度 (SIMD)
            simd_adjust_brightness_contrast(input, g_context->rgb_work1.data(),
                                         width, height,
                                         params->brightness, params->contrast);
            g_context->simd_accelerated_pixels += pixel_count;
        } else {
            // 标量实现
            float bright_offset = params->brightness * 128.0f;
            for (size_t i = 0; i < rgb_size; i += 3) {
                for (int c = 0; c < 3; c++) {
                    float val = input[i + c] + bright_offset;
                    val = (val - 128.0f) * params->contrast + 128.0f;
                    g_context->rgb_work1[i + c] = (uint8_t)std::max(0.0f, std::min(255.0f, val));
                }
            }
        }
    }
    
    // 饱和度调整 (SIMD)
    if (params->saturation != 1.0f) {
        const uint8_t* src = (params->brightness == 0.0f && params->contrast == 1.0f) ? input : g_context->rgb_work1.data();
        
        if (g_context->simd_backend != SIMD_NONE) {
            simd_adjust_saturation(src, g_context->rgb_work2.data(),
                                 width, height, params->saturation);
            g_context->simd_accelerated_pixels += pixel_count;
        } else {
            for (size_t i = 0; i < rgb_size; i += 3) {
                float r = src[i], g = src[i + 1], b = src[i + 2];
                float gray = 0.299f * r + 0.587f * g + 0.114f * b;
                g_context->rgb_work2[i] = (uint8_t)std::max(0.0f, std::min(255.0f, gray + (r - gray) * params->saturation));
                g_context->rgb_work2[i + 1] = (uint8_t)std::max(0.0f, std::min(255.0f, gray + (g - gray) * params->saturation));
                g_context->rgb_work2[i + 2] = (uint8_t)std::max(0.0f, std::min(255.0f, gray + (b - gray) * params->saturation));
            }
        }
        std::memcpy(output, g_context->rgb_work2.data(), rgb_size);
    } else {
        const uint8_t* src = (params->brightness == 0.0f && params->contrast == 1.0f) 
            ? input : g_context->rgb_work1.data();
        std::memcpy(output, src, rgb_size);
    }
    
    g_context->total_pixels_processed += pixel_count;
    
    auto end = std::chrono::high_resolution_clock::now();
    // 可以在这里更新延迟统计
    
    return 0;
}

// ============================================================================
// 并行帧处理
// ============================================================================

int surgical_process_frames_parallel(const uint8_t** inputs,
                                   uint8_t** outputs,
                                   int count,
                                   int width, int height,
                                   const EnhancementParams* params) {
    if (!inputs || !outputs || count <= 0) return -1;
    
    std::vector<std::future<int>> futures;
    
    // 并行处理
    for (int i = 0; i < count; i++) {
        futures.push_back(std::async(std::launch::async,
            [inputs, outputs, i, width, height, params]() {
                return surgical_process_frame_optimized(
                    inputs[i], width, height, params, outputs[i]);
            }));
    }
    
    int success_count = 0;
    for (auto& f : futures) {
        if (f.get() == 0) success_count++;
    }
    
    return success_count;
}

// ============================================================================
// 性能统计
// ============================================================================

void surgical_get_optimization_stats(uint64_t* total_pixels,
                                    uint64_t* simd_pixels,
                                    const char** backend_name) {
    if (!g_context) return;
    
    if (total_pixels) *total_pixels = g_context->total_pixels_processed;
    if (simd_pixels) *simd_pixels = g_context->simd_accelerated_pixels;
    if (backend_name) *backend_name = g_context->simd_backend_name;
}

// ============================================================================
// 清理
// ============================================================================

void surgical_cleanup_optimization() {
    if (g_context) {
        if (g_context->frame_pool) {
            mem_pool_destroy(g_context->frame_pool);
        }
        delete g_context;
        g_context = nullptr;
    }
}
