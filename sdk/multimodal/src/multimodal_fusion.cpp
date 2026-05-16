/**
 * @file multimodal_fusion.cpp
 * @brief 多模态影像融合引擎实现 (PET-CT, PET-MR, 超声融合)
 */

#include "multimodal_fusion.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

// ============================================================================
// 内部数据结构
// ============================================================================

struct VolumeData {
    const uint16_t* data_uint16 = nullptr;
    const float* data_float = nullptr;
    int width = 0;
    int height = 0;
    int depth = 0;
    float spacing_x = 1.0f;
    float spacing_y = 1.0f;
    float spacing_z = 1.0f;
    bool is_float = false;
};

struct MultimodalFusionEngine {
    bool use_gpu;
    VolumeData anatomical_volume;  // CT/MR
    VolumeData functional_volume;   // PET/SPECT
    FusionAlignmentParams alignment;
    FusionModalityType fusion_type;
    
    MultimodalFusionEngine(bool gpu) : use_gpu(gpu), fusion_type(FUSION_UNKNOWN) {
        memset(&alignment, 0, sizeof(alignment));
        alignment.scale = 1.0f;
        alignment.registration_quality = 0.0f;
    }
};

// ============================================================================
// 颜色映射函数
// ============================================================================

static uint8_t hotmetal_colormap(float value) {
    // Hot Metal colormap: black -> red -> yellow -> white
    value = std::max(0.0f, std::min(1.0f, value));
    if (value < 0.33f) {
        return static_cast<uint8_t>(value * 3.0f * 255.0f);
    } else if (value < 0.66f) {
        return static_cast<uint8_t>((0.33f + (value - 0.33f) * 1.5f) * 255.0f);
    } else {
        return static_cast<uint8_t>((0.66f + (value - 0.66f) * 1.5f) * 255.0f);
    }
}

static uint8_t rainbow_colormap(float value, int channel) {
    // Rainbow colormap
    value = std::max(0.0f, std::min(1.0f, value));
    float pos = value * 4.0f;
    int segment = static_cast<int>(pos);
    float t = pos - segment;
    
    switch (segment) {
        case 0: return channel == 0 ? 0 : static_cast<uint8_t>(t * 255); // Blue -> Cyan
        case 1: return channel == 1 ? 255 : (channel == 0 ? static_cast<uint8_t>((1.0f - t) * 255) : 0); // Cyan -> Green
        case 2: return channel == 0 ? static_cast<uint8_t>(t * 255) : 255; // Green -> Yellow
        case 3: return channel == 2 ? 0 : 255; // Yellow -> Red
        default: return 255;
    }
}

// ============================================================================
// CT窗口化
// ============================================================================

static float window_ct_value(int16_t hu_value, int window_center, int window_width) {
    float min_val = static_cast<float>(window_center) - window_width / 2.0f;
    float max_val = static_cast<float>(window_center) + window_width / 2.0f;
    
    if (hu_value <= min_val) return 0.0f;
    if (hu_value >= max_val) return 1.0f;
    
    return (hu_value - min_val) / window_width;
}

// ============================================================================
// 引擎生命周期
// ============================================================================

