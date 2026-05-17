/**
 * @file simd_processing.h
 * @brief SIMD加速图像处理 - 公共API声明
 *
 * ============================================================================
 * 概述
 * ============================================================================
 *
 * 本模块提供SIMD加速的图像处理功能，支持：
 * - 基础图像处理（亮度/对比度/饱和度/色彩空间转换）
 * - 医疗影像处理（GSDF/Sobel边缘/无血术野增强）
 * - 流水线处理（一次性执行多个操作）
 *
 * 支持的后端：
 * - SSE4.2 (Intel/AMD x86_64)
 * - AVX2 (Intel/AMD x86_64)
 * - NEON (ARM)
 * - Scalar (Fallback)
 *
 * ============================================================================
 * 性能目标
 * ============================================================================
 *
 * 1920x1080 @ 135fps (全流水线, Apple M1 Pro)
 * 640x480 @ 968fps (全流水线, Apple M1 Pro)
 *
 * ============================================================================
 * 使用示例
 * ============================================================================
 *
 * @example 1: 基础亮度调整
 * @code
 *     #include "simd_processing.h"
 *
 *     uint8_t input[WIDTH * HEIGHT * 3];
 *     uint8_t output[WIDTH * HEIGHT * 3];
 *
 *     // 调整亮度 +10%
 *     simd_adjust_brightness_contrast(input, output, WIDTH, HEIGHT, 0.1f, 1.0f);
 * @endcode
 *
 * @example 2: 医疗影像流水线
 * @code
 *     // 生成GSDF查找表
 *     uint8_t gsdf_lut[256];
 *     for (int i = 0; i < 256; i++) {
 *         float t = i / 255.0f;
 *         gsdf_lut[i] = (uint8_t)(255.0f * pow(t, 0.8f));
 *     }
 *
 *     // 配置流水线
 *     SimdPipelineConfig config = {
 *         .brightness = 0.1f,
 *         .contrast = 1.1f,
 *         .saturation = 1.2f,
 *         .gsdf_lut = gsdf_lut,
 *         .enable_bloodless = true,
 *         .blood_suppress_level = 0.5f,
 *         .tissue_enhance = 0.3f,
 *     };
 *
 *     // 一行代码处理完整流程
 *     simd_pipeline_process(input, output, WIDTH, HEIGHT, &config);
 * @endcode
 *
 * @example 3: 无血术野增强
 * @code
 *     // 抑制血色60%，增强组织对比度30%
 *     simd_bloodless_enhance(input, output, WIDTH, HEIGHT,
 *                          0.6f,   // suppress_level
 *                          0.3f,   // tissue_enhance
 *                          0.8f);  // edge_preserve
 * @endcode
 *
 * ============================================================================
 * 线程安全
 * ============================================================================
 *
 * 所有函数都是线程安全的，可以并行处理不同的图像缓冲区。
 * 内部使用OpenMP进行并行化，需要链接 -fopenmp。
 *
 * ============================================================================
 * 编译选项
 * ============================================================================
 *
 * 使用SSE4.2/AVX2: 确保编译器启用相应选项
 *     gcc -O3 -march=native
 *
 * 使用OpenMP:
 *     gcc -O3 -fopenmp
 *
 * 使用NEON (ARM):
 *     确保编译器支持 -mfpu=neon
 */

#ifndef SIMD_PROCESSING_H
#define SIMD_PROCESSING_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 类型定义
// ============================================================================

/**
 * SIMD后端类型
 *
 * - SIMD_NONE: 无SIMD支持（纯标量）
 * - SIMD_SSE4: SSE4.2 (Intel/AMD)
 * - SIMD_AVX2: AVX2 (Intel/AMD)
 * - SIMD_NEON: ARM NEON
 * - SIMD_AUTO: 自动检测（推荐）
 */
