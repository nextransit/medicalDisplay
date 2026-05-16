#include "simd_utils.h"

#include <cstring>

#ifdef __AVX2__
#include <immintrin.h>
#endif

#ifdef __SSE2__
#include <emmintrin.h>
#endif

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

// ============================================================================
// Scalar fallback implementations (always available)
// ============================================================================

static void simd_normalize_tensor_scalar(float* data, size_t size,
                                          float mean, float std) {
    const float inv_stddev = 1.0f / std;
    for (size_t i = 0; i < size; ++i) {
        data[i] = (data[i] - mean) * inv_stddev;
    }
}

static void simd_convert_16bit_to_8bit_scalar(const uint16_t* src,
                                               uint8_t* dst, size_t count,
                                               int shift) {
    for (size_t i = 0; i < count; ++i) {
        dst[i] = static_cast<uint8_t>((src[i] >> shift) & 0xFFu);
    }
}

static void simd_apply_gsdf_batch_scalar(const float* input, float* output,
                                          const float* lut, size_t count,
                                          int lut_size) {
    const float scale = (float)(lut_size - 1);
    for (size_t i = 0; i < count; ++i) {
        float v = input[i];
        if (v <= 0.0f) { output[i] = lut[0]; continue; }
        if (v >= 1.0f) { output[i] = lut[lut_size - 1]; continue; }
        float idx = v * scale;
        int lo = (int)idx;
        int hi = lo + 1;
        if (hi >= lut_size) hi = lut_size - 1;
        float frac = idx - (float)lo;
        output[i] = lut[lo] * (1.0f - frac) + lut[hi] * frac;
    }
}

static void simd_pixels_to_hu_batch_scalar(const uint16_t* pixels,
                                            float* hu_values, size_t count,
                                            float slope, float intercept) {
    for (size_t i = 0; i < count; ++i) {
        hu_values[i] = (float)pixels[i] * slope + intercept;
    }
}

// ============================================================================
// SIMD-accelerated implementations
// ============================================================================

// ----- normalize_tensor -----

void simd_normalize_tensor(float* data, size_t size, float mean, float std) {
    if (!data || size == 0) return;
    if (std <= 0.0f) return;

    const float inv_stddev = 1.0f / std;

#if defined(__AVX2__)
    if (size >= 8) {
        // AVX2: 8 floats per vector
        __m256 mean_vec = _mm256_set1_ps(mean);
        __m256 inv_vec = _mm256_set1_ps(inv_stddev);
        size_t i = 0;
        for (; i + 8 <= size; i += 8) {
            __m256 val = _mm256_loadu_ps(data + i);
            val = _mm256_sub_ps(val, mean_vec);
            val = _mm256_mul_ps(val, inv_vec);
            _mm256_storeu_ps(data + i, val);
        }
        // Tail
        for (; i < size; ++i) {
            data[i] = (data[i] - mean) * inv_stddev;
        }
        return;
    }
#elif defined(__SSE2__)
    if (size >= 4) {
        __m128 mean_vec = _mm_set1_ps(mean);
        __m128 inv_vec = _mm_set1_ps(inv_stddev);
        size_t i = 0;
        for (; i + 4 <= size; i += 4) {
            __m128 val = _mm_loadu_ps(data + i);
            val = _mm_sub_ps(val, mean_vec);
            val = _mm_mul_ps(val, inv_vec);
            _mm_storeu_ps(data + i, val);
        }
        for (; i < size; ++i) {
            data[i] = (data[i] - mean) * inv_stddev;
        }
        return;
    }
#elif defined(__ARM_NEON)
    if (size >= 4) {
        float32x4_t mean_vec = vdupq_n_f32(mean);
        float32x4_t inv_vec = vdupq_n_f32(inv_stddev);
        size_t i = 0;
        for (; i + 4 <= size; i += 4) {
            float32x4_t val = vld1q_f32(data + i);
            val = vsubq_f32(val, mean_vec);
            val = vmulq_f32(val, inv_vec);
            vst1q_f32(data + i, val);
        }
        for (; i < size; ++i) {
            data[i] = (data[i] - mean) * inv_stddev;
        }
        return;
    }
#endif

    simd_normalize_tensor_scalar(data, size, mean, std);
}

// ----- convert_16bit_to_8bit -----

