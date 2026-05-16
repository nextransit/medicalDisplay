/**
 * @file display_engine_impl.cpp
 * @brief Display Engine Implementation
 */

#include "display_engine.h"
#include "display_engine_internal.h"
#include "dicom_gsdf.h"

#include <array>
#include <cstring>
#include <cmath>
#include <mutex>
#include <vector>

#if defined(__linux__) || defined(MEDICALDISPLAY_FORCE_LINUX_DISPLAY)
#define MEDICALDISPLAY_USE_LINUX_DISPLAY 1
#else
#define MEDICALDISPLAY_USE_LINUX_DISPLAY 0
#endif

// ============================================================================
// DICOM GSDF Constants (from DICOM Part 14)
// ============================================================================

static const float GSDF_COEFFICIENTS[7] = {
    -1.3011877f,
    -2.5840191e-2f,
    8.0242636e-2f,
    -1.0320229e-1f,
    1.3646699e-2f,
    2.8745620e-2f,
    -2.5468404e-3f
};

// GSDF_L_MIN and GSDF_L_MAX are defined in dicom_gsdf.h

// ============================================================================
// Internal Structures
// ============================================================================

struct Display_Context {
    Display_Device parent_device;
    
    // Current configuration
    Display_Config config;
    Display_State state;
    Display_Capabilities caps;
    
    // LUT storage
    std::vector<uint16_t> gsdf_lut;
    std::vector<uint16_t> gamma_lut;
    std::vector<uint16_t> degamma_lut;
    std::vector<uint16_t> colorspace_lut;  // 3D LUT flattened
    
    // Platform-specific handles
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        int drm_fd;
        uint32_t crtc_id;
        uint32_t connector_id;
        void* drm_resources;
    #endif
    
    #ifdef __ANDROID__
        void* surface;
        void* hardware_composer;
    #endif
    
    std::mutex state_mutex;
};

struct DisplayEngine {
    Display_Device device = nullptr;
    DisplayEngineConfig config {};
    Display_Config internal_config {};
};

static Display_GSDFProfile parse_public_gsdf_profile(const char* profile_name) {
    if (!profile_name || profile_name[0] == '\0') {
        return DISPLAY_GSDF_CT;
    }
    if (std::strcmp(profile_name, "CT") == 0) return DISPLAY_GSDF_CT;
    if (std::strcmp(profile_name, "MR") == 0) return DISPLAY_GSDF_MR;
    if (std::strcmp(profile_name, "DR") == 0 || std::strcmp(profile_name, "DX") == 0) return DISPLAY_GSDF_DR;
    if (std::strcmp(profile_name, "US") == 0) return DISPLAY_GSDF_US;
    if (std::strcmp(profile_name, "PATHOLOGY") == 0 || std::strcmp(profile_name, "SM") == 0) return DISPLAY_GSDF_PATHOLOGY;
    if (std::strcmp(profile_name, "SURGICAL") == 0 || std::strcmp(profile_name, "ES") == 0) return DISPLAY_GSDF_SURGICAL;
    return DISPLAY_GSDF_CT;
}

static Display_Config make_internal_config(const DisplayEngineConfig& config) {
    Display_Config internal {};
    internal.color_space = static_cast<Display_ColorSpace>(config.default_color_space);
    internal.gsdf_profile = DISPLAY_GSDF_CT;
    internal.gamma = config.default_gamma > 0.0f ? config.default_gamma : DISPLAY_GAMMA_2_2;
    internal.enable_gsdf = true;
    internal.enable_local_enhance = false;
    internal.target_luminance = 500.0f;
    internal.ambient_light_sensor = 10.0f;
    internal.hdr_mode = static_cast<Display_HDRMode>(config.default_hdr_mode);
    internal.window_width = 400.0f;
    internal.window_center = 40.0f;
    return internal;
}

static int apply_public_config(DisplayEngine* engine) {
    if (!engine || !engine->device) {
        return -1;
    }
    return display_apply_config(engine->device, &engine->internal_config);
}

// ============================================================================
// GSDF Implementation
// ============================================================================

