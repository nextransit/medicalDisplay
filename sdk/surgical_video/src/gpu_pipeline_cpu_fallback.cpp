/**
 * @file gpu_pipeline_cpu_fallback.cpp
 * @brief GPU Pipeline CPU Fallback 实现
 *
 * 当目标平台没有 Vulkan/OpenGL 时使用纯 CPU 实现，
 * 保证 API 一致性和向前兼容。
 */

#include "gpu_pipeline.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

// ============================================================================
// 内部数据结构
// ============================================================================

struct GPUPipeline {
    GPUPipelineConfig config;
    uint64_t total_frames;
    int64_t last_latency_us;
};

// ============================================================================
// 生命周期
// ============================================================================

GPUPipeline* gpu_pipeline_create(const GPUPipelineConfig* config) {
    if (!config) return nullptr;
    
    auto* p = new (std::nothrow) GPUPipeline();
    if (!p) return nullptr;
    
    p->config = *config;
    p->total_frames = 0;
    p->last_latency_us = 0;

    // 如果请求的不是 fallback 后端，尝试更好的后端
    // 在无 GPU 环境下，回退到 CPU
    p->config.backend = GPU_BACKEND_NONE;

    return p;
}

void gpu_pipeline_destroy(GPUPipeline* pipeline) {
    delete pipeline;
}

// ============================================================================
// 帧处理 (CPU fallback)
// ============================================================================

static inline uint8_t clamp_u8(float v) {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return static_cast<uint8_t>(v);
}

int gpu_pipeline_process_frame(GPUPipeline* pipeline,
                               const uint8_t* input_rgba,
                               uint8_t* output_rgba,
                               uint32_t width,
                               uint32_t height,
                               const GPUPipelineParams* params) {
    if (!pipeline || !input_rgba || !output_rgba || !params) return -1;

    uint32_t pixel_count = width * height;
    uint32_t bytes = pixel_count * 4;  // RGBA

    // 简单 pass-through + 亮度对比度调整 (CPU)
    float brightness_correction = params->brightness * 64.0f;  // 映射到 8-bit
    float contrast_factor = params->contrast;

    for (uint32_t i = 0; i < pixel_count; i++) {
        uint32_t idx = i * 4;
        float r = input_rgba[idx + 0];
        float g = input_rgba[idx + 1];
        float b = input_rgba[idx + 2];

        // 亮度
        r += brightness_correction;
        g += brightness_correction;
        b += brightness_correction;

        // 对比度 (以 128 为中心)
        r = (r - 128.0f) * contrast_factor + 128.0f;
        g = (g - 128.0f) * contrast_factor + 128.0f;
        b = (b - 128.0f) * contrast_factor + 128.0f;

        // 饱和度
        float gray = 0.299f * r + 0.587f * g + 0.114f * b;
        r = gray + (r - gray) * params->saturation;
        g = gray + (g - gray) * params->saturation;
        b = gray + (b - gray) * params->saturation;

        output_rgba[idx + 0] = clamp_u8(r);
        output_rgba[idx + 1] = clamp_u8(g);
        output_rgba[idx + 2] = clamp_u8(b);
        output_rgba[idx + 3] = input_rgba[idx + 3];  // 保持 alpha
    }

    return 0;
}

int gpu_pipeline_process_frame_yuv(GPUPipeline* pipeline,
                                   const uint8_t* input_yuv,
                                   uint8_t* output_yuv,
                                   uint32_t width,
                                   uint32_t height,
                                   int yuv_format,
                                   const GPUPipelineParams* params) {
    if (!pipeline || !input_yuv || !output_yuv || !params) return -1;

    // YUV 格式: 简单 pass-through (CPU fallback)
    size_t bytes_per_pixel = (yuv_format == 1) ? 2 : 3;  // YUV422=2, YUV444=3
    size_t total_bytes = width * height * bytes_per_pixel;
    std::memcpy(output_yuv, input_yuv, total_bytes);

    return 0;
}

// ============================================================================
// 性能查询
// ============================================================================

int64_t gpu_pipeline_get_last_latency_us(GPUPipeline* pipeline) {
    return pipeline ? pipeline->last_latency_us : -1;
}

int gpu_pipeline_get_device_info(GPUPipeline* pipeline,
                                 char* gpu_name,
                                 size_t max_size) {
    if (!pipeline || !gpu_name || max_size == 0) return -1;
    snprintf(gpu_name, max_size, "CPU Fallback (no GPU available)");
    return 0;
}

GPUBackendType gpu_pipeline_get_backend(GPUPipeline* pipeline) {
    return pipeline ? pipeline->config.backend : GPU_BACKEND_NONE;
}

bool gpu_pipeline_is_available(GPUBackendType preferred_backend) {
    // 在 fallback 中，我们总是返回 GPU_BACKEND_NONE
    (void)preferred_backend;
    return false;
}