void simd_convert_16bit_to_8bit(const uint16_t* src, uint8_t* dst,
                                 size_t count, int shift) {
    if (!src || !dst || count == 0) return;

#if defined(__AVX2__)
    if (count >= 16) {
        __m256i shift_vec = _mm256_set1_epi16((short)shift);
        size_t i = 0;
        for (; i + 16 <= count; i += 16) {
            __m256i val = _mm256_loadu_si256((const __m256i*)(src + i));
            if (shift > 0) val = _mm256_srl_epi16(val, shift_vec);
            val = _mm256_and_si256(val, _mm256_set1_epi16(0xFF));
            // Pack 16x 16-bit -> 16x 8-bit
            __m128i lo = _mm256_castsi256_si128(val);
            __m128i hi = _mm256_extracti128_si256(val, 1);
            __m128i packed = _mm_packus_epi16(lo, hi);
            _mm_storeu_si128((__m128i*)(dst + i), packed);
        }
        for (; i < count; ++i) {
            dst[i] = (uint8_t)((src[i] >> shift) & 0xFFu);
        }
        return;
    }
#elif defined(__SSE2__)
    if (count >= 8) {
        __m128i shift_vec = _mm_set1_epi16((short)shift);
        size_t i = 0;
        for (; i + 8 <= count; i += 8) {
            __m128i val = _mm_loadu_si128((const __m128i*)(src + i));
            if (shift > 0) val = _mm_srl_epi16(val, shift_vec);
            val = _mm_and_si128(val, _mm_set1_epi16(0xFF));
            __m128i packed = _mm_packus_epi16(val, val);
            _mm_storel_epi64((__m128i*)(dst + i), packed);
        }
        for (; i < count; ++i) {
            dst[i] = (uint8_t)((src[i] >> shift) & 0xFFu);
        }
        return;
    }
#elif defined(__ARM_NEON)
    if (count >= 8) {
        int16x8_t shift_vec = vdupq_n_s16((int16_t)shift);
        size_t i = 0;
        for (; i + 8 <= count; i += 8) {
            uint16x8_t val = vld1q_u16(src + i);
            if (shift > 0) val = vshlq_u16(val, vnegq_s16(shift_vec));
            val = vandq_u16(val, vdupq_n_u16(0xFF));
            uint8x8_t packed = vmovn_u16(val);
            vst1_u8(dst + i, packed);
        }
        for (; i < count; ++i) {
            dst[i] = (uint8_t)((src[i] >> shift) & 0xFFu);
        }
        return;
    }
#endif

    simd_convert_16bit_to_8bit_scalar(src, dst, count, shift);
}

// ----- apply_gsdf_batch -----

void simd_apply_gsdf_batch(const float* input, float* output,
                            const float* lut, size_t count, int lut_size) {
    if (!input || !output || !lut || count == 0 || lut_size <= 0) return;
    simd_apply_gsdf_batch_scalar(input, output, lut, count, lut_size);
}

// ----- pixels_to_hu_batch -----

void simd_pixels_to_hu_batch(const uint16_t* pixels, float* hu_values,
                              size_t count, float slope, float intercept) {
    if (!pixels || !hu_values || count == 0) return;

#if defined(__AVX2__)
    if (count >= 16) {
        __m256 slope_vec = _mm256_set1_ps(slope);
        __m256 int_vec = _mm256_set1_ps(intercept);
        size_t i = 0;
        for (; i + 8 <= count; i += 8) {
            __m128i px = _mm_loadu_si128((const __m128i*)(pixels + i));
            __m256i px32 = _mm256_cvtepu16_epi32(px);
            __m256 fx = _mm256_cvtepi32_ps(px32);
            fx = _mm256_fmadd_ps(fx, slope_vec, int_vec);
            _mm256_storeu_ps(hu_values + i, fx);
        }
        for (; i < count; ++i) {
            hu_values[i] = (float)pixels[i] * slope + intercept;
        }
        return;
    }
#elif defined(__SSE2__)
    if (count >= 8) {
        __m128 slope_vec = _mm_set1_ps(slope);
        __m128 int_vec = _mm_set1_ps(intercept);
        size_t i = 0;
        for (; i + 4 <= count; i += 4) {
            __m128i px = _mm_loadl_epi64((const __m128i*)(pixels + i));
            __m128i px32 = _mm_unpacklo_epi16(px, _mm_setzero_si128());
            __m128 fx = _mm_cvtepi32_ps(px32);
            fx = _mm_add_ps(_mm_mul_ps(fx, slope_vec), int_vec);
            _mm_storeu_ps(hu_values + i, fx);
        }
        for (; i < count; ++i) {
            hu_values[i] = (float)pixels[i] * slope + intercept;
        }
        return;
    }
#elif defined(__ARM_NEON)
    if (count >= 4) {
        float32x4_t slope_vec = vdupq_n_f32(slope);
        float32x4_t int_vec = vdupq_n_f32(intercept);
        size_t i = 0;
        for (; i + 4 <= count; i += 4) {
            uint16x4_t px = vld1_u16(pixels + i);
            uint32x4_t px32 = vmovl_u16(px);
            float32x4_t fx = vcvtq_f32_u32(px32);
            fx = vmlaq_f32(int_vec, fx, slope_vec);
            vst1q_f32(hu_values + i, fx);
        }
        for (; i < count; ++i) {
            hu_values[i] = (float)pixels[i] * slope + intercept;
        }
        return;
    }
#endif

    simd_pixels_to_hu_batch_scalar(pixels, hu_values, count, slope, intercept);
}