extern "C" {

MultimodalFusionEngine* fusion_engine_create(bool use_gpu) {
    return new (std::nothrow) MultimodalFusionEngine(use_gpu);
}

void fusion_engine_destroy(MultimodalFusionEngine* engine) {
    delete engine;
}

// ============================================================================
// 数据设置
// ============================================================================

int fusion_set_anatomical_volume(MultimodalFusionEngine* engine,
                                const uint16_t* ct_data,
                                int width, int height, int depth,
                                float spacing_x, float spacing_y, float spacing_z) {
    if (!engine || !ct_data || width <= 0 || height <= 0 || depth <= 0) {
        return -1;
    }
    
    engine->anatomical_volume.data_uint16 = ct_data;
    engine->anatomical_volume.width = width;
    engine->anatomical_volume.height = height;
    engine->anatomical_volume.depth = depth;
    engine->anatomical_volume.spacing_x = spacing_x;
    engine->anatomical_volume.spacing_y = spacing_y;
    engine->anatomical_volume.spacing_z = spacing_z;
    engine->anatomical_volume.is_float = false;
    
    return 0;
}

int fusion_set_functional_volume(MultimodalFusionEngine* engine,
                                const float* pet_data,
                                int width, int height, int depth,
                                float spacing_x, float spacing_y, float spacing_z,
                                float suv_scale) {
    if (!engine || !pet_data || width <= 0 || height <= 0 || depth <= 0) {
        return -1;
    }
    
    (void)suv_scale; // Reserved for SUV conversion
    
    engine->functional_volume.data_float = pet_data;
    engine->functional_volume.width = width;
    engine->functional_volume.height = height;
    engine->functional_volume.depth = depth;
    engine->functional_volume.spacing_x = spacing_x;
    engine->functional_volume.spacing_y = spacing_y;
    engine->functional_volume.spacing_z = spacing_z;
    engine->functional_volume.is_float = true;
    
    return 0;
}

// ============================================================================
// 配准
// ============================================================================

int fusion_auto_register(MultimodalFusionEngine* engine,
                        FusionAlignmentParams* alignment) {
    if (!engine || !alignment) {
        return -1;
    }
    
    // Simplified mutual information based registration
    // In production, this would use gradient descent optimization
    
    // Check if both volumes are loaded
    if (!engine->anatomical_volume.data_uint16 && !engine->functional_volume.data_float) {
        return -1;
    }
    
    // Compute scale factor based on voxel sizes
    float anat_voxel = engine->anatomical_volume.spacing_x * engine->anatomical_volume.spacing_y;
    float func_voxel = engine->functional_volume.spacing_x * engine->functional_volume.spacing_y;
    
    float scale = (anat_voxel > 0.0f && func_voxel > 0.0f) ? (func_voxel / anat_voxel) : 1.0f;
    
    // Initialize alignment parameters
    memset(alignment, 0, sizeof(FusionAlignmentParams));
    alignment->scale = scale;
    alignment->registration_quality = 0.75f; // Placeholder quality score
    
    // Copy to engine
    engine->alignment = *alignment;
    
    return 0;
}

int fusion_set_alignment(MultimodalFusionEngine* engine,
                         const FusionAlignmentParams* alignment) {
    if (!engine || !alignment) {
        return -1;
    }
    
    engine->alignment = *alignment;
    return 0;
}

// ============================================================================
// 渲染
// ============================================================================

int fusion_render_slice(MultimodalFusionEngine* engine,
                        int slice_index,
                        uint8_t* output,
                        int output_width, int output_height,
                        const FusionDisplayConfig* display_config) {
    if (!engine || !output || !display_config) {
        return -1;
    }
    
    const VolumeData& anat = engine->anatomical_volume;
    const VolumeData& func = engine->functional_volume;
    
    // Check if we have data
    if (!anat.data_uint16 && !func.data_float) {
        return -1;
    }
    
    int slice_idx = std::max(0, std::min(slice_index, 
                            anat.data_uint16 ? (anat.depth - 1) : (func.depth - 1)));
    
    // Determine slice dimensions
    int slice_w = anat.data_uint16 ? anat.width : func.width;
    int slice_h = anat.data_uint16 ? anat.height : func.height;
    
    // Scale factors
    float scale_x = (float)slice_w / output_width;
    float scale_y = (float)slice_h / output_height;
    
    int wc = display_config->window_center ? display_config->window_center : 40;
    int ww = display_config->window_width ? display_config->window_width : 400;
    
    int pet_wc = display_config->pet_window_center ? display_config->pet_window_center : 2.5f;
    int pet_ww = display_config->pet_window_width ? display_config->pet_window_width : 5.0f;
    
    // Render each pixel
    for (int y = 0; y < output_height; y++) {
        for (int x = 0; x < output_width; x++) {
            int src_x = static_cast<int>(x * scale_x);
            int src_y = static_cast<int>(y * scale_y);
            src_x = std::max(0, std::min(src_x, slice_w - 1));
            src_y = std::max(0, std::min(src_y, slice_h - 1));
            
            int idx = src_y * slice_w + src_x;
            
            // Get anatomical value (CT/MR)
            float anat_val = 0.0f;
            if (anat.data_uint16) {
                int16_t hu = static_cast<int16_t>(anat.data_uint16[slice_idx * slice_w * slice_h + idx]) - 1024;
                anat_val = window_ct_value(hu, wc, ww);
            }
            
            // Get functional value (PET)
            float pet_val = 0.0f;
            if (func.data_float) {
                float suv = func.data_float[slice_idx * slice_w * slice_h + idx];
                pet_val = (suv - pet_wc + pet_ww / 2.0f) / pet_ww;
                pet_val = std::max(0.0f, std::min(1.0f, pet_val));
            }
            
            // Fuse
            float ratio = display_config->fusion_ratio;
            float opacity = display_config->opacity;
            float fused = anat_val * ratio + pet_val * opacity * (1.0f - ratio);
            fused = std::max(0.0f, std::min(1.0f, fused));
            
            // Apply colormap
            int color_map = display_config->color_map;
            if (color_map == 0) {
                // Hot Metal
                uint8_t gray = hotmetal_colormap(fused);
                output[(y * output_width + x) * 3 + 0] = gray;
                output[(y * output_width + x) * 3 + 1] = gray;
                output[(y * output_width + x) * 3 + 2] = gray;
            } else {
                // Rainbow
                output[(y * output_width + x) * 3 + 0] = rainbow_colormap(fused, 0);
                output[(y * output_width + x) * 3 + 1] = rainbow_colormap(fused, 1);
                output[(y * output_width + x) * 3 + 2] = rainbow_colormap(fused, 2);
            }
        }
    }
    
    return 0;
}

int fusion_render_3d(MultimodalFusionEngine* engine,
                     uint8_t* output,
                     int output_width, int output_height,
                     const float* view_matrix,
                     const FusionDisplayConfig* display_config) {
    if (!engine || !output || !view_matrix || !display_config) {
        return -1;
    }
    
    // Simplified volume rendering (ray casting)
    // In production, this would use GPU acceleration
    
    const VolumeData& anat = engine->anatomical_volume;
    const VolumeData& func = engine->functional_volume;
    
    int wc = display_config->window_center ? display_config->window_center : 40;
    int ww = display_config->window_width ? display_config->window_width : 400;
    
    // Ray casting for each pixel
    for (int y = 0; y < output_height; y++) {
        for (int x = 0; x < output_width; x++) {
            float accumulated = 0.0f;
            int samples = 0;
            
            // March through volume (simplified)
            for (int z = 0; z < 64; z++) {
                int vol_x = (x * anat.width) / output_width;
                int vol_y = (y * anat.height) / output_height;
                int vol_z = (z * anat.depth) / 64;
                
                vol_x = std::max(0, std::min(vol_x, anat.width - 1));
                vol_y = std::max(0, std::min(vol_y, anat.height - 1));
                vol_z = std::max(0, std::min(vol_z, anat.depth - 1));
                
                if (anat.data_uint16) {
                    int idx = vol_z * anat.width * anat.height + vol_y * anat.width + vol_x;
                    int16_t hu = static_cast<int16_t>(anat.data_uint16[idx]) - 1024;
                    accumulated += window_ct_value(hu, wc, ww);
                    samples++;
                }
            }
            
            float value = samples > 0 ? (accumulated / samples) : 0.0f;
            value = std::max(0.0f, std::min(1.0f, value));
            
            uint8_t gray = hotmetal_colormap(value);
            output[(y * output_width + x) * 3 + 0] = gray;
            output[(y * output_width + x) * 3 + 1] = gray;
            output[(y * output_width + x) * 3 + 2] = gray;
        }
    }
    
    return 0;
}

// ============================================================================
// 热点检测
// ============================================================================

int fusion_detect_hotspots(MultimodalFusionEngine* engine,
                           float* hotspots,
                           int max_hotspots,
                           float threshold) {
    if (!engine || !hotspots || max_hotspots <= 0) {
        return -1;
    }
    
    const VolumeData& func = engine->functional_volume;
    if (!func.data_float) {
        return 0;
    }
    
    struct Candidate {
        float x, y, z, suv_max, volume;
    };
    std::vector<Candidate> candidates;
    
    int total_voxels = func.width * func.height * func.depth;
    
    // Simple threshold-based detection
    for (int z = 0; z < func.depth; z++) {
        for (int y = 0; y < func.height; y++) {
            for (int x = 0; x < func.width; x++) {
                int idx = z * func.width * func.height + y * func.width + x;
                float suv = func.data_float[idx];
                
                if (suv >= threshold) {
                    // Check if this is a local maximum
                    bool is_max = true;
                    for (int dz = -1; dz <= 1 && is_max; dz++) {
                        for (int dy = -1; dy <= 1 && is_max; dy++) {
                            for (int dx = -1; dx <= 1 && is_max; dx++) {
                                int nx = x + dx, ny = y + dy, nz = z + dz;
                                if (nx >= 0 && nx < func.width && 
                                    ny >= 0 && ny < func.height && 
                                    nz >= 0 && nz < func.depth) {
                                    int nidx = nz * func.width * func.height + ny * func.width + nx;
                                    if (func.data_float[nidx] > suv) {
                                        is_max = false;
                                    }
                                }
                            }
                        }
                    }
                    
                    if (is_max) {
                        candidates.push_back({
                            static_cast<float>(x),
                            static_cast<float>(y),
                            static_cast<float>(z),
                            suv,
                            1.0f
                        });
                    }
                }
            }
        }
    }
    
    // Sort by SUV max and limit
    std::sort(candidates.begin(), candidates.end(), 
              [](const Candidate& a, const Candidate& b) {
                  return a.suv_max > b.suv_max;
              });
    
    int count = std::min(static_cast<int>(candidates.size()), max_hotspots);
    for (int i = 0; i < count; i++) {
        hotspots[i * 5 + 0] = candidates[i].x * func.spacing_x;
        hotspots[i * 5 + 1] = candidates[i].y * func.spacing_y;
        hotspots[i * 5 + 2] = candidates[i].z * func.spacing_z;
        hotspots[i * 5 + 3] = candidates[i].suv_max;
        hotspots[i * 5 + 4] = candidates[i].volume;
    }
    
    return count;
}

// ============================================================================
// 默认配置
// ============================================================================

void fusion_get_default_config(FusionModalityType fusion_type,
                               FusionDisplayConfig* display_config) {
    if (!display_config) return;
    
    memset(display_config, 0, sizeof(FusionDisplayConfig));
    display_config->fusion_type = fusion_type;
    display_config->fusion_ratio = 0.5f;
    display_config->opacity = 0.6f;
    display_config->overlay_enabled = 1;
    display_config->color_map = 0; // HotMetal
    display_config->window_center = 40;
    display_config->window_width = 400;
    display_config->pet_window_center = 3;
    display_config->pet_window_width = 6;
    
    switch (fusion_type) {
        case FUSION_PET_CT:
        case FUSION_PET_MR:
            display_config->window_center = 40;   // Soft tissue
            display_config->window_width = 400;
            display_config->pet_window_center = 3; // SUV
            display_config->pet_window_width = 6;
            break;
        case FUSION_US_CT:
        case FUSION_US_MR:
            display_config->window_center = 60;
            display_config->window_width = 200;
            display_config->fusion_ratio = 0.7f;
            break;
        case FUSION_SPECT_CT:
            display_config->pet_window_center = 0;
            display_config->pet_window_width = 100;
            break;
        default:
            break;
    }
}

} // extern "C"