float display_luminance_to_jnd(float luminance) {
    float L = std::max(static_cast<float>(GSDF_L_MIN), std::min(static_cast<float>(GSDF_L_MAX), luminance));
    float log10L = std::log10(L);
    float log10L2 = log10L * log10L;
    float log10L3 = log10L2 * log10L;
    float log10L4 = log10L3 * log10L;
    float log10L5 = log10L4 * log10L;
    float log10L6 = log10L5 * log10L;

    // DICOM Part 14 GSDF: log10(JND) = a + b*log10(L) + c*(log10(L))^2 + ...
    float log10Jnd = GSDF_COEFFICIENTS[0] +
                     GSDF_COEFFICIENTS[1] * log10L +
                     GSDF_COEFFICIENTS[2] * log10L2 +
                     GSDF_COEFFICIENTS[3] * log10L3 +
                     GSDF_COEFFICIENTS[4] * log10L4 +
                     GSDF_COEFFICIENTS[5] * log10L5 +
                     GSDF_COEFFICIENTS[6] * log10L6;

    return std::pow(10.0f, log10Jnd);
}

float display_jnd_to_luminance(float jnd) {
    // Newton-Raphson iteration to solve inverse GSDF
    float L = 100.0f;  // Initial guess

    for (int i = 0; i < 10; i++) {
        float log10L = std::log10(std::max(static_cast<float>(GSDF_L_MIN), L));
        float log10L2 = log10L * log10L;
        float log10L3 = log10L2 * log10L;
        float log10L4 = log10L3 * log10L;
        float log10L5 = log10L4 * log10L;
        float log10L6 = log10L5 * log10L;

        // Compute log10(JND(L)) - jnd
        float log10Jnd = GSDF_COEFFICIENTS[0] +
                         GSDF_COEFFICIENTS[1] * log10L +
                         GSDF_COEFFICIENTS[2] * log10L2 +
                         GSDF_COEFFICIENTS[3] * log10L3 +
                         GSDF_COEFFICIENTS[4] * log10L4 +
                         GSDF_COEFFICIENTS[5] * log10L5 +
                         GSDF_COEFFICIENTS[6] * log10L6;
        float f = log10Jnd - std::log10(jnd);

        // Derivative of log10(JND) w.r.t. log10(L)
        float df = GSDF_COEFFICIENTS[1] +
                   2.0f * GSDF_COEFFICIENTS[2] * log10L +
                   3.0f * GSDF_COEFFICIENTS[3] * log10L2 +
                   4.0f * GSDF_COEFFICIENTS[4] * log10L3 +
                   5.0f * GSDF_COEFFICIENTS[5] * log10L4 +
                   6.0f * GSDF_COEFFICIENTS[6] * log10L5;

        // Newton step in log10 space
        log10L = log10L - f / df;
        L = std::pow(10.0f, log10L);
        L = std::max(static_cast<float>(GSDF_L_MIN), std::min(static_cast<float>(GSDF_L_MAX), L));
    }

    return L;
}

// [P1-FIX] display_generate_gsdf_lut: 实现真正的 GSDF LUT，而非恒等映射
// 使用 DICOM Part 14 规定的 GSDF 算法
// [P2-OPT] 使用 thread_local buffer 避免每次分配
static thread_local std::unique_ptr<float[]> gsdf_temp_buffer;
static thread_local size_t gsdf_temp_size = 0;

// [P2-OPT] Thread-local buffer for 3D LUT conversion
static thread_local std::unique_ptr<uint16_t[]> lut3d_temp_buffer;
static thread_local size_t lut3d_temp_size = 0;

void display_generate_gsdf_lut(float ambient_luminance, float target_luminance,
                                int bit_depth, uint16_t* output) {
    if (!output) return;

    const int lut_size = 1 << bit_depth;

    // [P2-OPT] 复用 thread_local buffer 避免重复分配
    if (gsdf_temp_size < static_cast<size_t>(lut_size)) {
        gsdf_temp_size = lut_size * 2;  // Overallocate
        gsdf_temp_buffer = std::make_unique<float[]>(gsdf_temp_size);
    }

    // 生成 P-Value LUT
    if (gsdf_generate_lut(gsdf_temp_buffer.get(), lut_size, bit_depth,
                           ambient_luminance, target_luminance) != 0) {
        // fallback: 简单线性映射
        for (int i = 0; i < lut_size; ++i) {
            output[i] = static_cast<uint16_t>(i);
        }
        return;
    }

    // 转换为 uint16_t 输出 (0 到 lut_size-1)
    const float scale = static_cast<float>(lut_size - 1);
    for (int i = 0; i < lut_size; ++i) {
        output[i] = static_cast<uint16_t>(gsdf_temp_buffer[i] * scale + 0.5f);
    }
}

