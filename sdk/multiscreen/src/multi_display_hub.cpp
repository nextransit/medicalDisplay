/**
 * @file multi_display_hub.cpp
 * @brief 多屏协同系统实现
 */

#include "multi_display_hub.h"
#include <cstring>
#include <algorithm>
#include <vector>
#include <cmath>

// ============================================================================
// 内部数据结构
// ============================================================================

struct DisplayContext {
    DisplayInfo info;
    uint16_t* lut_12bit;          // 12-bit LUT
    float gamma_curve[4096];       // GSDF曲线
    bool needs_recalibration;
    
    DisplayContext() : lut_12bit(nullptr), needs_recalibration(false) {
        memset(&info, 0, sizeof(info));
    }
    
    ~DisplayContext() {
        delete[] lut_12bit;
    }
};

struct MultiDisplayHub {
    std::vector<DisplayContext> displays;
    int max_displays;
    SyncMode sync_mode;
    ColorConsistencyConfig color_config;
    
    MultiDisplayHub(int max) : max_displays(max), sync_mode(SYNC_MODE_INDEPENDENT) {
        displays.reserve(max);
        
        // 默认色彩配置
        memset(&color_config, 0, sizeof(color_config));
        color_config.enable_color_matching = true;
        color_config.target_colorspace = 0; // sRGB
        color_config.target_white_point_x = 0.3127f;
        color_config.target_white_point_y = 0.3290f;
        color_config.target_luminance = 400.0f;
        color_config.apply_gsdf = true;
        color_config.ambient_light_lux = 0;
    }
};

// ============================================================================
// GSDF 计算 (DICOM Part 14)
// ============================================================================

static float gsdf_jnd_to_luminance(float jnd) {
    // GSDF inverse function: JND -> Luminance (cd/m²)
    // Based on Barten (1999) model
    float a = -1.3011877f;
    float b = -2.5840191e-2f;
    float c = 8.0242636e-1f;
    float d = -1.0320229e-1f;
    float e = 1.3646699e-2f;
    float f = 2.8745620e-2f;
    float g = -2.5468404e-3f;
    float h = -3.1978977e-3f;
    float k = 1.2992634e-4f;
    float m = 1.3635334e-3f;
    
    float log_l = a + c * jnd + e * jnd * jnd + g * jnd * jnd * jnd +
                  k * jnd * jnd * jnd * jnd +
                  m * jnd * jnd * jnd * jnd * jnd;
    
    return powf(10.0f, log_l);
}

static float gsdf_luminance_to_jnd(float lum) {
    // GSDF forward function: Luminance -> JND
    float log_l = log10f(std::max(1e-10f, lum));
    
    float a = -1.3011877f;
    float c = 8.0242636e-1f;
    float e = 1.3646699e-2f;
    float g = -2.5468404e-3f;
    float k = 1.2992634e-4f;
    float m = 1.3635334e-3f;
    
    // Newton-Raphson iteration
    float jnd = 100.0f;
    for (int iter = 0; iter < 20; iter++) {
        float log_l_est = a + c * jnd + e * jnd * jnd + g * jnd * jnd * jnd +
                          k * jnd * jnd * jnd * jnd +
                          m * jnd * jnd * jnd * jnd * jnd;
        float d_log_l = c + 2.0f * e * jnd + 3.0f * g * jnd * jnd +
                       4.0f * k * jnd * jnd * jnd +
                       5.0f * m * jnd * jnd * jnd * jnd;
        jnd = jnd - (log_l_est - log_l) / d_log_l;
        jnd = std::max(1.0f, jnd);
    }
    
    return jnd;
}

static void generate_gsdf_curve(float* curve, int size, float min_lum, float max_lum) {
    float log_min = log10f(std::max(1e-10f, min_lum));
    float log_max = log10f(std::max(1e-10f, max_lum));
    
    for (int i = 0; i < size; i++) {
        float t = static_cast<float>(i) / (size - 1);
        float target_log_lum = log_min + t * (log_max - log_min);
        float target_lum = powf(10.0f, target_log_lum);
        
        curve[i] = gsdf_luminance_to_jnd(target_lum);
    }
}

// ============================================================================
// 12-bit LUT 生成
// ============================================================================

static uint16_t* generate_unified_lut_12bit(const ColorConsistencyConfig& config,
                                             const DisplayInfo& display) {
    uint16_t* lut = new uint16_t[4096];
    
    float target_lum = config.target_luminance;
    float max_lum = display.max_luminance;
    float min_lum = std::max(1.0f, display.min_luminance);
    
    // Generate GSDF curve
    float gsdf_curve[4096];
    generate_gsdf_curve(gsdf_curve, 4096, min_lum, max_lum);
    
    for (int i = 0; i < 4096; i++) {
        float input_val = static_cast<float>(i) / 4095.0f;
        
        // Apply GSDF
        float lum = min_lum * powf(max_lum / min_lum, input_val);
        float jnd = gsdf_luminance_to_jnd(lum);
        
        // Map to target luminance
        float target_jnd = jnd * (gsdf_luminance_to_jnd(target_lum) / 
                                 gsdf_luminance_to_jnd(max_lum));
        float target_lum_out = gsdf_jnd_to_luminance(target_jnd);
        
        // Map back to 12-bit output
        float output_val = logf(target_lum_out / min_lum) / logf(max_lum / min_lum);
        output_val = std::max(0.0f, std::min(1.0f, output_val));
        
        lut[i] = static_cast<uint16_t>(output_val * 4095.0f);
    }
    
    return lut;
}

