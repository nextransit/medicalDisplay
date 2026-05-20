/**
 * @file test_gsdf_nist.cpp
 * @brief GSDF LUT vs DICOM Part 14 NIST 参考表对比验证
 *
 * 验证 SDK 的 GSDF 实现与 DICOM Part 14 标准公式的一致性。
 * 参考数据: DICOM Part 14 Eq.7-1 rational function of ln(JND)
 * 亮度范围: 0.05 - 4000 cd/m², JND 范围: 1-1023
 */

#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>

extern "C" {
#include "dicom_gsdf.h"
}

// DICOM Part 14 Table C-1: JND index → Luminance (cd/m²) 的关键参考点
// 由 DICOM Part 14 Eq.7-1 公式计算（Python 双精度验证）
struct NISTRefPoint {
    float jnd_index;
    float luminance;  // cd/m²
};

// 选取 15 个覆盖全范围的参考点，用 DICOM Part 14 Eq.7-1 精确计算
static const NISTRefPoint k_nist_ref[] = {
    {1.0f,     0.0500f},
    {10.0f,    0.0991f},
    {50.0f,    0.5574f},
    {100.0f,   1.8508f},
    {200.0f,   8.2179f},
    {300.0f,   23.3989f},
    {400.0f,   55.3640f},
    {500.0f,   119.1318f},
    {600.0f,   243.0099f},
    {700.0f,   480.4999f},
    {800.0f,   933.1874f},
    {900.0f,   1795.0813f},
    {1000.0f,  3439.1590f},
    {1020.0f,  3916.2622f},
    {1023.0f,  3993.3296f},  // 最大 JND
};

static constexpr int k_num_points = sizeof(k_nist_ref) / sizeof(k_nist_ref[0]);

// ============================================================================
// Test 1: JND→Luminance 对比 DICOM Part 14 标准值 (核心验证)
// ============================================================================

TEST(GsdfNistValidation, JndToLuminanceMatchesDICOM) {
    float max_err_percent = 0.0f;
    int fail_count = 0;

    for (int i = 0; i < k_num_points; i++) {
        float jnd = k_nist_ref[i].jnd_index;
        float expected = k_nist_ref[i].luminance;
        float actual = gsdf_jnd_to_luminance(jnd);

        float rel_err = std::fabs(actual - expected) / expected * 100.0f;
        if (rel_err > max_err_percent) {
            max_err_percent = rel_err;
        }

        // DICOM Part 14 允许的精度: ±1% (实际 float 实现通常 <0.01%)
        if (rel_err > 1.0f) {
            fail_count++;
            ADD_FAILURE() << "JND=" << jnd
                          << " Expected=" << expected
                          << " Actual=" << actual
                          << " Error=" << rel_err << "%";
        }
    }

    printf("\n  GSDF JND→Luminance DICOM Part 14 validation:\n");
    printf("    Max error: %.6f%%\n", max_err_percent);
    printf("    Failures:  %d/%d\n", fail_count, k_num_points);
    printf("    Verdict:   %s\n", fail_count == 0 ? "PASS (DICOM Part 14 compliant)" : "FAIL");

    EXPECT_EQ(fail_count, 0);
    EXPECT_LT(max_err_percent, 1.0f);
}

// ============================================================================
// Test 2: Luminance→JND 往返一致性
// ============================================================================

TEST(GsdfNistValidation, LuminanceJndRoundTrip) {
    // 测试整个亮度范围的往返一致性
    float luminances[] = {0.05f, 0.1f, 0.5f, 1.0f, 10.0f, 100.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f};

    for (float L : luminances) {
        if (L < 0.05f || L > 4000.0f) continue;

        float jnd = gsdf_luminance_to_jnd(L);
        float L_recovered = gsdf_jnd_to_luminance(jnd);

        float rel_err = std::fabs(L_recovered - L) / L * 100.0f;
        EXPECT_LT(rel_err, 1.0f) << "L=" << L << " JND=" << jnd
                                 << " L_recovered=" << L_recovered
                                 << " err=" << rel_err << "%";
    }
}

// ============================================================================
// Test 3: LUT 单调性 (12-bit)
// ============================================================================

TEST(GsdfNistValidation, LUT12bitMonotonic) {
    const int lut_size = 1 << 12;  // 4096
    auto *lut = new float[lut_size];

    int ret = gsdf_generate_lut(lut, lut_size, 12, 0.0f, 4000.0f);
    EXPECT_EQ(ret, 0);

    // 验证单调递增
    for (int i = 1; i < lut_size; i++) {
        EXPECT_GE(lut[i], lut[i-1]) << "LUT non-monotonic at index " << i;
    }

    // 验证范围
    EXPECT_GE(lut[0], 0.0f);
    EXPECT_LE(lut[0], 1.0f);
    EXPECT_GE(lut[lut_size-1], 0.5f);
    EXPECT_LE(lut[lut_size-1], 1.0f);

    delete[] lut;
}

// ============================================================================
// Test 4: 边界条件
// ============================================================================

TEST(GsdfNistValidation, BoundaryConditions) {
    // JND 最小值 → 接近 0.05 cd/m²
    float L_min = gsdf_jnd_to_luminance(GSDF_JND_MIN);
    EXPECT_GE(L_min, 0.04f);
    EXPECT_LE(L_min, 0.06f);

    // JND 最大值 → 接近 4000 cd/m²
    float L_max = gsdf_jnd_to_luminance(GSDF_JND_MAX);
    EXPECT_GE(L_max, 3990.0f);
    EXPECT_LE(L_max, 4005.0f);

    // 越界 clamp (低于 1.0 的 JND 会被 clamp 到 1.0)
    float L_below = gsdf_jnd_to_luminance(0.5f);
    EXPECT_FLOAT_EQ(L_below, gsdf_jnd_to_luminance(GSDF_JND_MIN));

    // 越界 clamp (高于 1023 的 JND 会被 clamp 到 1023)
    float L_above = gsdf_jnd_to_luminance(2000.0f);
    EXPECT_FLOAT_EQ(L_above, gsdf_jnd_to_luminance(GSDF_JND_MAX));
}

// ============================================================================
// Test 5: JND 步进单调性 (相邻 JND 亮度差应 >0)
// ============================================================================

TEST(GsdfNistValidation, JndStepSizes) {
    float prev_L = gsdf_jnd_to_luminance(GSDF_JND_MIN);

    for (float jnd = GSDF_JND_MIN + 1.0f; jnd <= 100.0f; jnd += 1.0f) {
        float L = gsdf_jnd_to_luminance(jnd);
        float delta_L = L - prev_L;
        EXPECT_GT(delta_L, 0.0f) << "JND=" << jnd << " delta_L=" << delta_L;

        prev_L = L;
    }
}

// ============================================================================
// Test 6: 10-bit 和 8-bit LUT 多分辨率一致性
// ============================================================================

TEST(GsdfNistValidation, MultiBitDepthConsistency) {
    auto *lut8 = new float[256];
    auto *lut10 = new float[1024];

    gsdf_generate_lut(lut8, 256, 8, 10.0f, 500.0f);
    gsdf_generate_lut(lut10, 1024, 10, 10.0f, 500.0f);

    // 8-bit 的索引 i 对应 10-bit 的索引 i*4
    for (int i = 0; i < 256; i++) {
        float diff = std::fabs(lut8[i] - lut10[i * 4]);
        EXPECT_LT(diff, 0.01f) << "8-bit[" << i << "]=" << lut8[i]
                                << " vs 10-bit[" << (i*4) << "]=" << lut10[i*4]
                                << " diff=" << diff;
    }

    delete[] lut8;
    delete[] lut10;
}