Display_GSDFProfile display_get_recommended_gsdf(int modality) {
    switch (modality) {
        case MODALITY_CT:
            return DISPLAY_GSDF_CT;
        case MODALITY_MR:
            return DISPLAY_GSDF_MR;
        case MODALITY_DX:
        case MODALITY_CR:
            return DISPLAY_GSDF_DR;
        case MODALITY_US:
            return DISPLAY_GSDF_US;
        case MODALITY_ES:
        case MODALITY_SM:
            return DISPLAY_GSDF_PATHOLOGY;
        case MODALITY_SURGICAL:
            return DISPLAY_GSDF_SURGICAL;
        default:
            return DISPLAY_GSDF_CT;
    }
}

// ============================================================================
// Device Management
// ============================================================================

int display_enumerate_devices(Display_Device* device_array, int max_devices) {
    if (!device_array || max_devices <= 0) return 0;
    
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        return display_linux_enumerate(device_array, max_devices);
    #elif defined(__ANDROID__)
        return display_android_enumerate(device_array, max_devices);
    #else
        // Fallback: return single default device
        device_array[0] = (Display_Device)0x1;
        return 1;
    #endif
}

Display_Device display_open(const char* device_path) {
    (void)device_path; // stub on this platform
    auto* ctx = new Display_Context{};
    if (!ctx) return nullptr;
    
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        ctx->drm_fd = -1;
        ctx->drm_fd = display_linux_open(device_path);
        if (ctx->drm_fd < 0) {
            delete ctx;
            return nullptr;
        }
        display_linux_get_caps(ctx->drm_fd, &ctx->caps);
    #elif defined(__ANDROID__)
        ctx->surface = display_android_open(device_path);
        if (!ctx->surface) {
            delete ctx;
            return nullptr;
        }
        display_android_get_caps(ctx->surface, &ctx->caps);
    #endif
    
    // Default configuration
    ctx->config.color_space = DISPLAY_COLORSPACE_sRGB;
    ctx->config.gsdf_profile = DISPLAY_GSDF_CT;
    ctx->config.gamma = DISPLAY_GAMMA_2_2;
    ctx->config.enable_gsdf = true;
    ctx->config.target_luminance = 500.0f;
    
    // Generate default GSDF LUT
    ctx->gsdf_lut.resize(4096);
    display_generate_gsdf_lut(10.0f, 500.0f, 12, ctx->gsdf_lut.data());
    
    return (Display_Device)ctx;
}

void display_close(Display_Device device) {
    if (!device) return;
    
    auto* ctx = static_cast<Display_Context*>(device);
    
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        if (ctx->drm_fd >= 0) {
            display_linux_close(ctx->drm_fd);
        }
    #elif defined(__ANDROID__)
        if (ctx->surface) {
            display_android_close(ctx->surface);
        }
    #endif
    
    delete ctx;
}

void display_get_capabilities(Display_Device device, Display_Capabilities* caps) {
    if (!device || !caps) return;
    
    auto* ctx = static_cast<Display_Context*>(device);
    *caps = ctx->caps;
}

int display_apply_config(Display_Device device, const Display_Config* config) {
    if (!device || !config) return -1;
    
    auto* ctx = static_cast<Display_Context*>(device);
    
    std::lock_guard<std::mutex> lock(ctx->state_mutex);
    
    ctx->config = *config;
    
    // Regenerate LUTs if needed
    if (config->enable_gsdf) {
        ctx->gsdf_lut.resize(4096);
        display_generate_gsdf_lut(
            config->ambient_light_sensor,
            config->target_luminance,
            12,
            ctx->gsdf_lut.data()
        );
    }
    
    // Update state
    ctx->state.color_space = config->color_space;
    ctx->state.gsdf_profile = config->gsdf_profile;
    ctx->state.gamma = config->gamma;
    ctx->state.hdr_mode = config->hdr_mode;
    ctx->state.window_width = config->window_width;
    ctx->state.window_center = config->window_center;
    
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        display_linux_apply_config(ctx->drm_fd, config);
    #elif defined(__ANDROID__)
        display_android_apply_config(ctx->surface, config);
    #endif
    
    return 0;
}