// ============================================================================
// 引擎生命周期
// ============================================================================

extern "C" {

MultiDisplayHub* multi_display_hub_create(int max_displays) {
    if (max_displays <= 0 || max_displays > 16) {
        return nullptr;
    }
    return new (std::nothrow) MultiDisplayHub(max_displays);
}

void multi_display_hub_destroy(MultiDisplayHub* hub) {
    delete hub;
}

// ============================================================================
// 显示器扫描与信息
// ============================================================================

int multi_display_hub_scan_displays(MultiDisplayHub* hub,
                                    DisplayInfo* displays,
                                    int max_count) {
    if (!hub || !displays || max_count <= 0) {
        return -1;
    }
    
    // Clear existing displays
    hub->displays.clear();
    
    // Simulate display detection (in production, this would use DRM/Wayland APIs)
    int detected = 0;
    
    // Primary display
    if (detected < max_count) {
        DisplayContext ctx;
        ctx.info.display_id = detected;
        ctx.info.type = DISPLAY_TYPE_DIAGNOSTIC;
        ctx.info.status = DISPLAY_STATUS_ONLINE;
        ctx.info.width = 3840;
        ctx.info.height = 2160;
        ctx.info.bit_depth = 10;
        ctx.info.max_luminance = 1000.0f;
        ctx.info.min_luminance = 1.0f;
        ctx.info.current_luminance = 450.0f;
        ctx.info.calibration_age_days = 7;
        ctx.info.is_primary = 1;
        strncpy(ctx.info.display_name, "Primary Diagnostic Display", 127);
        strncpy(ctx.info.edid_hash, "primary_edid_hash_abc123", 63);
        
        hub->displays.push_back(std::move(ctx));
        detected++;
    }
    
    // Secondary display
    if (detected < max_count) {
        DisplayContext ctx;
        ctx.info.display_id = detected;
        ctx.info.type = DISPLAY_TYPE_CLINICAL;
        ctx.info.status = DISPLAY_STATUS_ONLINE;
        ctx.info.width = 1920;
        ctx.info.height = 1080;
        ctx.info.bit_depth = 8;
        ctx.info.max_luminance = 350.0f;
        ctx.info.min_luminance = 1.0f;
        ctx.info.current_luminance = 300.0f;
        ctx.info.calibration_age_days = 14;
        ctx.info.is_primary = 0;
        strncpy(ctx.info.display_name, "Secondary Clinical Display", 127);
        strncpy(ctx.info.edid_hash, "secondary_edid_hash_def456", 63);
        
        hub->displays.push_back(std::move(ctx));
        detected++;
    }
    
    // Copy to output
    for (int i = 0; i < detected && i < max_count; i++) {
        displays[i] = hub->displays[i].info;
    }
    
    return detected;
}

int multi_display_hub_get_display_info(MultiDisplayHub* hub,
                                       int display_id,
                                       DisplayInfo* info) {
    if (!hub || !info) {
        return -1;
    }
    
    for (const auto& ctx : hub->displays) {
        if (ctx.info.display_id == display_id) {
            *info = ctx.info;
            return 0;
        }
    }
    
    return -1;
}

int multi_display_hub_set_primary(MultiDisplayHub* hub, int display_id) {
    if (!hub) {
        return -1;
    }
    
    for (auto& ctx : hub->displays) {
        ctx.info.is_primary = (ctx.info.display_id == display_id) ? 1 : 0;
    }
    
    return 0;
}

// ============================================================================
// 色彩一致性
// ============================================================================

int multi_display_hub_set_color_config(MultiDisplayHub* hub,
                                       const ColorConsistencyConfig* config) {
    if (!hub || !config) {
        return -1;
    }
    
    hub->color_config = *config;
    return 0;
}

int multi_display_hub_get_color_config(MultiDisplayHub* hub,
                                       ColorConsistencyConfig* config) {
    if (!hub || !config) {
        return -1;
    }
    
    *config = hub->color_config;
    return 0;
}

int multi_display_hub_apply_gsdf(MultiDisplayHub* hub,
                                  int display_id,
                                  int ambient_lux) {
    if (!hub) {
        return -1;
    }
    
    for (auto& ctx : hub->displays) {
        if (ctx.info.display_id == display_id) {
            // Generate GSDF curve
            float min_lum = ctx.info.min_luminance;
            float max_lum = ctx.info.max_luminance;
            generate_gsdf_curve(ctx.gamma_curve, 4096, min_lum, max_lum);
            
            // Generate 12-bit LUT
            delete[] ctx.lut_12bit;
            ctx.lut_12bit = generate_unified_lut_12bit(hub->color_config, ctx.info);
            
            ctx.info.status = DISPLAY_STATUS_ONLINE;
            ctx.info.calibration_age_days = 0;
            
            return 0;
        }
    }
    
    return -1;
}

int multi_display_hub_apply_unified_lut(MultiDisplayHub* hub,
                                        const int* display_ids,
                                        int count) {
    if (!hub || !display_ids || count <= 0) {
        return -1;
    }
    
    for (int i = 0; i < count; i++) {
        for (auto& ctx : hub->displays) {
            if (ctx.info.display_id == display_ids[i]) {
                // Generate unified LUT for this display
                delete[] ctx.lut_12bit;
                ctx.lut_12bit = generate_unified_lut_12bit(hub->color_config, ctx.info);
                break;
            }
        }
    }
    
    return 0;
}

// ============================================================================
// 协同模式
// ============================================================================

int multi_display_hub_set_sync_mode(MultiDisplayHub* hub, SyncMode mode) {
    if (!hub) {
        return -1;
    }
    
    hub->sync_mode = mode;
    return 0;
}

SyncMode multi_display_hub_get_sync_mode(MultiDisplayHub* hub) {
    return hub ? hub->sync_mode : SYNC_MODE_INDEPENDENT;
}

int multi_display_hub_sync_render(MultiDisplayHub* hub,
                                  const int* display_ids,
                                  int count,
                                  const uint8_t* source_data,
                                  int width, int height) {
    if (!hub || !display_ids || !source_data || count <= 0) {
        return -1;
    }
    
    // In production, this would use display server APIs to render to multiple displays
    // For now, we just validate the parameters
    
    (void)width;
    (void)height;
    
    return 0;
}

// ============================================================================
// 校准
// ============================================================================

int multi_display_hub_start_calibration(MultiDisplayHub* hub, int display_id) {
    if (!hub) {
        return -1;
    }
    
    for (auto& ctx : hub->displays) {
        if (ctx.info.display_id == display_id) {
            ctx.info.status = DISPLAY_STATUS_CALIBRATING;
            return 0;
        }
    }
    
    return -1;
}

int multi_display_hub_get_calibration_status(MultiDisplayHub* hub,
                                             int display_id,
                                             float* progress,
                                             char* status,
                                             int status_len) {
    if (!hub || !progress || !status) {
        return -1;
    }
    
    for (const auto& ctx : hub->displays) {
        if (ctx.info.display_id == display_id) {
            if (ctx.info.status == DISPLAY_STATUS_CALIBRATING) {
                *progress = 0.5f;  // Simulated progress
                strncpy(status, "Calibrating luminance...", status_len - 1);
            } else {
                *progress = 1.0f;
                strncpy(status, "Calibration complete", status_len - 1);
            }
            status[status_len - 1] = '\0';
            return 0;
        }
    }
    
    return -1;
}

int multi_display_hub_complete_calibration(MultiDisplayHub* hub, int display_id) {
    if (!hub) {
        return -1;
    }
    
    for (auto& ctx : hub->displays) {
        if (ctx.info.display_id == display_id) {
            ctx.info.status = DISPLAY_STATUS_ONLINE;
            ctx.info.calibration_age_days = 0;
            ctx.needs_recalibration = false;
            return 0;
        }
    }
    
    return -1;
}

// ============================================================================
// 诊断
// ============================================================================

int multi_display_hub_health_check(MultiDisplayHub* hub,
                                    int display_id,
                                    char** issues,
                                    int max_issues) {
    if (!hub || !issues || max_issues <= 0) {
        return -1;
    }
    
    int issue_count = 0;
    
    for (const auto& ctx : hub->displays) {
        if (ctx.info.display_id == display_id) {
            // Check calibration age
            if (ctx.info.calibration_age_days > 30) {
                if (issue_count < max_issues) {
                    issues[issue_count++] = const_cast<char*>("calibration_overdue");
                }
            }
            
            // Check luminance drift
            float expected_lum = hub->color_config.target_luminance;
            float drift = fabsf(ctx.info.current_luminance - expected_lum) / expected_lum;
            if (drift > 0.2f) {
                if (issue_count < max_issues) {
                    issues[issue_count++] = const_cast<char*>("luminance_drift");
                }
            }
            
            // Check status
            if (ctx.info.status == DISPLAY_STATUS_ERROR) {
                if (issue_count < max_issues) {
                    issues[issue_count++] = const_cast<char*>("display_error");
                }
            }
            
            break;
        }
    }
    
    return issue_count;
}

int multi_display_hub_export_calibration_report(MultiDisplayHub* hub,
                                                 int display_id,
                                                 const char* report_path) {
    if (!hub || !report_path) {
        return -1;
    }
    
    (void)report_path;
    (void)display_id;
    
    // In production, this would generate a PDF/JSON report
    return 0;
}

} // extern "C"
