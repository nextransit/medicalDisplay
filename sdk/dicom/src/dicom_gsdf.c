#include "dicom_gsdf.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static inline float clampf_local(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

/* ------------------------------------------------------------------ */
/*  Eq.7-1  JND → Luminance  (rational function of ln(j))              */
/*                                                                      */
/*  log10(L) =  (a + c·y + e·y² + g·y³ + m·y⁴)                        */
/*            / (1 + b·y + d·y² + f·y³ + h·y⁴ + k·y⁵)                 */
/*  where y = ln(JND)                                                  */
/* ------------------------------------------------------------------ */

float gsdf_jnd_to_luminance(float jnd) {
    float j = clampf_local(jnd, GSDF_JND_MIN, GSDF_JND_MAX);
    float y  = logf(j);
    float y2 = y  * y;
    float y3 = y2 * y;
    float y4 = y3 * y;
    float y5 = y4 * y;

    float num = GSDF_EQ71_A
              + GSDF_EQ71_C * y
              + GSDF_EQ71_E * y2
              + GSDF_EQ71_G * y3
              + GSDF_EQ71_M * y4;

    float den = 1.0f
              + GSDF_EQ71_B * y
              + GSDF_EQ71_D * y2
              + GSDF_EQ71_F * y3
              + GSDF_EQ71_H * y4
              + GSDF_EQ71_K * y5;

    float log10L = num / (den + 1e-12f);
    return powf(10.0f, log10L);
}

/* ------------------------------------------------------------------ */
/*  Eq.7-2  Luminance → JND  (direct degree-8 polynomial of log10(L))  */
/*                                                                      */
/*  j(L) = A + B·x + C·x² + D·x³ + E·x⁴ + F·x⁵ + G·x⁶ + H·x⁷ + I·x⁸  */
/*  where x = log10(L)                                                 */
/* ------------------------------------------------------------------ */

float gsdf_luminance_to_jnd(float luminance) {
    float L = clampf_local(luminance, GSDF_L_MIN, GSDF_L_MAX);
    float x  = log10f(L);
    float x2 = x  * x;
    float x3 = x2 * x;
    float x4 = x3 * x;
    float x5 = x4 * x;
    float x6 = x5 * x;
    float x7 = x6 * x;
    float x8 = x7 * x;

    float jnd = GSDF_EQ72_A
              + GSDF_EQ72_B * x
              + GSDF_EQ72_C * x2
              + GSDF_EQ72_D * x3
              + GSDF_EQ72_E * x4
              + GSDF_EQ72_F * x5
              + GSDF_EQ72_G * x6
              + GSDF_EQ72_H * x7
              + GSDF_EQ72_I * x8;

    return clampf_local(jnd, GSDF_JND_MIN, GSDF_JND_MAX);
}

/* ------------------------------------------------------------------ */
/*  Fast LUT-backed L→JND  (uses Eq.7-2 for init, binary-search for    */
/*  interpolation within the sampled LUT)                               */
/* ------------------------------------------------------------------ */

#define GSDF_PRECOMPUTED_LUT_SIZE 16384

static float gsdf_precomputed_jnd_lut[GSDF_PRECOMPUTED_LUT_SIZE];
static int   gsdf_precomputed_ready = 0;

static void gsdf_precomputed_init(void) {
    if (gsdf_precomputed_ready) return;

    float lo = GSDF_JND_MIN;
    float hi = GSDF_JND_MAX;

    for (int i = 0; i < GSDF_PRECOMPUTED_LUT_SIZE; ++i) {
        float jnd = lo + (hi - lo) * (float)i
                      / (float)(GSDF_PRECOMPUTED_LUT_SIZE - 1);
        gsdf_precomputed_jnd_lut[i] = gsdf_jnd_to_luminance(jnd);
    }
    gsdf_precomputed_ready = 1;
}

float gsdf_luminance_to_jnd_fast(float luminance) {
    float L = clampf_local(luminance, GSDF_L_MIN, GSDF_L_MAX);

    if (!gsdf_precomputed_ready) gsdf_precomputed_init();

    /* binary search in the sampled L→JND LUT */
    int lo = 0;
    int hi = GSDF_PRECOMPUTED_LUT_SIZE - 1;

    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (gsdf_precomputed_jnd_lut[mid] < L)
            lo = mid;
        else
            hi = mid;
    }

    /* linear interpolation between lo..hi */
    float t = (L - gsdf_precomputed_jnd_lut[lo])
            / (gsdf_precomputed_jnd_lut[hi] - gsdf_precomputed_jnd_lut[lo] + 1e-12f);

    float jnd_lo = GSDF_JND_MIN
                 + (GSDF_JND_MAX - GSDF_JND_MIN)
                   * (float)lo / (float)(GSDF_PRECOMPUTED_LUT_SIZE - 1);
    float jnd_hi = GSDF_JND_MIN
                 + (GSDF_JND_MAX - GSDF_JND_MIN)
                   * (float)hi / (float)(GSDF_PRECOMPUTED_LUT_SIZE - 1);

    return jnd_lo + t * (jnd_hi - jnd_lo);
}

/* ------------------------------------------------------------------ */
/*  P-Value helpers (DICOM Part 14 Section 10.4)                       */
/* ------------------------------------------------------------------ */