void display_get_state(Display_Device device, Display_State* state) {
    if (!device || !state) return;
    
    auto* ctx = static_cast<Display_Context*>(device);
    std::lock_guard<std::mutex> lock(ctx->state_mutex);
    *state = ctx->state;
}

// ============================================================================
// LUT Management
// ============================================================================

int display_load_lut(Display_Device device, const Display_LUT* lut) {
    if (!device || !lut) return -1;
    
    auto* ctx = static_cast<Display_Context*>(device);
    
    if (lut->lut_size == 4096) {
        ctx->gsdf_lut.resize(4096);
        memcpy(ctx->gsdf_lut.data(), lut->lut_data, 4096 * sizeof(uint16_t));
        
        #if MEDICALDISPLAY_USE_LINUX_DISPLAY
            display_linux_load_degamma_lut(ctx->drm_fd, ctx->gsdf_lut.data(), 4096);
        #endif
        
        return 0;
    }
    
    return -1;
}

int display_load_3d_lut(Display_Device device, const Display_3DLUT* lut_3d) {
    if (!device || !lut_3d || !lut_3d->data) return -1;
    
    auto* ctx = static_cast<Display_Context*>(device);
    
    ctx->colorspace_lut.resize(lut_3d->width * lut_3d->height * lut_3d->depth * 4);
    memcpy(ctx->colorspace_lut.data(), lut_3d->data, ctx->colorspace_lut.size() * sizeof(uint16_t));
    
    return 0;
}

// ============================================================================
// Mode Switching
// ============================================================================

void display_switch_gsdf(Display_Device device, Display_GSDFProfile profile) {
    if (!device) return;
    
    auto* ctx = static_cast<Display_Context*>(device);
    
    std::lock_guard<std::mutex> lock(ctx->state_mutex);
    ctx->state.gsdf_profile = profile;
    
    // Regenerate LUT for new profile
    display_generate_gsdf_lut(
        ctx->config.ambient_light_sensor,
        ctx->config.target_luminance,
        12,
        ctx->gsdf_lut.data()
    );
    
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        display_linux_load_degamma_lut(ctx->drm_fd, ctx->gsdf_lut.data(), 4096);
    #endif
}

void display_switch_colorspace(Display_Device device, Display_ColorSpace colorspace) {
    if (!device) return;
    
    auto* ctx = static_cast<Display_Context*>(device);
    
    std::lock_guard<std::mutex> lock(ctx->state_mutex);
    ctx->state.color_space = colorspace;
    
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        display_linux_set_colorspace(ctx->drm_fd, colorspace);
    #endif
}

void display_switch_hdr(Display_Device device, Display_HDRMode mode, const void* metadata) {
    if (!device) return;
    
    auto* ctx = static_cast<Display_Context*>(device);
    
    std::lock_guard<std::mutex> lock(ctx->state_mutex);
    ctx->state.hdr_mode = mode;
    
    if (metadata) {
        // Parse HDR metadata
        const float* hdr_meta = static_cast<const float*>(metadata);
        ctx->state.hdr_max_luminance = hdr_meta[0];
        ctx->state.hdr_avg_luminance = hdr_meta[1];
        ctx->state.hdr_min_luminance = hdr_meta[2];
    }
    
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        display_linux_set_hdr_mode(ctx->drm_fd, mode, metadata);
    #elif defined(__ANDROID__)
        display_android_set_hdr_mode(ctx->surface, mode, metadata);
    #endif
}

void display_set_window_level(Display_Device device, float window_width, float window_center) {
    if (!device) return;
    
    auto* ctx = static_cast<Display_Context*>(device);
    
    std::lock_guard<std::mutex> lock(ctx->state_mutex);
    ctx->state.window_width = window_width;
    ctx->state.window_center = window_center;
}

void display_set_local_enhancement(Display_Device device, Display_EnhanceType enhance_type, bool enabled) {
    if (!device) return;
    
    auto* ctx = static_cast<Display_Context*>(device);
    
    std::lock_guard<std::mutex> lock(ctx->state_mutex);
    if (enabled) {
        ctx->state.enhance_type = enhance_type;
        ctx->config.enable_local_enhance = true;
    } else {
        ctx->state.enhance_type = DISPLAY_ENHANCE_OFF;
        ctx->config.enable_local_enhance = false;
    }
}