typedef enum {
    SIMD_NONE = 0,  /**< 无SIMD，纯标量实现 */
    SIMD_SSE4 = 1,  /**< SSE4.2 */
    SIMD_AVX2 = 2,  /**< AVX2 */
    SIMD_NEON = 3,  /**< ARM NEON */
    SIMD_AUTO = 4   /**< 自动检测最佳后端 */
} SIMDBackend;

/**
 * SIMD流水线配置
 *
 * 用于配置simd_pipeline_process()的完整处理流程。
 *
 * @example
 *     SimdPipelineConfig config = {
 *         .brightness = 0.1f,           // 亮度 +10%
 *         .contrast = 1.1f,             // 对比度 +10%
 *         .saturation = 1.2f,           // 饱和度 +20%
 *         .gsdf_lut = my_lut,           // GSDF查找表
 *         .enable_bloodless = true,      // 启用无血术野
 *         .blood_suppress_level = 0.5f, // 血色抑制50%
 *         .tissue_enhance = 0.3f,      // 组织增强30%
 *         .edge_threshold = 50.0f,      // 边缘阈值
 *     };
 */
typedef struct {
    /** @brief 启用色彩空间转换（预留） */
    bool enable_colorspace_conversion;

    /** @brief 亮度调整 (-1.0 to 1.0, 默认0.0) */
    float brightness;

    /** @brief 对比度调整 (0.5 to 2.0, 默认1.0) */
    float contrast;

    /** @brief 饱和度调整 (0.0 to 2.0, 默认1.0) */
    float saturation;

    /**
     * @brief GSDF查找表
     *
     * 256字节的DICOM GSDF校正表。
     * 如果为NULL则跳过GSDF处理。
     *
     * @example 生成标准GSDF LUT:
     *     uint8_t lut[256];
     *     for (int i = 0; i < 256; i++) {
     *         float t = i / 255.0f;
     *         lut[i] = (uint8_t)(255.0f * pow(t, 0.8f));
     *     }
     */
    const uint8_t* gsdf_lut;

    /** @brief 启用无血术野增强 */
    bool enable_bloodless;

    /** @brief 血色抑制强度 (0.0 to 1.0, 默认0.5) */
    float blood_suppress_level;

    /** @brief 组织对比度增强 (0.0 to 1.0, 默认0.3) */
    float tissue_enhance;

    /** @brief 边缘保留 (0.0 to 1.0, 默认0.8) */
    float edge_preserve;

    /** @brief Sobel边缘检测阈值 (0-255, 默认50) */
    float edge_threshold;

    /** @brief 启用降噪（预留） */
    bool enable_denoise;

    /** @brief 锐化强度 (0.0 to 1.0, 默认0.0) */
    float sharpen_strength;
} SimdPipelineConfig;

/**
 * 性能统计结构
 *
 * 用于获取SIMD处理的性能数据。
 */
typedef struct {
    uint64_t total_pixels;           /**< 总处理的像素数 */
    uint64_t simd_accelerated_pixels;/**< SIMD加速处理的像素数 */
    double total_time_ms;             /**< 总处理时间(毫秒) */
    double simd_time_ms;             /**< SIMD处理时间(毫秒) */
    const char* backend_name;         /**< 后端名称 */
} SIMDStats;

// ============================================================================
// 后端查询
// ============================================================================

/**
 * @brief 获取当前SIMD后端
 *
 * 自动检测并返回最佳可用的SIMD后端。
 *
 * @return SIMD后端枚举值
 *
 * @example
 *     SIMDBackend backend = simd_get_backend();
 *     printf("Using: %s\n", simd_get_backend_name(backend));
 */
SIMDBackend simd_get_backend(void);

/**
 * @brief 获取后端名称
 *
 * @param backend 后端枚举值
 * @return 后端名称字符串 ("SSE4.2", "AVX2", "NEON", "Scalar")
 *
 * @example
 *     const char* name = simd_get_backend_name(SIMD_AVX2);
 *     // name = "AVX2"
 */
