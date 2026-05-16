/**
 * @file surgical_video.cpp
 * @brief 术野视频实时增强引擎实现
 * 
 * 支持 <20ms 低延迟的手术视频增强
 */

#include "surgical_video.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <queue>
#include <chrono>
#include <string>

// ============================================================================
// 内部数据结构
// ============================================================================

struct FrameBuffer {
    std::vector<uint8_t> data;
    VideoFrameInfo info;
    uint64_t timestamp;
};

struct SurgicalVideoEngine {
    SurgicalType surgical_type;
    bool use_gpu;
    EnhancementParams params;
    EnhancementMode current_mode;
    
    // 性能统计
    PerformanceStats stats;
    std::vector<float> latency_history;
    size_t latency_history_size;
    
    // AR叠加
    std::vector<uint8_t> ar_overlay;
    std::string annotations;
    
    // 帧处理
    std::queue<FrameBuffer> frame_queue;
    uint32_t frame_counter;
    
    SurgicalVideoEngine(SurgicalType type, bool gpu)
        : surgical_type(type), use_gpu(gpu), current_mode(ENHANCE_NONE),
          latency_history_size(100), frame_counter(0) {
        memset(&params, 0, sizeof(params));
        memset(&stats, 0, sizeof(stats));
        
        params.brightness = 0.0f;
        params.contrast = 1.0f;
        params.saturation = 1.0f;
        params.sharpness = 0.5f;
        params.bloodless_strength = 0.0f;
        params.edge_strength = 0.3f;
        params.edge_threshold = 0.1f;
        params.edge_thickness = 1.0f;
        params.pseudo_color_enable = false;
        params.hdr_enable = false;
        params.ar_overlay_enable = false;
    }
};

// ============================================================================
// 辅助函数
// ============================================================================

static inline uint8_t clamp_uint8(float value) {
    return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, value)));
}

// Sobel边缘检测
static void sobel_edge_detect(const uint8_t* gray, int width, int height,
                              uint8_t* edge, float threshold) {
    const int sobel_x[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    const int sobel_y[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};
    
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            float gx = 0.0f, gy = 0.0f;
            
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    int idx = (y + ky) * width + (x + kx);
                    uint8_t pixel = gray[idx];
                    gx += pixel * sobel_x[ky + 1][kx + 1];
                    gy += pixel * sobel_y[ky + 1][kx + 1];
                }
            }
            
            float magnitude = std::sqrt(gx * gx + gy * gy);
            int out_idx = y * width + x;
            
            if (magnitude > threshold * 255.0f) {
                edge[out_idx] = 255;
            } else {
                edge[out_idx] = 0;
            }
        }
    }
}

// 血流检测 (简化血红蛋白特征)
static float detect_blood_content(const uint8_t* rgb, int width, int height) {
    float blood_score = 0.0f;
    int count = 0;
    
    for (int i = 0; i < width * height; i += 100) {
        uint8_t r = rgb[i * 3 + 0];
        uint8_t g = rgb[i * 3 + 1];
        uint8_t b = rgb[i * 3 + 2];
        
        if (r > 150 && g < 100 && b < 100) {
            blood_score += 1.0f;
        } else if (r > g && r > b && r - g > 30) {
            blood_score += 0.5f;
        }
        count++;
    }
    
    return count > 0 ? blood_score / count : 0.0f;
}

// RGB转YUV
static void rgb_to_yuv(const uint8_t* rgb, uint8_t* yuv, int pixel_count) {
    for (int i = 0; i < pixel_count; i++) {
        uint8_t r = rgb[i * 3 + 0];
        uint8_t g = rgb[i * 3 + 1];
        uint8_t b = rgb[i * 3 + 2];
        
        yuv[i * 3 + 0] = clamp_uint8(0.299f * r + 0.587f * g + 0.114f * b);
        yuv[i * 3 + 1] = clamp_uint8(-0.169f * r - 0.331f * g + 0.5f * b + 128);
        yuv[i * 3 + 2] = clamp_uint8(0.5f * r - 0.419f * g - 0.081f * b + 128);
    }
}

