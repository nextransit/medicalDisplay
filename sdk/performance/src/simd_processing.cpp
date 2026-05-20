/**
 * @file simd_processing.cpp
 * @brief SIMD加速图像处理实现
 * 
 * 支持 SSE4.2/AVX2 自动选择
 */

#include "simd_processing.h"
#include <cstring>
#include <cmath>
#include <chrono>
#include <vector>

// ============================================================================
// 平台检测
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64)
    #include <immintrin.h>
    #define HAS_X86_SIMD 1
#else
    #define HAS_X86_SIMD 0
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #define HAS_ARM_NEON 1
    void neon_brightness_contrast(const uint8_t* input, uint8_t* output,
                                  int width, int height, float brightness, float contrast);
    void neon_saturation(const uint8_t* input, uint8_t* output,
                         int width, int height, float saturation);
    void neon_rgb_to_grayscale(const uint8_t* rgb, uint8_t* gray, int width, int height);
    void neon_gsdf_lut_apply(const uint8_t* input, uint8_t* output,
                             int width, int height, const uint8_t* lut);
    void neon_sobel_edge(const uint8_t* gray, uint8_t* edge,
                         int width, int height, float threshold);
    void neon_bloodless_enhance(const uint8_t* input, uint8_t* output,
                                int width, int height,
                                float suppress_level, float tissue_enhance, float edge_preserve);
#else
    #define HAS_ARM_NEON 0
#endif

#if HAS_ARM_NEON && defined(MEDICALDISPLAY_ENABLE_NEON_IMPL)
    #define USE_ARM_NEON_IMPL 1
#else
    #define USE_ARM_NEON_IMPL 0
#endif

// ============================================================================
// 后端检测
// ============================================================================

static SIMDBackend detect_best_backend() {
#if USE_ARM_NEON_IMPL
    return SIMD_NEON;
#elif HAS_X86_SIMD
    int info[4];
    __cpuid(info, 7);
    if (info[1] & (1 << 5)) {  // AVX2
        return SIMD_AVX2;
    }
    
    __cpuid(info, 1);
    if (info[2] & (1 << 20)) {  // SSE4.2
        return SIMD_SSE4;
    }
#endif
    return SIMD_NONE;
}

