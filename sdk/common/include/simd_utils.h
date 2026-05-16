#ifndef SIMD_UTILS_H
#define SIMD_UTILS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void simd_normalize_tensor(float* data, size_t size, float mean, float std);

void simd_convert_16bit_to_8bit(const uint16_t* src, uint8_t* dst, size_t count, int shift);

void simd_apply_gsdf_batch(const float* input, float* output, const float* lut, size_t count, int lut_size);

void simd_pixels_to_hu_batch(const uint16_t* pixels, float* hu_values, size_t count, float slope, float intercept);

#ifdef __cplusplus
}
#endif

#endif