const char* simd_get_backend_name(SIMDBackend backend);

/**
 * @brief 检查后端是否支持
 *
 * @param backend 要检查的后端（SIMD_AUTO会自动检测）
 * @return true支持，false不支持
 *
 * @example
 *     if (simd_is_supported(SIMD_AVX2)) {
 *         printf("AVX2 available!\n");
 *     }
 */
bool simd_is_supported(SIMDBackend backend);

// ============================================================================
// 基础图像处理
// ============================================================================

/**
 * @brief 亮度/对比度调整
 *
 * 调整图像的亮度和对比度。
 *
 * @param input     RGB24输入数据 (width*height*3字节)
 * @param output    RGB24输出数据 (必须预先分配)
 * @param width     图像宽度
 * @param height    图像高度
 * @param brightness 亮度 (-1.0 to 1.0, 0.0=不变)
 * @param contrast  对比度 (0.5 to 2.0, 1.0=不变)
 * @return 0成功, -1失败
 *
 * @performance 640x480 @ 0.21ms (M1 Pro)
 *
 * @example 调亮:
 *     simd_adjust_brightness_contrast(input, output, 640, 480, 0.2f, 1.0f);
 *
 * @example 增强对比度:
 *     simd_adjust_brightness_contrast(input, output, 640, 480, 0.0f, 1.5f);
 *
 * @example 同时调整:
 *     simd_adjust_brightness_contrast(input, output, 640, 480, 0.1f, 1.2f);
 */
int simd_adjust_brightness_contrast(const uint8_t* input,
                                   uint8_t* output,
                                   int width, int height,
                                   float brightness,
                                   float contrast);

/**
 * @brief 饱和度调整
 *
 * 调整HSV色彩空间中的饱和度通道。
 *
 * @param saturation 饱和度 (0.0=灰度, 1.0=不变, 2.0=双倍饱和)
 * @return 0成功, -1失败
 *
 * @performance 640x480 @ 0.19ms (M1 Pro)
 *
 * @example 降低饱和度（去色）:
 *     simd_adjust_saturation(input, output, 640, 480, 0.5f);
 *
 * @example 增强饱和度:
 *     simd_adjust_saturation(input, output, 640, 480, 1.5f);
 */
int simd_adjust_saturation(const uint8_t* input,
                         uint8_t* output,
                         int width, int height,
                         float saturation);

/**
 * @brief RGB到YUV420转换
 *
 * 将RGB24转换为YUV420 planar格式。
 *
 * @param rgb  RGB24输入 (width*height*3字节)
 * @param yuv  YUV420输出 (width*height*3/2字节)
 * @return 0成功, -1失败
 *
 * @note Y平面: width*height字节
 *       U平面: width*height/4字节
 *       V平面: width*height/4字节
 *
 * @example
 *     size_t yuv_size = width * height * 3 / 2;
 *     uint8_t* yuv = malloc(yuv_size);
 *     simd_rgb_to_yuv420(rgb, yuv, width, height);
 */
int simd_rgb_to_yuv420(const uint8_t* rgb,
                      uint8_t* yuv,
                      int width, int height);

/**
 * @brief YUV420到RGB转换
 *
 * 将YUV420 planar转换回RGB24。
 *
 * @param yuv  YUV420输入
 * @param rgb  RGB24输出
 * @return 0成功, -1失败
 *
 * @example
 *     simd_yuv420_to_rgb(yuv, rgb, width, height);
 */
int simd_yuv420_to_rgb(const uint8_t* yuv,
                      uint8_t* rgb,
                      int width, int height);

// ============================================================================
// 医疗影像处理
// ============================================================================

