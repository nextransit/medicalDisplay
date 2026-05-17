/**
 * @file volume_renderer.h
 * @brief 3D体绘制GPU加速接口
 * 
 * 用于PET-CT/PET-MR等多模态融合的实时3D渲染
 * 支持: Ray Casting, Maximum Intensity Projection, Composite
 */

#ifndef VOLUME_RENDERER_H
#define VOLUME_RENDERER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 渲染模式
// ============================================================================
typedef enum {
    VOLUME_MODE_RAY_CASTING = 0,   // 光线投射 (医学可视化标准)
    VOLUME_MODE_MIP,               // 最大密度投影
    VOLUME_MODE_COMPOSITE,         //  alpha合成
    VOLUME_MODE_SURFACE,           // 表面渲染
    VOLUME_MODE_COUNT
} VolumeRenderMode;

// ============================================================================
// 传输函数类型
// ============================================================================
typedef enum {
    TRANSFER_CT_BONE = 0,          // CT骨骼
    TRANSFER_CT_SOFT_TISSUE,       // CT软组织
    TRANSFER_CT_LUNG,              // CT肺
    TRANSFER_CT_VESSEL,            // CT血管
    TRANSFER_PET_METABOLIC,        // PET代谢
    TRANSFER_PET_TUMOR,            // PET肿瘤
    TRANSFER_FUSED_CT_PET,         // CT-PET融合
    TRANSFER_CUSTOM                 // 自定义
} TransferFunctionType;

// ============================================================================
// 渲染配置
// ============================================================================
typedef struct {
    VolumeRenderMode mode;
    TransferFunctionType transfer_type;
    
    // 窗口/级别
    float window_center;
    float window_width;
    
    // 光照
    float light_direction[3];
    float ambient_strength;
    float diffuse_strength;
    float specular_strength;
    float shininess;
    
    // 步进
    float step_size;
    int max_steps;
    
    // 混合
    float opacity_scale;
    bool enable_shading;
    bool enable_gradient;
    
    // 颜色映射
    float colormap_low[3];
    float colormap_high[3];
} VolumeRenderConfig;

// ============================================================================
// 体数据
// ============================================================================
typedef struct {
    // 数据指针 (3D体素数据)
    const void* data;
    
    // 维度
    int width;
    int height;
    int depth;
    
    // 体素间距 (mm)
    float spacing_x;
    float spacing_y;
    float spacing_z;
    
    // 数据范围
    float min_value;
    float max_value;
    
    // 数据类型
    int bytes_per_voxel;  // 1, 2, or 4
    bool is_float;        // true = float, false = unsigned
    
    // 第二通道 (如PET代谢数据)
    const void* secondary_data;
    float secondary_scale;
} VolumeData;

// ============================================================================
// 体渲染器句柄
// ============================================================================
typedef struct VolumeRenderer VolumeRenderer;

// ============================================================================
// 生命周期
// ============================================================================

/**
 * 创建体渲染器
 * @param backend "vulkan", "metal", "cpu"
 * @return 句柄
 */
VolumeRenderer* volume_renderer_create(const char* backend);

/**
 * 销毁体渲染器
 */
void volume_renderer_destroy(VolumeRenderer* renderer);

/**
 * 设置体数据
 */
int volume_renderer_set_data(VolumeRenderer* renderer, const VolumeData* data);

/**
 * 设置第二通道数据 (如PET)
 */
int volume_renderer_set_secondary_data(VolumeRenderer* renderer, const void* data);

/**
 * 设置渲染配置
 */
int volume_renderer_set_config(VolumeRenderer* renderer, const VolumeRenderConfig* config);

// ============================================================================
// 渲染
// ============================================================================

/**
 * 渲染单帧
 * @param renderer 体渲染器
 * @param output 输出缓冲区 (RGBA)
 * @param output_width 输出宽度
 * @param output_height 输出高度
 * @param view_matrix 4x4视图矩阵 (列主序)
 * @return 0成功
 */
int volume_renderer_render(VolumeRenderer* renderer,
                          uint8_t* output,
                          int output_width,
                          int output_height,
                          const float* view_matrix);

/**
 * 渲染到纹理 (GPU到GPU，避免CPU拷贝)
 */
int volume_renderer_render_to_texture(VolumeRenderer* renderer,
                                     void* output_texture,
                                     int output_width,
                                     int output_height,
                                     const float* view_matrix);

// ============================================================================
// 传输函数
// ============================================================================

/**
 * 设置预定义的传输函数
 */
int volume_renderer_set_transfer_function(VolumeRenderer* renderer,
                                        TransferFunctionType type);

/**
 * 设置自定义传输函数
 * @param num_points 传输函数控制点数
 * @param points 控制点数组 [value, r, g, b, alpha]
 */
int volume_renderer_set_custom_transfer(VolumeRenderer* renderer,
                                       int num_points,
                                       const float* points);

// ============================================================================
// 相机控制
// ============================================================================

/**
 * 设置相机位置
 */
int volume_renderer_set_camera(VolumeRenderer* renderer,
                              float pos_x, float pos_y, float pos_z,
                              float look_at_x, float look_at_y, float look_at_z,
                              float up_x, float up_y, float up_z);

/**
 * 正投影
 */
int volume_renderer_set_orthographic(VolumeRenderer* renderer,
                                    float left, float right,
                                    float bottom, float top,
                                    float near, float far);

/**
 * 透视投影
 */
int volume_renderer_set_perspective(VolumeRenderer* renderer,
                                   float fov_degrees,
                                   float aspect,
                                   float near, float far);

// ============================================================================
// 性能统计
// ============================================================================

typedef struct {
    uint64_t total_frames;
    double avg_render_time_ms;
    double min_render_time_ms;
    double max_render_time_ms;
    int gpu_used;
    const char* backend_name;
} VolumeRenderStats;

void volume_renderer_get_stats(VolumeRenderer* renderer, VolumeRenderStats* stats);

// ============================================================================
// GPU后端查询
// ============================================================================

/**
 * 获取可用的渲染后端
 * @param backends 输出后端列表
 * @param max_backends 最大数量
 * @return 实际数量
 */
int volume_renderer_get_available_backends(char** backends, int max_backends);

#ifdef __cplusplus
}
#endif

#endif // VOLUME_RENDERER_H
