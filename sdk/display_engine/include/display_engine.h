#ifndef DISPLAY_ENGINE_H
#define DISPLAY_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "ai_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 色彩空间定义
// ============================================================================
typedef enum {
    COLOR_SPACE_sRGB = 0,
    COLOR_SPACE_DCI_P3,
    COLOR_SPACE_Rec709,
    COLOR_SPACE_Rec2020,
    COLOR_SPACE_AdobeRGB,
    COLOR_SPACE_DICOM_GSDF,    // 医疗灰阶
    COLOR_SPACE_NATIVE,
    COLOR_SPACE_COUNT
} ColorSpace;

// ============================================================================
// HDR模式
// ============================================================================
typedef enum {
    HDR_MODE_OFF = 0,
    HDR_MODE_HDR10,
    HDR_MODE_HLG,
    HDR_MODE_DolbyVision,
    HDR_MODE_LOCAL_Dimming,    // 本地调光HDR
    HDR_MODE_COUNT
} HDRMode;

// ============================================================================
// LUT配置
// ============================================================================
typedef struct {
    int     bit_depth;         // LUT位深 (8, 10, 12)
    int     size;              // LUT大小
    float*  data;              // LUT数据
    bool    gsdf_enabled;      // 是否启用GSDF
} LUTConfig;

// ============================================================================
// 显示引擎配置
// ============================================================================
typedef struct {
    // 显示硬件
    int             display_id;            // 显示设备ID
    int             width;                  // 显示分辨率
    int             height;
    int             color_depth;            // 位深 (8, 10, 12)
    
    // 渲染配置
    bool            use_vulkan;             // 使用Vulkan (否则OpenGL)
    bool            use_gpu;                // GPU加速
    int             max_fps;                // 最大帧率
    
    // LUT配置
    LUTConfig       gsdf_lut;               // GSDF查找表
    LUTConfig       gamma_lut;              // Gamma查找表
    LUTConfig       color_space_lut;        // 色彩空间转换LUT
    
    // 渲染参数
    float           default_gamma;
    ColorSpace      default_color_space;
    HDRMode         default_hdr_mode;
    
    // Vulkan特定
    struct {
        int         vulkan_device_id;
        uint32_t    queue_family_index;
        uint32_t    graphics_queue_index;
        uint32_t    present_queue_index;
    } vulkan;
} DisplayEngineConfig;

// ============================================================================
// 显示引擎句柄
// ============================================================================
typedef struct DisplayEngine DisplayEngine;

// ============================================================================
// 显示引擎生命周期
// ============================================================================

/**
 * 创建显示引擎
 * @param config 配置
 * @return 引擎句柄
 */
DisplayEngine* display_engine_create(const DisplayEngineConfig* config);

/**
 * 销毁显示引擎
 * @param engine 引擎句柄
 */
void display_engine_destroy(DisplayEngine* engine);

/**
 * 重置为默认状态
 * @param engine 引擎句柄
 */
void display_engine_reset(DisplayEngine* engine);

// ============================================================================
// 显示参数应用
// ============================================================================

/**
 * 应用显示策略 (AI推荐或手动)
 * @param engine 引擎句柄
 * @param strategy 显示策略
 * @return 0成功
 */
int display_engine_apply_strategy(DisplayEngine* engine, const DisplayStrategy* strategy);

/**
 * 切换GSDF模式
 * @param engine 引擎句柄
 * @param enabled 是否启用
 * @param profile_name GSDF配置文件 (NULL=使用默认)
 * @return 0成功
 */
int display_engine_set_gsdf(DisplayEngine* engine, bool enabled, const char* profile_name);

/**
 * 设置窗口/层级 (CT/MRI)
 * @param engine 引擎句柄
 * @param center 窗口中心
 * @param width 窗口宽度
 * @return 0成功
 */
int display_engine_set_window_level(DisplayEngine* engine, float center, float width);

/**
 * 应用3D LUT (色彩空间转换)
 * @param engine 引擎句柄
 * @param lut 3D LUT数据 (33x33x33)
 * @param size LUT大小
 * @return 0成功
 */
int display_engine_apply_3d_lut(DisplayEngine* engine, const float* lut, int size);

// ============================================================================
// 渲染接口
// ============================================================================

/**
 * 渲染帧
 * @param engine 引擎句柄
 * @param frame_data 输入帧数据
 * @param width 帧宽度
 * @param height 帧高度
 * @param format 输入格式 (0=RGB, 1=RGBA, 2=YUV420, 3=YUV422, 4=YUV444)
 * @return 0成功
 */
int display_engine_render_frame(DisplayEngine* engine,
                                const uint8_t* frame_data,
                                int width, int height,
                                int format);

/**
 * 渲染DICOM图像
 * @param engine 引擎句柄
 * @param pixel_data DICOM像素数据
 * @param width 宽度
 * @param height 高度
 * @param bits_stored 位深
 * @param window_center 窗口中心 (可为0使用默认)
 * @param window_width 窗口宽度 (可为0使用默认)
 * @return 0成功
 */
int display_engine_render_dicom(DisplayEngine* engine,
                                const uint16_t* pixel_data,
                                int width, int height,
                                int bits_stored,
                                float window_center,
                                float window_width);

// ============================================================================
// 多屏管理
// ============================================================================

/**
 * 获取可用显示设备列表
 * @param engine 引擎句柄
 * @param displays 输出数组
 * @param max_count 最大数量
 * @return 实际数量
 */
int display_engine_list_displays(DisplayEngine* engine, int* displays, int max_count);

/**
 * 切换主显示
 * @param engine 引擎句柄
 * @param display_id 新显示ID
 * @return 0成功
 */
int display_engine_set_primary_display(DisplayEngine* engine, int display_id);

/**
 * 设置多屏同步
 * @param engine 引擎句柄
 * @param enabled 是否启用同步
 * @param mode 同步模式 (0=GENLOCK, 1=VSYNC, 2=VRR)
 * @return 0成功
 */
int display_engine_set_sync(DisplayEngine* engine, bool enabled, int mode);

// ============================================================================
// 校准与诊断
// ============================================================================

/**
 * 获取当前校准状态
 * @param engine 引擎句柄
 * @param delta_e 输出色彩偏差
 * @param luminance 输出亮度 (cd/m²)
 * @return 0成功
 */
int display_engine_get_calibration_status(DisplayEngine* engine,
                                           float* delta_e,
                                           float* luminance);

/**
 * 执行自检
 * @param engine 引擎句柄
 * @param test_pattern 测试图案类型 (0=灰阶, 1=色块, 2=锐度)
 * @return 0通过，-1失败
 */
int display_engine_self_test(DisplayEngine* engine, int test_pattern);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_ENGINE_H
