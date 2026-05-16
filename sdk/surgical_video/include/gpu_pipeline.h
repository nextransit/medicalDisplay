/**
 * @file gpu_pipeline.h
 * @brief 术野视频 GPU 加速 Compute Pipeline 抽象层
 *
 * 目标: 将术野视频增强从 CPU 迁移到 GPU (Vulkan/OpenGL)
 * 性能目标: 1920x1080 @ <20ms/frame
 *
 * 支持的后端:
 * - VULKAN_COMPUTE: Vulkan Compute Shader (Linux/Android 首选)
 * - OPENGL_COMPUTE: OpenGL 4.3+ Compute Shader (跨平台fallback)
 * - CPU_FALLBACK: 纯 CPU 实现 (无 GPU 时)
 */

#ifndef GPU_PIPELINE_H
#define GPU_PIPELINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// GPU 后端类型
// ============================================================================
typedef enum {
    GPU_BACKEND_NONE = 0,
    GPU_BACKEND_VULKAN,       // Vulkan Compute
    GPU_BACKEND_OPENGL,       // OpenGL 4.3 Compute
    GPU_BACKEND_METAL,        // Metal Compute (macOS/iOS)
    GPU_BACKEND_COUNT
} GPUBackendType;

// ============================================================================
// GPU Pipeline 配置
// ============================================================================
typedef struct {
    GPUBackendType backend;      // GPU 后端
    uint32_t max_width;          // 最大帧宽
    uint32_t max_height;         // 最大帧高
    bool enable_hdr;             // 是否启用 HDR 管线
    bool enable_async;           // 是否启用异步计算
    uint32_t pipeline_depth;     // 管线深度 (帧缓冲数)
} GPUPipelineConfig;

// ============================================================================
// 增强参数 (对齐 shader uniform)
// ============================================================================
typedef struct {
    float brightness;            // 亮度 (-1.0 to 1.0)
    float contrast;              // 对比度 (0.5 to 2.0)
    float saturation;            // 饱和度 (0.0 to 2.0)
    float sharpness;             // 锐化 (0.0 to 1.0)
    float edge_strength;         // 边缘增强强度 (0.0 to 1.0)
    float edge_threshold;        // 边缘检测阈值 (0.0 to 1.0)
    float bloodless_strength;    // 无血术野强度 (0.0 to 1.0)
    uint32_t enable_bloodless;   // 是否启用无血术野
    uint32_t enable_pseudo_color;// 是否启用伪彩色
    uint32_t enable_hdr;         // 是否启用 HDR
    uint32_t mode;               // EnhancementMode (0-3)
    uint32_t frame_width;        // 帧宽度
    uint32_t frame_height;       // 帧高度
} GPUPipelineParams;

// ============================================================================
// GPU Pipeline 句柄 (opaque)
// ============================================================================
typedef struct GPUPipeline GPUPipeline;

// ============================================================================
// 生命周期
// ============================================================================

/**
 * 创建 GPU Pipeline
 * @param config Pipeline 配置
 * @return Pipeline 句柄，失败返回 NULL
 */
GPUPipeline* gpu_pipeline_create(const GPUPipelineConfig* config);

/**
 * 销毁 GPU Pipeline
 * @param pipeline Pipeline 句柄
 */
void gpu_pipeline_destroy(GPUPipeline* pipeline);

// ============================================================================
// 帧处理
// ============================================================================

/**
 * 处理单帧 (RGBA8 输入，RGBA8 输出)
 * @param pipeline Pipeline 句柄
 * @param input_rgba 输入帧 (4 * width * height 字节)
 * @param output_rgba 输出帧 (4 * width * height 字节)
 * @param width 帧宽度
 * @param height 帧高度
 * @param params 增强参数
 * @return 0 成功，-1 失败
 */
int gpu_pipeline_process_frame(GPUPipeline* pipeline,
                               const uint8_t* input_rgba,
                               uint8_t* output_rgba,
                               uint32_t width,
                               uint32_t height,
                               const GPUPipelineParams* params);

/**
 * 处理单帧 (YUV 输入，YUV 输出 — 带转换)
 * @param pipeline Pipeline 句柄
 * @param input_yuv 输入帧 (3/2 * width * height 字节，取决于格式)
 * @param output_yuv 输出帧
 * @param width 帧宽度
 * @param height 帧高度
 * @param yuv_format 0=YUV444, 1=YUV422, 2=YUV420
 * @param params 增强参数
 * @return 0 成功，-1 失败
 */
int gpu_pipeline_process_frame_yuv(GPUPipeline* pipeline,
                                   const uint8_t* input_yuv,
                                   uint8_t* output_yuv,
                                   uint32_t width,
                                   uint32_t height,
                                   int yuv_format,
                                   const GPUPipelineParams* params);

// ============================================================================
// 性能查询
// ============================================================================

/**
 * 获取 GPU 管线延迟 (微秒)
 * @param pipeline Pipeline 句柄
 * @return 最后帧延迟 (微秒)，-1 失败
 */
int64_t gpu_pipeline_get_last_latency_us(GPUPipeline* pipeline);

/**
 * 获取 GPU 硬件信息
 * @param pipeline Pipeline 句柄
 * @param gpu_name 输出 GPU 名称
 * @param max_size 缓冲区大小
 * @return 0 成功
 */
int gpu_pipeline_get_device_info(GPUPipeline* pipeline,
                                 char* gpu_name,
                                 size_t max_size);

/**
 * 获取可用的 GPU 后端类型
 * @param pipeline Pipeline 句柄
 * @return 后端类型
 */
GPUBackendType gpu_pipeline_get_backend(GPUPipeline* pipeline);

/**
 * 检查 GPU 是否可用
 * @param preferred_backend 首选后端
 * @return true 可用
 */
bool gpu_pipeline_is_available(GPUBackendType preferred_backend);

#ifdef __cplusplus
}
#endif

#endif // GPU_PIPELINE_H