extern "C" {

SIMDBackend simd_get_backend(void) {
    static SIMDBackend backend = detect_best_backend();
    return backend;
}

const char* simd_get_backend_name(SIMDBackend backend) {
    switch (backend) {
        case SIMD_SSE4: return "SSE4.2";
        case SIMD_AVX2: return "AVX2";
        case SIMD_NEON: return "NEON";
        case SIMD_AUTO: {
            SIMDBackend b = simd_get_backend();
            return simd_get_backend_name(b);
        }
        default: return "Scalar";
    }
}

bool simd_is_supported(SIMDBackend backend) {
    if (backend == SIMD_AUTO) {
        backend = simd_get_backend();
    }
    return backend != SIMD_NONE;
}

// ============================================================================
// 标量实现 (Fallback)
// ============================================================================

static void scalar_brightness_contrast(const uint8_t* input,
                                    uint8_t* output,
                                    int width, int height,
                                    float brightness,
                                    float contrast) {
    float bright_offset = brightness * 128.0f;
    
    for (int i = 0; i < width * height * 3; i += 3) {
        for (int c = 0; c < 3; c++) {
            float val = input[i + c] + bright_offset;
            val = (val - 128.0f) * contrast + 128.0f;
            val = std::max(0.0f, std::min(255.0f, val));
            output[i + c] = (uint8_t)val;
        }
    }
}

static void scalar_saturation(const uint8_t* input,
                            uint8_t* output,
                            int width, int height,
                            float saturation) {
    for (int i = 0; i < width * height * 3; i += 3) {
        float r = input[i + 0];
        float g = input[i + 1];
        float b = input[i + 2];
        
        float gray = 0.299f * r + 0.587f * g + 0.114f * b;
        
        output[i + 0] = (uint8_t)std::max(0.0f, std::min(255.0f, gray + (r - gray) * saturation));
        output[i + 1] = (uint8_t)std::max(0.0f, std::min(255.0f, gray + (g - gray) * saturation));
        output[i + 2] = (uint8_t)std::max(0.0f, std::min(255.0f, gray + (b - gray) * saturation));
    }
}

// ============================================================================
// SSE4.2 实现
// ============================================================================

#if HAS_X86_SIMD

static void sse42_brightness_contrast(const uint8_t* input,
                                      uint8_t* output,
                                      int width, int height,
                                      float brightness,
                                      float contrast) {
    float bright_offset = brightness * 128.0f;
    __m128 bright_vec = _mm_set1_ps(bright_offset);
    __m128 contrast_vec = _mm_set1_ps(contrast);
    __m128 offset_vec = _mm_set1_ps(128.0f);
    
    int count = width * height;
    int i = 0;
    
    for (; i + 16 <= count * 3; i += 16) {
        __m128i input_vec = _mm_loadu_si128((__m128i*)(input + i));
        
        __m128 f0 = _mm_cvtepi32_ps(_mm_unpacklo_epi8(_mm_setzero_si128(), input_vec));
        __m128 f1 = _mm_cvtepi32_ps(_mm_unpackhi_epi8(_mm_setzero_si128(), input_vec));
        
        f0 = _mm_add_ps(f0, bright_vec);
        f1 = _mm_add_ps(f1, bright_vec);
        
        f0 = _mm_sub_ps(_mm_mul_ps(_mm_sub_ps(f0, offset_vec), contrast_vec), offset_vec);
        f1 = _mm_sub_ps(_mm_mul_ps(_mm_sub_ps(f1, offset_vec), contrast_vec), offset_vec);
        
        f0 = _mm_max_ps(_mm_setzero_ps(), _mm_min_ps(f0, _mm_set1_ps(255.0f)));
        f1 = _mm_max_ps(_mm_setzero_ps(), _mm_min_ps(f1, _mm_set1_ps(255.0f)));
        
        __m128i result = _mm_packus_epi32(_mm_cvtps_epi32(f0), _mm_cvtps_epi32(f1));
        result = _mm_packus_epi16(result, _mm_setzero_si128());
        
        _mm_storeu_si128((__m128i*)(output + i), result);
    }
    
    for (; i < count * 3; i += 3) {
        for (int c = 0; c < 3; c++) {
            float val = input[i + c] + bright_offset;
            val = (val - 128.0f) * contrast + 128.0f;
            val = std::max(0.0f, std::min(255.0f, val));
            output[i + c] = (uint8_t)val;
        }
    }
}

#endif // HAS_X86_SIMD

// ============================================================================
// 公共 API 实现
// ============================================================================

int simd_adjust_brightness_contrast(const uint8_t* input,
                                   uint8_t* output,
                                   int width, int height,
                                   float brightness,
                                   float contrast) {
    if (!input || !output || width <= 0 || height <= 0) return -1;
    
#if HAS_X86_SIMD
    SIMDBackend backend = simd_get_backend();
    if (backend == SIMD_SSE4 || backend == SIMD_AVX2) {
        sse42_brightness_contrast(input, output, width, height, brightness, contrast);
        return 0;
    }
#endif

    scalar_brightness_contrast(input, output, width, height, brightness, contrast);
    return 0;
}

int simd_adjust_saturation(const uint8_t* input,
                         uint8_t* output,
                         int width, int height,
                         float saturation) {
    if (!input || !output || width <= 0 || height <= 0) return -1;

#if USE_ARM_NEON_IMPL
    if (simd_get_backend() == SIMD_NEON) {
        neon_saturation(input, output, width, height, saturation);
        return 0;
    }
#endif

    scalar_saturation(input, output, width, height, saturation);
    return 0;
}

int simd_rgb_to_yuv420(const uint8_t* rgb,
                      uint8_t* yuv,
                      int width, int height) {
    if (!rgb || !yuv || width <= 0 || height <= 0) return -1;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            float r = rgb[idx];
            float g = rgb[idx + 1];
            float b = rgb[idx + 2];
            
            float y_val = 0.299f * r + 0.587f * g + 0.114f * b;
            yuv[y * width + x] = (uint8_t)std::max(0.0f, std::min(255.0f, y_val));
        }
    }
    
    for (int y = 0; y < height; y += 2) {
        for (int x = 0; x < width; x += 2) {
            int idx00 = (y * width + x) * 3;
            int idx10 = ((y + 1) * width + x) * 3;
            
            float r00 = rgb[idx00], g00 = rgb[idx00 + 1], b00 = rgb[idx00 + 2];
            float r10 = rgb[idx10], g10 = rgb[idx10 + 1], b10 = rgb[idx10 + 2];
            
            float u_val = -0.169f * (r00 + r10) - 0.331f * (g00 + g10) + 0.5f * (b00 + b10) + 128;
            float v_val = 0.5f * (r00 + r10) - 0.419f * (g00 + g10) - 0.081f * (b00 + b10) + 128;
            
            int uv_idx = width * height + (y / 2) * width + x;
            yuv[uv_idx] = (uint8_t)std::max(0.0f, std::min(255.0f, u_val));
            yuv[uv_idx + width * height / 4] = (uint8_t)std::max(0.0f, std::min(255.0f, v_val));
        }
    }
    
    return 0;
}

