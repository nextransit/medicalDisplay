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

void gsdf_apply_lut_batch(const float* input, float* output, int count,
                          const float* lut, int lut_size) {
    if (!input || !output || !lut) return;

    for (int index = 0; index < count; ++index) {
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