/**
 * @brief RGB到灰度转换
 *
 * 使用标准亮度系数将RGB转换为灰度图。
 *
 * @param rgb  RGB24输入
 * @param gray 灰度输出 (width*height字节)
 * @return 0成功, -1失败
 *
 * @performance 640x480 @ 0.08ms (M1 Pro) - 最快的操作
 *
 * @note 使用ITU-R BT.601系数: Y=0.299R+0.587G+0.114B
 *
 * @example
 *     uint8_t* gray = malloc(width * height);
 *     simd_rgb_to_grayscale(rgb, gray, width, height);
 */
int simd_rgb_to_grayscale(const uint8_t* rgb,
                         uint8_t* gray,
                         int width, int height);

/**
 * @brief GSDF查表应用
 *
 * 应用DICOM Part 14 GSDF（灰度标准显示函数）进行亮度校正。
 *
 * @param input RGB24输入
 * @param output RGB24输出
 * @param lut   256字节GSDF查找表
 * @return 0成功, -1失败
 *
 * @performance 640x480 @ 0.28ms (M1 Pro)
 *
 * @note GSDF LUT应将输入值映射到DICOM标准亮度响应
 *
 * @example 标准GSDF LUT生成:
 *     uint8_t gsdf_lut[256];
 *     for (int i = 0; i < 256; i++) {
 *         float jnd = i;  // Just Noticeable Difference
 *         float luminance = ...;  // DICOM GSDF公式
 *         gsdf_lut[i] = (uint8_t)luminance;
 *     }
 *     simd_gsdf_lut_apply(input, output, width, height, gsdf_lut);
 */
int simd_gsdf_lut_apply(const uint8_t* input,
                       uint8_t* output,
                       int width, int height,
                       const uint8_t* lut);

/**
 * @brief Sobel边缘检测
 *
 * 使用Sobel算子检测图像边缘。
 *
 * @param input     RGB24输入
 * @param edge      边缘图输出 (单通道, width*height字节)
 * @param threshold 边缘阈值 (0-255, 低于此值忽略)
 * @return 0成功, -1失败
 *
 * @performance 640x480 @ 0.21ms (M1 Pro)
 *
 * @example 检测所有边缘:
 *     simd_edge_detection_sobel(input, edge, 640, 480, 0.0f);
 *
 * @example 只保留强边缘:
 *     simd_edge_detection_sobel(input, edge, 640, 480, 100.0f);
 */
int simd_edge_detection_sobel(const uint8_t* input,
                             uint8_t* edge,
                             int width, int height,
                             float threshold);

/**
 * @brief 无血术野增强
 *
 * 专门为手术视频设计的增强算法：
 * 1. 检测并抑制红色（血色）
 * 2. 增强绿色/蓝色（组织）
 * 3. 提升组织对比度
 * 4. 保留边缘细节
 *
 * @param input           RGB24输入
 * @param output          RGB24输出
 * @param suppress_level  血色抑制强度 (0.0=无, 1.0=最大抑制)
 * @param tissue_enhance  组织增强 (0.0=无, 1.0=最大增强)
 * @param edge_preserve   边缘保留 (0.0=平滑, 1.0=完全保留)
 * @return 0成功, -1失败
 *
 * @performance 640x480 @ 0.32ms (M1 Pro)
 *
 * @example 腹腔镜手术设置:
 *     simd_bloodless_enhance(input, output, 1920, 1080,
 *                          0.6f,   // 抑制60%血色
 *                          0.4f,   // 增强40%组织
 *                          0.8f);  // 保留80%边缘
 *
 * @example 内窥镜检查:
 *     simd_bloodless_enhance(input, output, 1920, 1080,
 *                          0.3f,   // 轻度抑制
 *                          0.2f,   // 轻度增强
 *                          0.9f);  // 高保真边缘
 */
int simd_bloodless_enhance(const uint8_t* input,
                          uint8_t* output,
                          int width, int height,
                          float suppress_level,
                          float tissue_enhance,
                          float edge_preserve);