int simd_yuv420_to_rgb(const uint8_t* yuv,
                      uint8_t* rgb,
                      int width, int height) {
    if (!yuv || !rgb || width <= 0 || height <= 0) return -1;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float y_val = yuv[y * width + x];
            int uv_idx = width * height + (y / 2) * width + x;
            float u_val = yuv[uv_idx] - 128;
            float v_val = yuv[uv_idx + width * height / 4] - 128;
            
            float r = y_val + 1.402f * v_val;
            float g = y_val - 0.344f * u_val - 0.714f * v_val;
            float b = y_val + 1.772f * u_val;
            
            int idx = (y * width + x) * 3;
            rgb[idx] = (uint8_t)std::max(0.0f, std::min(255.0f, r));
            rgb[idx + 1] = (uint8_t)std::max(0.0f, std::min(255.0f, g));
            rgb[idx + 2] = (uint8_t)std::max(0.0f, std::min(255.0f, b));
        }
    }
    
    return 0;
}

int simd_convolution_3x3(const uint8_t* input,
                        uint8_t* output,
                        int width, int height,
                        const int8_t* kernel) {
    if (!input || !output || width <= 0 || height <= 0 || !kernel) return -1;
    
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int sum = 0;
            
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    int idx = (y + ky) * width + (x + kx);
                    sum += input[idx] * kernel[(ky + 1) * 3 + (kx + 1)];
                }
            }
            
            output[y * width + x] = (uint8_t)std::max(0, std::min(255, sum));
        }
    }
    
    return 0;
}

int simd_benchmark(int width, int height, int iterations, double* ops_per_sec) {
    if (width <= 0 || height <= 0 || iterations <= 0 || !ops_per_sec) return -1;
    
    std::vector<uint8_t> input(width * height * 3);
    std::vector<uint8_t> output(width * height * 3);
    
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = i % 256;
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; iter++) {
        simd_adjust_brightness_contrast(input.data(), output.data(),
                                      width, height, 0.1f, 1.1f);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    *ops_per_sec = iterations / duration;
    return 0;
}

} // extern "C"

// ============================================================================
// 扩展函数实现
// ============================================================================

int simd_gsdf_lut_apply(const uint8_t* input,
                       uint8_t* output,
                       int width, int height,
                       const uint8_t* lut) {
    if (!input || !output || !lut || width <= 0 || height <= 0) return -1;

#if USE_ARM_NEON_IMPL
    if (simd_get_backend() == SIMD_NEON) {
        neon_gsdf_lut_apply(input, output, width, height, lut);
        return 0;
    }
#endif

    size_t total = (size_t)width * height * 3;
    
    #pragma omp parallel for
    for (size_t i = 0; i < total; i++) {
        output[i] = lut[input[i]];
    }
    
    return 0;
}

int simd_edge_detection_sobel(const uint8_t* input,
                             uint8_t* edge,
                             int width, int height,
                             float threshold) {
    if (!input || !edge || width <= 0 || height <= 0) return -1;

    std::vector<uint8_t> gray(width * height);

#if USE_ARM_NEON_IMPL
    if (simd_get_backend() == SIMD_NEON) {
        neon_rgb_to_grayscale(input, gray.data(), width, height);
        neon_sobel_edge(gray.data(), edge, width, height, threshold);
        return 0;
    }
#endif

    // RGB到灰度
    #pragma omp parallel for
    for (int i = 0; i < width * height; i++) {
        int idx = i * 3;
        gray[i] = (uint8_t)(0.299f * input[idx] + 0.587f * input[idx + 1] + 0.114f * input[idx + 2]);
    }

    const int threshold_i = threshold <= 1.0f ? static_cast<int>(threshold * 255.0f)
                                               : static_cast<int>(threshold);
    
    // Sobel边缘检测
    #pragma omp parallel for
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int i = y * width + x;
            
            int gx = -gray[i - width - 1] + gray[i - width + 1]
                     - 2 * gray[i - 1] + 2 * gray[i + 1]
                     - gray[i + width - 1] + gray[i + width + 1];
            
            int gy = -gray[i - width - 1] - 2 * gray[i - width] - gray[i - width + 1]
                     + gray[i + width - 1] + 2 * gray[i + width] + gray[i + width + 1];
            
            int mag = std::abs(gx) + std::abs(gy);
            edge[i] = (mag > threshold_i) ? 255 : 0;
        }
    }
    
    return 0;
}