// YUV转RGB
static void yuv_to_rgb(const uint8_t* yuv, uint8_t* rgb, int pixel_count) {
    for (int i = 0; i < pixel_count; i++) {
        int y = yuv[i * 3 + 0] - 16;
        int u = yuv[i * 3 + 1] - 128;
        int v = yuv[i * 3 + 2] - 128;
        
        rgb[i * 3 + 0] = clamp_uint8(1.164f * y + 1.596f * v);
        rgb[i * 3 + 1] = clamp_uint8(1.164f * y - 0.392f * u - 0.813f * v);
        rgb[i * 3 + 2] = clamp_uint8(1.164f * y + 2.017f * u);
    }
}

// 更新延迟统计
static void update_latency_stats(PerformanceStats* stats, float latency_ms) {
    if (stats->frames_processed == 0) {
        stats->total_latency_ms = latency_ms;
        stats->avg_latency_ms = latency_ms;
        stats->max_latency_ms = latency_ms;
        stats->p95_latency_ms = latency_ms;
        stats->p99_latency_ms = latency_ms;
    } else {
        float alpha = 0.1f;
        stats->avg_latency_ms = alpha * latency_ms + (1.0f - alpha) * stats->avg_latency_ms;
        stats->total_latency_ms += latency_ms;
        stats->max_latency_ms = std::max(stats->max_latency_ms, latency_ms);
        
        if (latency_ms > stats->p95_latency_ms * 0.9f) {
            stats->p95_latency_ms = latency_ms * 1.05f;
        }
        if (latency_ms > stats->p99_latency_ms * 0.95f) {
            stats->p99_latency_ms = latency_ms * 1.02f;
        }
    }
    
    stats->current_fps = 1000.0f / stats->avg_latency_ms;
}

// ============================================================================
// 引擎生命周期
// ============================================================================