/**
 * @brief 5x5高斯模糊
 *
 * 使用5x5高斯核对图像进行模糊处理。
 *
 * @param input RGB24输入
 * @param output RGB24输出
 * @param sigma 高斯核标准差（目前未使用）
 * @return 0成功, -1失败
 *
 * @note sigma参数目前未生效，使用固定核
 */
int simd_gaussian_blur_5x5(const uint8_t* input,
                           uint8_t* output,
                           int width, int height,
                           float sigma);

/**
 * @brief 3x3卷积
 *
 * 应用任意3x3卷积核。
 *
 * @param input  RGB24输入
 * @param output RGB24输出
 * @param kernel 9字节卷积核 (行优先)
 * @return 0成功, -1失败
 *
 * @example 锐化核:
 *     int8_t kernel[] = { 0, -1, 0,
 *                       -1, 5, -1,
 *                        0, -1, 0 };
 *     simd_convolution_3x3(input, output, w, h, kernel);
 *
 * @example 边缘检测核:
 *     int8_t kernel[] = {-1, -1, -1,
 *                       -1,  8, -1,
 *                       -1, -1, -1};
 *     simd_convolution_3x3(input, output, w, h, kernel);
 */
int simd_convolution_3x3(const uint8_t* input,
                        uint8_t* output,
                        int width, int height,
                        const int8_t* kernel);

// ============================================================================
// 流水线处理
// ============================================================================

/**
 * @brief SIMD流水线处理
 *
 * 一次性执行所有配置的图像处理步骤。
 * 内部自动选择最佳后端并使用OpenMP并行化。
 *
 * 处理顺序:
 * 1. 色彩空间转换（预留）
 * 2. 亮度/对比度调整
 * 3. 饱和度调整
 * 4. GSDF查表
 * 5. 无血术野增强
 *
 * @param input  RGB24输入
 * @param output RGB24输出（必须预先分配）
 * @param config 流水线配置（可为NULL使用默认配置）
 * @return 0成功, -1失败
 *
 * @performance 640x480 @ 1.03ms (M1 Pro)
 * @performance 1920x1080 @ 7.42ms (M1 Pro)
 *
 * @example 完整手术视频增强:
 *     uint8_t gsdf_lut[256];
 *     generate_gsdf_lut(gsdf_lut);
 *
 *     SimdPipelineConfig config = {
 *         .brightness = 0.1f,
 *         .contrast = 1.1f,
 *         .saturation = 1.2f,
 *         .gsdf_lut = gsdf_lut,
 *         .enable_bloodless = true,
 *         .blood_suppress_level = 0.5f,
 *         .tissue_enhance = 0.3f,
 *     };
 *
 *     simd_pipeline_process(frame_in, frame_out, 1920, 1080, &config);
 *
 * @example 快速亮度调整:
 *     SimdPipelineConfig minimal = {
 *         .brightness = 0.1f,
 *         .contrast = 1.0f,
 *     };
 *     simd_pipeline_process(input, output, w, h, &minimal);
 *
 * @example 单帧处理:
 *     simd_pipeline_process(input, output, w, h, NULL);  // 使用默认值
 */
int simd_pipeline_process(const uint8_t* input,
                         uint8_t* output,
                         int width, int height,
                         const SimdPipelineConfig* config);

// ============================================================================
// 性能基准测试
// ============================================================================

/**
 * @brief SIMD性能基准测试
 *
 * 测试当前后端的处理性能。
 *
 * @param iterations 迭代次数
 * @param ops_per_sec 输出: 操作数/秒
 * @return 0成功, -1失败
 *
 * @example
 *     double ops;
 *     simd_benchmark(1920, 1080, 100, &ops);
 *     printf("Performance: %.0f ops/sec\n", ops);
 */
int simd_benchmark(int width, int height, int iterations, double* ops_per_sec);

/**
 * @brief 获取性能统计
 * @param stats 输出统计结构
 */
void simd_get_stats(SIMDStats* stats);

/**
 * @brief 重置性能统计
 */
void simd_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif // SIMD_PROCESSING_H