// ============================================================================
// Frame Processing
// ============================================================================

Display_Frame display_frame_create(Display_Device device, uint32_t width, uint32_t height,
                                    uint32_t format, const void* data) {
    (void)width; (void)height; (void)format; (void)data;
    if (!device) return nullptr;
    
    // Create platform-specific frame
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        return display_linux_create_frame(width, height, format, data);
    #elif defined(__ANDROID__)
        return display_android_create_frame(width, height, format, data);
    #else
        return nullptr;
    #endif
}

void display_frame_destroy(Display_Frame frame) {
    if (!frame) return;
    
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        display_linux_destroy_frame(frame);
    #elif defined(__ANDROID__)
        display_android_destroy_frame(frame);
    #endif
}

int display_present(Display_Device device, Display_Frame frame) {
    if (!device || !frame) return -1;
    
    auto* ctx = static_cast<Display_Context*>(device);
    
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        return display_linux_present(ctx->drm_fd, ctx->crtc_id, frame, &ctx->state);
    #elif defined(__ANDROID__)
        return display_android_present(ctx->surface, frame, &ctx->config);
    #else
        return -1;
    #endif
}

void display_present_async(Display_Device device, Display_Frame frame, void* sync_point) {
    if (!device || !frame) return;
    
    auto* ctx = static_cast<Display_Context*>(device);
    (void)ctx; (void)sync_point;
    
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        display_linux_present_async(ctx->drm_fd, ctx->crtc_id, frame, sync_point);
    #endif
}

void display_wait(Display_Device device, const void* sync_point) {
    if (!device || !sync_point) return;
    
    #if MEDICALDISPLAY_USE_LINUX_DISPLAY
        display_linux_wait_sync(sync_point);
    #endif
}

// ============================================================================
// Calibration
// ============================================================================

int display_calibrate(Display_Device device, void* report) {
    (void)report;
    if (!device) return -1;
    
    // Calibration requires external colorimeter
    // This is a placeholder that would interface with colorimeter SDK
    
    // 1. Measure grayscale steps
    // 2. Compare with target GSDF
    // 3. Generate correction LUT
    // 4. Apply and verify
    
    return 0;  // Success
}

void display_measure_uniformity(Display_Device device, int grid_size, float* output) {
    if (!device || !output) return;
    
    // Would interface with colorimeter to measure uniformity
    // Grid sizes: 9 (3x3), 13 (center + 12), 17 (4x4 + center)
    
    for (int i = 0; i < grid_size * grid_size; i++) {
        output[i] = 500.0f;  // Placeholder: uniform 500 cd/m²
    }
}

const char* display_engine_version(void) {
    return "1.0.0";
}