float gsdf_calculate_pvalue(float luminance, float ambient_luminance) {
    float effective = luminance + 0.1f * ambient_luminance;
    float jnd = gsdf_luminance_to_jnd_fast(effective);
    return clampf_local(
        (jnd - GSDF_JND_MIN) / (GSDF_JND_MAX - GSDF_JND_MIN),
        0.0f, 1.0f);
}

float gsdf_pvalue_to_luminance(float pvalue, float ambient_luminance) {
    float clamped = clampf_local(pvalue, 0.0f, 1.0f);
    float jnd = GSDF_JND_MIN
              + clamped * (GSDF_JND_MAX - GSDF_JND_MIN);
    float effective = gsdf_jnd_to_luminance(jnd);
    return clampf_local(effective - 0.1f * ambient_luminance,
                        GSDF_L_MIN, GSDF_L_MAX);
}

/* ------------------------------------------------------------------ */
/*  LUT generation / apply / validate / metrics                        */
/* ------------------------------------------------------------------ */

int gsdf_generate_lut(float *lut, int lut_size, int bit_depth,
                       float ambient_luminance, float max_luminance) {
    (void)bit_depth;
    if (!lut || lut_size <= 0) return -1;

    for (int index = 0; index < lut_size; ++index) {
        float normalized_input = (float)index / (float)(lut_size - 1);
        float target = normalized_input * max_luminance;
        lut[index] = gsdf_calculate_pvalue(target, ambient_luminance);
    }
    return 0;
}

float gsdf_apply_lut(float pixel_value, const float *lut, int lut_size) {
    if (!lut || lut_size <= 0) return pixel_value;
    if (pixel_value <= 0.0f) return lut[0];
    if (pixel_value >= 1.0f) return lut[lut_size - 1];

    float scaled = pixel_value * (float)(lut_size - 1);
    int   low    = (int)floorf(scaled);
    int   high   = low + 1;
    if (high >= lut_size) return lut[lut_size - 1];

    float frac = scaled - (float)low;
    return lut[low] * (1.0f - frac) + lut[high] * frac;
}

void gsdf_apply_lut_batch(const float *input, float *output, int count,
                           const float *lut, int lut_size) {
    if (!input || !output || !lut || lut_size <= 0) return;
    for (int i = 0; i < count; ++i) {
        output[i] = gsdf_apply_lut(input[i], lut, lut_size);
    }
}

bool gsdf_validate_lut(const float *lut, int lut_size, float tolerance) {
    if (!lut || lut_size < 2) return false;
    for (int i = 1; i < lut_size; ++i) {
        if (lut[i] < lut[i - 1] - tolerance) return false;
    }
    return true;
}

float gsdf_measure_jnd_accuracy(float luminance, float reference_jnd,
                                 float ambient_luminance) {
    float effective = luminance + 0.1f * ambient_luminance;
    float jnd = gsdf_luminance_to_jnd_fast(effective);
    return clampf_local(fabsf(jnd - reference_jnd) / fmaxf(reference_jnd, 1e-6f),
                         0.0f, 100.0f);
}

float gsdf_calculate_delta_jnd(const float *lut, int lut_size) {
    if (!lut || lut_size < 2) return 0.0f;
    float prev_jnd  = gsdf_calculate_pvalue(0.0f, 0.0f);
    float max_delta = 0.0f;

    for (int i = 1; i < lut_size; ++i) {
        float pvalue    = lut[i];
        float luminance = gsdf_pvalue_to_luminance(pvalue, 0.0f);
        float jnd       = gsdf_luminance_to_jnd_fast(luminance);
        float delta     = jnd - prev_jnd;
        if (delta > max_delta) max_delta = delta;
        prev_jnd = jnd;
    }
    return max_delta;
}

/* ------------------------------------------------------------------ */
/*  DICOM Modality + VOI LUT → display pipeline                        */
/* ------------------------------------------------------------------ */

float dicom_apply_modality_and_voi_lut(
        float hu_value,
        float rescale_slope,
        float rescale_intercept,
        const uint16_t *voi_lut,
        int   voi_lut_size,
        int   voi_lut_bits,
        const float *gsdf_lut,
        int   gsdf_lut_size,
        float ambient) {

    float hu_converted = hu_value * rescale_slope + rescale_intercept;

    float voi_value;
    if (voi_lut && voi_lut_size > 0 && voi_lut_bits > 0) {
        int idx = (int)clampf_local(hu_converted, 0.0f,
                                      (float)(voi_lut_size - 1));
        voi_value = (float)voi_lut[idx] / (float)((1 << voi_lut_bits) - 1);
    } else {
        voi_value = clampf_local(hu_converted / 4096.0f + 0.5f, 0.0f, 1.0f);
    }

    float pvalue;
    if (gsdf_lut && gsdf_lut_size > 0) {
        pvalue = gsdf_apply_lut(voi_value, gsdf_lut, gsdf_lut_size);
    } else {
        float jnd = GSDF_JND_MIN
                  + voi_value * (GSDF_JND_MAX - GSDF_JND_MIN);
        float luminance = gsdf_jnd_to_luminance(jnd);
        pvalue = gsdf_calculate_pvalue(luminance, ambient);
    }
    return clampf_local(pvalue, 0.0f, 1.0f);
}
