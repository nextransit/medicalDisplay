#ifndef MULTIMODAL_FUSION_H
#define MULTIMODAL_FUSION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 融合模态类型
// ============================================================================
typedef enum {
    FUSION_UNKNOWN = 0,
    FUSION_PET_CT,          // PET-CT融合
    FUSION_PET_MR,          // PET-MR融合
    FUSION_US_CT,           // 超声-CT融合导航
    FUSION_US_MR,           // 超声-MR融合导航
    FUSION_SPECT_CT,        // SPECT-CT融合
    FUSION_COUNT
} FusionModalityType;

// ============================================================================
// 融合对齐参数
// ============================================================================
typedef struct {
    float translation_x;    // X方向平移 (mm)
    float translation_y;    // Y方向平移 (mm)
    float translation_z;    // Z方向平移 (mm)
    float rotation_x;       // X轴旋转 (度)
    float rotation_y;       // Y轴旋转 (度)
    float rotation_z;       // Z轴旋转 (度)
    float scale;            // 缩放因子
    float registration_quality; // 配准质量 (0.0-1.0)
} FusionAlignmentParams;

// ============================================================================
// 融合结果显示
// ============================================================================
typedef struct {
    FusionModalityType fusion_type;   // 融合类型
    float fusion_ratio;               // 融合比例 (0.0=PET only, 1.0=CT/MR only)
    float confidence;                  // 置信度
    FusionAlignmentParams alignment;   // 对齐参数
    int overlay_enabled;              // 是否启用叠加显示
    int color_map;                     // 调色板 (0=HotMetal, 1=Rainbow, 2=Jet)
    float opacity;                     // 不透明度 (0.0-1.0)
    int window_center;                 // CT/MR窗口中心
    int window_width;                  // CT/MR窗口宽度
    int pet_window_center;             // PET窗口中心 (SUV)
    int pet_window_width;              // PET窗口宽度 (SUV)
} FusionDisplayConfig;

// ============================================================================
// 多模态融合引擎句柄
// ============================================================================
typedef struct MultimodalFusionEngine MultimodalFusionEngine;

// ============================================================================
// 引擎生命周期
// ============================================================================

MultimodalFusionEngine* fusion_engine_create(bool use_gpu);
void fusion_engine_destroy(MultimodalFusionEngine* engine);

int fusion_set_anatomical_volume(MultimodalFusionEngine* engine,
                                const uint16_t* ct_data,
                                int width, int height, int depth,
                                float spacing_x, float spacing_y, float spacing_z);

int fusion_set_functional_volume(MultimodalFusionEngine* engine,
                                const float* pet_data,
                                int width, int height, int depth,
                                float spacing_x, float spacing_y, float spacing_z,
                                float suv_scale);

int fusion_auto_register(MultimodalFusionEngine* engine,
                        FusionAlignmentParams* alignment);

int fusion_set_alignment(MultimodalFusionEngine* engine,
                         const FusionAlignmentParams* alignment);

int fusion_render_slice(MultimodalFusionEngine* engine,
                        int slice_index,
                        uint8_t* output,
                        int output_width, int output_height,
                        const FusionDisplayConfig* display_config);

int fusion_render_3d(MultimodalFusionEngine* engine,
                     uint8_t* output,
                     int output_width, int output_height,
                     const float* view_matrix,
                     const FusionDisplayConfig* display_config);

int fusion_detect_hotspots(MultimodalFusionEngine* engine,
                           float* hotspots,
                           int max_hotspots,
                           float threshold);

void fusion_get_default_config(FusionModalityType fusion_type,
                               FusionDisplayConfig* display_config);

#ifdef __cplusplus
}
#endif

#endif // MULTIMODAL_FUSION_H