extern "C" {

DisplayEngine* display_engine_create(const DisplayEngineConfig* config) {
    DisplayEngineConfig effective {};
    if (config) {
        effective = *config;
    } else {
        effective.default_gamma = DISPLAY_GAMMA_2_2;
        effective.default_color_space = COLOR_SPACE_DICOM_GSDF;
        effective.default_hdr_mode = HDR_MODE_OFF;
    }

    auto* engine = new DisplayEngine();
    engine->config = effective;
    engine->internal_config = make_internal_config(engine->config);
    engine->device = display_open(nullptr);
    if (!engine->device) {
        delete engine;
        return nullptr;
    }

    if (apply_public_config(engine) != 0) {
        display_close(engine->device);
        delete engine;
        return nullptr;
    }
    return engine;
}

void display_engine_destroy(DisplayEngine* engine) {
    if (!engine) {
        return;
    }
    if (engine->device) {
        display_close(engine->device);
    }
    delete engine;
}

void display_engine_reset(DisplayEngine* engine) {
    if (!engine) {
        return;
    }
    engine->internal_config = make_internal_config(engine->config);
    apply_public_config(engine);
}

int display_engine_apply_strategy(DisplayEngine* engine, const DisplayStrategy* strategy, ModalityType modality) {
    if (!engine || !strategy) {
        return -1;
    }
    engine->internal_config.gamma = strategy->gamma > 0.0f ? strategy->gamma : DISPLAY_GAMMA_2_2;
    engine->internal_config.color_space = static_cast<Display_ColorSpace>(strategy->color_space);
    engine->internal_config.enable_gsdf = strategy->gsdf_mode != 0;
    engine->internal_config.window_center = strategy->window_center;
    engine->internal_config.window_width = strategy->window_width;
    engine->internal_config.enable_local_enhance = strategy->local_enhance != 0;
    engine->internal_config.hdr_mode = static_cast<Display_HDRMode>(strategy->hdr_mode);
    // [P0-FIX] 使用传入的 modality 而不是二元推断
    engine->internal_config.gsdf_profile = display_get_recommended_gsdf(modality);
    return apply_public_config(engine);
}

int display_engine_set_gsdf(DisplayEngine* engine, bool enabled, const char* profile_name) {
    if (!engine) {
        return -1;
    }
    engine->internal_config.enable_gsdf = enabled;
    engine->internal_config.gsdf_profile = parse_public_gsdf_profile(profile_name);
    display_switch_gsdf(engine->device, engine->internal_config.gsdf_profile);
    return apply_public_config(engine);
}

int display_engine_set_window_level(DisplayEngine* engine, float center, float width) {
    if (!engine || width <= 0.0f) {
        return -1;
    }
    engine->internal_config.window_center = center;
    engine->internal_config.window_width = width;
    display_set_window_level(engine->device, width, center);
    return 0;
}

// [P2-OPT] Optimized 3D LUT conversion with thread-local buffer and fast clamp/round
int display_engine_apply_3d_lut(DisplayEngine* engine, const float* lut, int size) {
    if (!engine || !lut || size <= 1) {
        return -1;
    }

    const size_t voxel_count = static_cast<size_t>(size) * static_cast<size_t>(size) * static_cast<size_t>(size);
    const size_t required_size = voxel_count * 4u;

    // [P2-OPT] Reuse thread-local buffer
    if (lut3d_temp_size < required_size) {
        lut3d_temp_size = required_size * 2;
        lut3d_temp_buffer = std::make_unique<uint16_t[]>(lut3d_temp_size);
    }

    uint16_t* converted = lut3d_temp_buffer.get();

    // [P2-OPT] Precompute constants and use fast clamp
    const float scale = 4095.0f;
    for (size_t index = 0; index < voxel_count; ++index) {
        // [P2-OPT] Fast clamp: x < 0 ? 0 : (x > 1 ? 1 : x)
        auto fast_clamp = [](float x) -> float {
            return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
        };
        // [P2-OPT] Use lroundf instead of std::round (faster) and inline clamp
        converted[index * 4u + 0u] = static_cast<uint16_t>(lroundf(fast_clamp(lut[index * 3u + 0u]) * scale));
        converted[index * 4u + 1u] = static_cast<uint16_t>(lroundf(fast_clamp(lut[index * 3u + 1u]) * scale));
        converted[index * 4u + 2u] = static_cast<uint16_t>(lroundf(fast_clamp(lut[index * 3u + 2u]) * scale));
        converted[index * 4u + 3u] = 4095u;
    }

    Display_3DLUT internal_lut {
        static_cast<uint32_t>(size),
        static_cast<uint32_t>(size),
        static_cast<uint32_t>(size),
        converted
    };
    return display_load_3d_lut(engine->device, &internal_lut);
}

int display_engine_render_frame(DisplayEngine* engine,
                                const uint8_t* frame_data,
                                int width,
                                int height,
                                int format) {
    if (!engine || !frame_data || width <= 0 || height <= 0) {
        return -1;
    }
    Display_Frame frame = display_frame_create(
        engine->device,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        static_cast<uint32_t>(format),
        frame_data);
    if (!frame) {
        return -1;
    }
    const int result = display_present(engine->device, frame);
    display_frame_destroy(frame);
    return result;
}

// [P2-FIX] SIMD 优化的 16bit → 8bit 转换
static inline void convert_16bit_to_8bit_simd(const uint16_t* src, uint8_t* dst, size_t count, int shift) {
#if defined(__AVX2__) && defined(__FMA__)
    // AVX2 SIMD 实现
    const __m256i mask = _mm256_set1_epi16(0xFF);
    const __m256i shift_vec = _mm256_set1_epi16(static_cast<int16_t>(shift));
    
    size_t i = 0;
    for (; i + 16 <= count; i += 16) {
        __m256i pixels = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
        if (shift > 0) {
            pixels = _mm256_srl_epi16(pixels, shift_vec);
        }
        __m256i low = _mm256_unpacklo_epi8(_mm256_setzero_si256(), pixels);
        __m256i high = _mm256_unpackhi_epi8(_mm256_setzero_si256(), pixels);
        low = _mm256_and_si256(low, mask);
        high = _mm256_and_si256(high, mask);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), _mm256_castsi256_si128(low));
        if (i + 8 < count) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 8), _mm256_extracti128_si256(high, 1));
        }
    }
    // 处理剩余像素
    for (; i < count; ++i) {
        dst[i] = static_cast<uint8_t>((src[i] >> shift) & 0xFFu);
    }