int simd_bloodless_enhance(const uint8_t* input,
                          uint8_t* output,
                          int width, int height,
                          float suppress_level,
                          float tissue_enhance,
                          float edge_preserve) {
    if (!input || !output || width <= 0 || height <= 0) return -1;

#if USE_ARM_NEON_IMPL
    if (simd_get_backend() == SIMD_NEON) {
        neon_bloodless_enhance(input, output, width, height,
                               suppress_level, tissue_enhance, edge_preserve);
        return 0;
    }
#endif

    #pragma omp parallel for
    for (int i = 0; i < width * height; i++) {
        int idx = i * 3;
        float r = input[idx], g = input[idx + 1], b = input[idx + 2];
        
        float blood_score = r * 0.5f - g * 0.3f - b * 0.2f;
        float blood_mask = std::max(0.0f, std::min(1.0f, blood_score / 128.0f));
        
        float suppress = blood_mask * suppress_level;
        r = r * (1.0f - suppress * 0.3f);
        g = g * (1.0f + suppress * 0.2f);
        b = b * (1.0f + suppress * 0.2f);
        
        float gray = 0.299f * r + 0.587f * g + 0.114f * b;
        r = gray + (r - gray) * (1.0f + tissue_enhance * 0.2f);
        g = gray + (g - gray) * (1.0f + tissue_enhance * 0.2f);
        b = gray + (b - gray) * (1.0f + tissue_enhance * 0.2f);
        
        output[idx] = (uint8_t)std::max(0.0f, std::min(255.0f, r));
        output[idx + 1] = (uint8_t)std::max(0.0f, std::min(255.0f, g));
        output[idx + 2] = (uint8_t)std::max(0.0f, std::min(255.0f, b));
    }
    
    return 0;
}

int simd_rgb_to_grayscale(const uint8_t* rgb,
                         uint8_t* gray,
                         int width, int height) {
    if (!rgb || !gray || width <= 0 || height <= 0) return -1;

#if USE_ARM_NEON_IMPL
    if (simd_get_backend() == SIMD_NEON) {
        neon_rgb_to_grayscale(rgb, gray, width, height);
        return 0;
    }
#endif

    #pragma omp parallel for
    for (int i = 0; i < width * height; i++) {
        int idx = i * 3;
        gray[i] = (uint8_t)(0.299f * rgb[idx] + 0.587f * rgb[idx + 1] + 0.114f * rgb[idx + 2]);
    }
    
    return 0;
}

int simd_gaussian_blur_5x5(const uint8_t* input,
                           uint8_t* output,
                           int width, int height,
                           float sigma) {
    if (!input || !output || width <= 0 || height <= 0) return -1;
    
    // 简化的5x5高斯核
    static const float kernel[5][5] = {
        {0.003f, 0.013f, 0.022f, 0.013f, 0.003f},
        {0.013f, 0.059f, 0.097f, 0.059f, 0.013f},
        {0.022f, 0.097f, 0.159f, 0.097f, 0.022f},
        {0.013f, 0.059f, 0.097f, 0.059f, 0.013f},
        {0.003f, 0.013f, 0.022f, 0.013f, 0.003f}
    };
    
    #pragma omp parallel for
    for (int y = 2; y < height - 2; y++) {
        for (int x = 2; x < width - 2; x++) {
            float sum = 0.0f;
            for (int ky = -2; ky <= 2; ky++) {
                for (int kx = -2; kx <= 2; kx++) {
                    sum += input[(y + ky) * width + (x + kx)] * kernel[ky + 2][kx + 2];
                }
            }
            output[y * width + x] = (uint8_t)std::max(0.0f, std::min(255.0f, sum));
        }
    }
    
    return 0;
}

int simd_pipeline_process(const uint8_t* input,
                         uint8_t* output,
                         int width, int height,
                         const SimdPipelineConfig* config) {
    if (!input || !output || !config) return -1;
    
    std::vector<uint8_t> temp1(width * height * 3);
    std::vector<uint8_t> temp2(width * height * 3);
    
    const uint8_t* src = input;
    uint8_t* dst = temp1.data();
    bool use_temp1 = true;
    
    if (config->brightness != 0.0f || config->contrast != 1.0f) {
        simd_adjust_brightness_contrast(src, dst, width, height,
                                    config->brightness, config->contrast);
        src = dst;
        dst = use_temp1 ? temp2.data() : temp1.data();
        use_temp1 = !use_temp1;
    }
    
    if (config->saturation != 1.0f) {
        simd_adjust_saturation(src, dst, width, height, config->saturation);
        src = dst;
        dst = use_temp1 ? temp2.data() : temp1.data();
        use_temp1 = !use_temp1;
    }
    
    if (config->gsdf_lut) {
        simd_gsdf_lut_apply(src, dst, width, height, config->gsdf_lut);
        src = dst;
        dst = use_temp1 ? temp2.data() : temp1.data();
        use_temp1 = !use_temp1;
    }
    
    if (config->enable_bloodless) {
        simd_bloodless_enhance(src, dst, width, height,
                              config->blood_suppress_level,
                              config->tissue_enhance,
                              config->edge_preserve);
        src = dst;
        dst = use_temp1 ? temp2.data() : temp1.data();
        use_temp1 = !use_temp1;
    }
    
    memcpy(output, src, width * height * 3);
    
    return 0;
}