extern "C" {

SurgicalVideoEngine* surgical_engine_create(SurgicalType surgical_type, bool use_gpu) {
    return new (std::nothrow) SurgicalVideoEngine(surgical_type, use_gpu);
}

void surgical_engine_destroy(SurgicalVideoEngine* engine) {
    delete engine;
}

int surgical_engine_config(SurgicalVideoEngine* engine, const EnhancementParams* params) {
    if (!engine || !params) return -1;
    engine->params = *params;
    return 0;
}

int surgical_engine_get_config(SurgicalVideoEngine* engine, EnhancementParams* params) {
    if (!engine || !params) return -1;
    *params = engine->params;
    return 0;
}

// ============================================================================
// 帧处理
// ============================================================================

int surgical_engine_process_frame(SurgicalVideoEngine* engine,
                                 const uint8_t* input,
                                 const VideoFrameInfo* input_info,
                                 uint8_t* output,
                                 VideoFrameInfo* output_info) {
    if (!engine || !input || !input_info || !output) return -1;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    int pixel_count = input_info->width * input_info->height;
    
    if (output_info) {
        *output_info = *input_info;
    }
    
    std::vector<uint8_t> yuv(pixel_count * 3);
    std::vector<uint8_t> temp_rgb(pixel_count * 3);
    
    if (input_info->format == 0) {
        std::memcpy(yuv.data(), input, pixel_count * 3);
    } else {
        rgb_to_yuv(input, yuv.data(), pixel_count);
    }
    
    yuv_to_rgb(yuv.data(), temp_rgb.data(), pixel_count);
    
    // 亮度/对比度/饱和度调整
    float brightness = engine->params.brightness * 128.0f;
    float contrast = engine->params.contrast;
    float saturation = engine->params.saturation;
    
    for (int i = 0; i < pixel_count; i++) {
        int idx = i * 3;
        
        float r = temp_rgb[idx + 0];
        float g = temp_rgb[idx + 1];
        float b = temp_rgb[idx + 2];
        
        r += brightness;
        g += brightness;
        b += brightness;
        
        r = ((r - 128.0f) * contrast) + 128.0f;
        g = ((g - 128.0f) * contrast) + 128.0f;
        b = ((b - 128.0f) * contrast) + 128.0f;
        
        float gray = 0.299f * r + 0.587f * g + 0.114f * b;
        r = gray + (r - gray) * saturation;
        g = gray + (g - gray) * saturation;
        b = gray + (b - gray) * saturation;
        
        temp_rgb[idx + 0] = clamp_uint8(r);
        temp_rgb[idx + 1] = clamp_uint8(g);
        temp_rgb[idx + 2] = clamp_uint8(b);
    }
    
    // 无血术野处理
    if (engine->params.bloodless_strength > 0.0f) {
        float blood_ratio = detect_blood_content(temp_rgb.data(), 
                                                 input_info->width, input_info->height);
        if (blood_ratio > engine->params.bloodless_threshold) {
            float suppress = engine->params.bloodless_strength * 0.5f;
            for (int i = 0; i < pixel_count; i++) {
                int idx = i * 3;
                uint8_t r = temp_rgb[idx + 0];
                uint8_t g = temp_rgb[idx + 1];
                uint8_t b = temp_rgb[idx + 2];
                
                if (r > g + 20 && r > b + 20) {
                    temp_rgb[idx + 0] = clamp_uint8(r * (1.0f - suppress));
                    temp_rgb[idx + 1] = clamp_uint8(g * (1.0f + suppress * 0.5f));
                    temp_rgb[idx + 2] = clamp_uint8(b * (1.0f + suppress * 0.5f));
                }
            }
        }
    }
    
    // 边缘增强
    if (engine->current_mode != ENHANCE_NONE && engine->params.edge_strength > 0.0f) {
        std::vector<uint8_t> gray(pixel_count);
        for (int i = 0; i < pixel_count; i++) {
            gray[i] = static_cast<uint8_t>(
                0.299f * temp_rgb[i * 3 + 0] +
                0.587f * temp_rgb[i * 3 + 1] +
                0.114f * temp_rgb[i * 3 + 2]
            );
        }
        
        std::vector<uint8_t> edge(pixel_count);
        sobel_edge_detect(gray.data(), input_info->width, input_info->height,
                          edge.data(), engine->params.edge_threshold);
        
        for (int i = 0; i < pixel_count; i++) {
            if (edge[i] > 0) {
                int idx = i * 3;
                float strength = engine->params.edge_strength * 0.5f;
                
                switch (engine->current_mode) {
                    case ENHANCE_VASCULAR:
                        temp_rgb[idx + 0] = clamp_uint8(temp_rgb[idx + 0] * (1.0f + strength));
                        break;
                    case ENHANCE_NERVE:
                        temp_rgb[idx + 0] = clamp_uint8(temp_rgb[idx + 0] * (1.0f + strength));
                        temp_rgb[idx + 1] = clamp_uint8(temp_rgb[idx + 1] * (1.0f + strength));
                        break;
                    case ENHANCE_TISSUE_BOUNDRY:
                        temp_rgb[idx + 0] = clamp_uint8(temp_rgb[idx + 0] + 30 * strength);
                        temp_rgb[idx + 1] = clamp_uint8(temp_rgb[idx + 1] + 30 * strength);
                        temp_rgb[idx + 2] = clamp_uint8(temp_rgb[idx + 2] + 30 * strength);
                        break;
                    default:
                        break;
                }
            }
        }
    }
    
    // 锐化
    if (engine->params.sharpness > 0.0f) {
        int w = input_info->width;
        int h = input_info->height;
        
        for (int y = 1; y < h - 1; y++) {
            for (int x = 1; x < w - 1; x++) {
                int idx = (y * w + x) * 3;
                
                for (int c = 0; c < 3; c++) {
                    float center = temp_rgb[idx + c];
                    float blur = 0.0f;
                    int count = 0;
                    
                    for (int ky = -1; ky <= 1; ky++) {
                        for (int kx = -1; kx <= 1; kx++) {
                            int px = x + kx;
                            int py = y + ky;
                            int pidx = (py * w + px) * 3;
                            blur += temp_rgb[pidx + c];
                            count++;
                        }
                    }
                    blur /= count;
                    
                    float sharpened = center + (center - blur) * engine->params.sharpness;
                    temp_rgb[idx + c] = clamp_uint8(sharpened);
                }
            }
        }
    }
    
    rgb_to_yuv(temp_rgb.data(), yuv.data(), pixel_count);
    
    if (output_info && output_info->format == 0) {
        std::memcpy(output, yuv.data(), pixel_count * 3);
    } else {
        yuv_to_rgb(yuv.data(), output, pixel_count);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    float latency_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
    
    engine->stats.frames_processed++;
    update_latency_stats(&engine->stats, latency_ms);
    
    return 0;
}

int surgical_engine_process_batch(SurgicalVideoEngine* engine,
                                 const uint8_t** frames,
                                 int count,
                                 uint8_t** outputs) {
    if (!engine || !frames || !outputs) return -1;
    
    int processed = 0;
    VideoFrameInfo info = {1920, 1080, 1, 0, 60.0f, 0, 0};
    
    for (int i = 0; i < count; i++) {
        if (surgical_engine_process_frame(engine, frames[i], &info, outputs[i], &info) == 0) {
            processed++;
        }
    }
    
    return processed;
}

int surgical_engine_set_mode(SurgicalVideoEngine* engine, EnhancementMode mode) {
    if (!engine) return -1;
    engine->current_mode = mode;
    return 0;
}

EnhancementMode surgical_engine_get_mode(SurgicalVideoEngine* engine) {
    return engine ? engine->current_mode : ENHANCE_NONE;
}

int surgical_engine_set_ar_overlay(SurgicalVideoEngine* engine,
                                   const uint8_t* overlay_data,
                                   size_t data_size) {
    if (!engine) return -1;
    engine->ar_overlay.assign(overlay_data, overlay_data + data_size);
    return 0;
}

int surgical_engine_update_annotations(SurgicalVideoEngine* engine,
                                       const char* annotations) {
    if (!engine) return -1;
    if (annotations) {
        engine->annotations = annotations;
    }
    return 0;
}

int surgical_engine_get_stats(SurgicalVideoEngine* engine, PerformanceStats* stats) {
    if (!engine || !stats) return -1;
    *stats = engine->stats;
    return 0;
}

void surgical_engine_reset_stats(SurgicalVideoEngine* engine) {
    if (!engine) return;
    memset(&engine->stats, 0, sizeof(engine->stats));
    engine->latency_history.clear();
}

bool surgical_engine_check_latency(SurgicalVideoEngine* engine, float max_latency_ms) {
    if (!engine) return false;
    return engine->stats.avg_latency_ms <= max_latency_ms;
}

void surgical_preset_laparoscopic(EnhancementParams* params) {
    if (!params) return;
    memset(params, 0, sizeof(EnhancementParams));
    params->brightness = 0.05f;
    params->contrast = 1.1f;
    params->saturation = 1.2f;
    params->sharpness = 0.4f;
    params->bloodless_strength = 0.3f;
    params->bloodless_threshold = 0.15f;
    params->mode = ENHANCE_VASCULAR;
    params->edge_strength = 0.5f;
    params->edge_threshold = 0.08f;
    params->hdr_enable = true;
}

void surgical_preset_endoscopic(EnhancementParams* params) {
    if (!params) return;
    memset(params, 0, sizeof(EnhancementParams));
    params->brightness = 0.0f;
    params->contrast = 1.2f;
    params->saturation = 1.3f;
    params->sharpness = 0.5f;
    params->bloodless_strength = 0.4f;
    params->bloodless_threshold = 0.1f;
    params->mode = ENHANCE_TISSUE_BOUNDRY;
    params->edge_strength = 0.6f;
    params->edge_threshold = 0.06f;
    params->hdr_enable = true;
}

void surgical_preset_microscopic(EnhancementParams* params) {
    if (!params) return;
    memset(params, 0, sizeof(EnhancementParams));
    params->brightness = 0.1f;
    params->contrast = 1.15f;
    params->saturation = 1.0f;
    params->sharpness = 0.6f;
    params->bloodless_strength = 0.2f;
    params->bloodless_threshold = 0.2f;
    params->mode = ENHANCE_NERVE;
    params->edge_strength = 0.7f;
    params->edge_threshold = 0.05f;
    params->pseudo_color_enable = true;
    params->pseudo_color_intensity = 0.5f;
    params->hdr_enable = true;
}

void surgical_preset_bloodless(EnhancementParams* params) {
    if (!params) return;
    memset(params, 0, sizeof(EnhancementParams));
    params->brightness = 0.02f;
    params->contrast = 1.05f;
    params->saturation = 0.9f;
    params->sharpness = 0.3f;
    params->bloodless_strength = 0.8f;
    params->bloodless_threshold = 0.08f;
    params->mode = ENHANCE_TISSUE_BOUNDRY;
    params->edge_strength = 0.4f;
    params->edge_threshold = 0.1f;
}

} // extern "C"