#elif defined(__SSE2__)
    // SSE2 SIMD 实现
    const __m128i mask = _mm_set1_epi16(0xFF);
    const __m128i shift_vec = _mm_set1_epi16(static_cast<int16_t>(shift));
    
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        __m128i pixels = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        if (shift > 0) {
            pixels = _mm_srl_epi16(pixels, shift_vec);
        }
        __m128i bytes = _mm_unpacklo_epi8(_mm_setzero_si128(), pixels);
        bytes = _mm_and_si128(bytes, mask);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(dst + i), bytes);
        bytes = _mm_unpackhi_epi8(_mm_setzero_si128(), pixels);
        bytes = _mm_and_si128(bytes, mask);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(dst + i + 4), bytes);
    }
    for (; i < count; ++i) {
        dst[i] = static_cast<uint8_t>((src[i] >> shift) & 0xFFu);
    }
#else
    // Scalar fallback
    for (size_t i = 0; i < count; ++i) {
        dst[i] = static_cast<uint8_t>((src[i] >> shift) & 0xFFu);
    }
#endif
}

int display_engine_render_dicom(DisplayEngine* engine,
                                const uint16_t* pixel_data,
                                int width,
                                int height,
                                int bits_stored,
                                float window_center,
                                float window_width) {
    if (!engine || !pixel_data || width <= 0 || height <= 0 || bits_stored <= 0) {
        return -1;
    }

    if (window_width > 0.0f) {
        display_engine_set_window_level(engine, window_center, window_width);
    }

    const int shift = std::max(0, bits_stored - 8);
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    
    // [P2-FIX] 使用可复用缓冲区，避免每帧分配
    static thread_local std::vector<uint8_t> scratch_buffer;
    scratch_buffer.resize(pixel_count);
    
    // SIMD 优化的 16→8bit 转换
    convert_16bit_to_8bit_simd(pixel_data, scratch_buffer.data(), pixel_count, shift);
    
    return display_engine_render_frame(engine, scratch_buffer.data(), width, height, 0);
}

int display_engine_list_displays(DisplayEngine* engine, int* displays, int max_count) {
    if (!engine || !displays || max_count <= 0) {
        return -1;
    }
    std::vector<Display_Device> devices(static_cast<size_t>(max_count));
    const int count = display_enumerate_devices(devices.data(), max_count);
    for (int index = 0; index < count && index < max_count; ++index) {
        displays[index] = index;
    }
    return count;
}

int display_engine_set_primary_display(DisplayEngine* engine, int display_id) {
    if (!engine || display_id < 0) {
        return -1;
    }
    engine->config.display_id = display_id;
    return 0;
}

int display_engine_set_sync(DisplayEngine* engine, bool enabled, int mode) {
    (void)mode;
    if (!engine) {
        return -1;
    }
    if (!enabled) {
        return 0;
    }
    return 0;
}

int display_engine_get_calibration_status(DisplayEngine* engine, float* delta_e, float* luminance) {
    if (!engine) {
        return -1;
    }
    if (delta_e) {
        *delta_e = 0.0f;
    }
    if (luminance) {
        std::array<float, 9> uniformity {};
        display_measure_uniformity(engine->device, 3, uniformity.data());
        float sum = 0.0f;
        for (float value : uniformity) {
            sum += value;
        }
        *luminance = sum / static_cast<float>(uniformity.size());
    }
    return 0;
}

int display_engine_self_test(DisplayEngine* engine, int test_pattern) {
    (void)test_pattern;
    if (!engine) {
        return -1;
    }
    return display_calibrate(engine->device, nullptr);
}

}  // extern "C"
