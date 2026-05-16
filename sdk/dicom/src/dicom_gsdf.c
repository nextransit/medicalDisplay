#include "dicom_gsdf.h"

#include <math.h>
#include <stdlib.h>

static float clampf_local(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static float log_span(void) {
    return log10f((float)(GSDF_L_MAX / GSDF_L_MIN));
}

float gsdf_luminance_to_jnd(float luminance) {
    const float l = clampf_local(luminance, (float)GSDF_L_MIN, (float)GSDF_L_MAX);
    const float normalized = log10f(l / (float)GSDF_L_MIN) / log_span();
    return clampf_local(normalized * (float)GSDF_JND_MAX, (float)GSDF_JND_MIN, (float)GSDF_JND_MAX);
}

float gsdf_jnd_to_luminance(float jnd) {
    const float normalized = clampf_local(
        (jnd - (float)GSDF_JND_MIN) / ((float)GSDF_JND_MAX - (float)GSDF_JND_MIN),
        0.0f,
        1.0f);
    const float luminance = (float)GSDF_L_MIN * powf(10.0f, normalized * log_span());
    return clampf_local(luminance, (float)GSDF_L_MIN, (float)GSDF_L_MAX);
}

float gsdf_calculate_pvalue(float luminance, float ambient_luminance) {
    const float effective_luminance = luminance + 0.1f * ambient_luminance;
    const float jnd = gsdf_luminance_to_jnd(effective_luminance);
    return clampf_local(
        (jnd - (float)GSDF_JND_MIN) / ((float)GSDF_JND_MAX - (float)GSDF_JND_MIN),
        0.0f,
        1.0f);
}

float gsdf_pvalue_to_luminance(float pvalue, float ambient_luminance) {
    const float clamped = clampf_local(pvalue, 0.0f, 1.0f);
    const float jnd = (float)GSDF_JND_MIN + clamped * ((float)GSDF_JND_MAX - (float)GSDF_JND_MIN);
    const float effective_luminance = gsdf_jnd_to_luminance(jnd);
    return clampf_local(effective_luminance - 0.1f * ambient_luminance, (float)GSDF_L_MIN, (float)GSDF_L_MAX);
}

int gsdf_generate_lut(float* lut, int lut_size, int bit_depth,
                      float ambient_luminance, float max_luminance) {
    (void)bit_depth;
    if (!lut || lut_size <= 0) return -1;

    for (int index = 0; index < lut_size; ++index) {
        const float normalized_input = (float)index / (float)(lut_size - 1);
        const float target_luminance = normalized_input * max_luminance;
        lut[index] = gsdf_calculate_pvalue(target_luminance, ambient_luminance);
    }

    return 0;
}

float gsdf_apply_lut(float pixel_value, const float* lut, int lut_size) {
    if (!lut || lut_size <= 0) return pixel_value;
    if (pixel_value <= 0.0f) return lut[0];
    if (pixel_value >= 1.0f) return lut[lut_size - 1];

    const float scaled = pixel_value * (float)(lut_size - 1);
    const int low = (int)floorf(scaled);
    const int high = low + 1;
    const float t = scaled - (float)low;
    return lut[low] + t * (lut[high] - lut[low]);
}

// [P2-OPT] Optimized gsdf_apply_lut_batch with precomputed values and loop unrolling
void gsdf_apply_lut_batch(const float* input, float* output, int count,
                          const float* lut, int lut_size) {
    if (!input || !output || !lut || count <= 0) return;

    // [P2-OPT] Precompute constants outside loop
    const float inv_lut_size_minus_1 = 1.0f / (float)(lut_size - 1);
    const int last_index = lut_size - 1;

    // [P2-OPT] Process 4 elements at a time with manual unrolling
    int index = 0;
    const int unrolled_count = (count / 4) * 4;

    for (; index < unrolled_count; index += 4) {
        // Process 4 pixels
        float scaled0 = input[index + 0] * inv_lut_size_minus_1;
        float scaled1 = input[index + 1] * inv_lut_size_minus_1;
        float scaled2 = input[index + 2] * inv_lut_size_minus_1;
        float scaled3 = input[index + 3] * inv_lut_size_minus_1;

        // Floor + clamp for all 4
        int low0 = (int)(scaled0 >= 0.0f ? scaled0 : 0.0f);
        int low1 = (int)(scaled1 >= 0.0f ? scaled1 : 0.0f);
        int low2 = (int)(scaled2 >= 0.0f ? scaled2 : 0.0f);
        int low3 = (int)(scaled3 >= 0.0f ? scaled3 : 0.0f);

        low0 = low0 < last_index ? low0 : last_index;
        low1 = low1 < last_index ? low1 : last_index;
        low2 = low2 < last_index ? low2 : last_index;
        low3 = low3 < last_index ? low3 : last_index;

        // Get high index and fractional part for all 4
        int high0 = low0 + 1 < lut_size ? low0 + 1 : last_index;
        int high1 = low1 + 1 < lut_size ? low1 + 1 : last_index;
        int high2 = low2 + 1 < lut_size ? low2 + 1 : last_index;
        int high3 = low3 + 1 < lut_size ? low3 + 1 : last_index;

        float t0 = scaled0 - (float)low0;
        float t1 = scaled1 - (float)low1;
        float t2 = scaled2 - (float)low2;
        float t3 = scaled3 - (float)low3;

        // Linear interpolation for all 4
        output[index + 0] = lut[low0] + t0 * (lut[high0] - lut[low0]);
        output[index + 1] = lut[low1] + t1 * (lut[high1] - lut[low1]);
        output[index + 2] = lut[low2] + t2 * (lut[high2] - lut[low2]);
        output[index + 3] = lut[low3] + t3 * (lut[high3] - lut[low3]);
    }

    // Handle remaining elements
    for (; index < count; ++index) {
        output[index] = gsdf_apply_lut(input[index], lut, lut_size);
    }
}

float dicom_apply_modality_and_voi_lut(float hu_value,
                                       float rescale_slope,
                                       float rescale_intercept,
                                       const uint16_t* voi_lut,
                                       int voi_lut_size,
                                       int voi_lut_bits,
                                       const float* gsdf_lut,
                                       int gsdf_lut_size,
                                       float ambient) {
    float pvalue = 0.0f;

    if (voi_lut && voi_lut_size > 0 && voi_lut_bits > 0) {
        const float pixel_value = (hu_value - rescale_intercept) / rescale_slope;
        int voi_index = (int)(pixel_value * (float)(voi_lut_size - 1) + 0.5f);
        voi_index = (voi_index < 0) ? 0 : voi_index;
        voi_index = (voi_index >= voi_lut_size) ? (voi_lut_size - 1) : voi_index;
        pvalue = (float)voi_lut[voi_index] / (float)((1 << voi_lut_bits) - 1);
    } else {
        const float pixel_value = (hu_value - rescale_intercept) / rescale_slope;
        const float normalized = clampf_local((pixel_value + 1024.0f) / 4095.0f, 0.0f, 1.0f);
        pvalue = gsdf_calculate_pvalue(normalized * (float)GSDF_L_MAX, ambient);
    }

    if (gsdf_lut && gsdf_lut_size > 0) {
        pvalue = gsdf_apply_lut(pvalue, gsdf_lut, gsdf_lut_size);
    }

    return clampf_local(pvalue, 0.0f, 1.0f);
}

bool gsdf_validate_lut(const float* lut, int lut_size, float tolerance) {
    if (!lut || lut_size < 2) return false;

    for (int index = 1; index < lut_size; ++index) {
        if (lut[index] + tolerance < lut[index - 1]) {
            return false;
        }
    }

    for (int index = 0; index < lut_size; ++index) {
        if (lut[index] < -tolerance || lut[index] > 1.0f + tolerance) {
            return false;
        }
    }

    return true;
}

float gsdf_measure_jnd_accuracy(float measured_luminance, float target_jnd, float ambient) {
    return gsdf_luminance_to_jnd(measured_luminance + 0.1f * ambient) - target_jnd;
}

float gsdf_calculate_delta_jnd(const float* lut, int lut_size) {
    if (!lut || lut_size <= 0) return 0.0f;

    float max_error = 0.0f;
    for (int index = 0; index < lut_size; ++index) {
        const float pvalue = (float)index / (float)(lut_size - 1);
        const float expected = gsdf_pvalue_to_luminance(pvalue, 0.0f);
        const float actual = gsdf_pvalue_to_luminance(lut[index], 0.0f);
        const float error = fabsf(gsdf_luminance_to_jnd(actual) - gsdf_luminance_to_jnd(expected));
        if (error > max_error) {
            max_error = error;
        }
    }
    return max_error;
}
