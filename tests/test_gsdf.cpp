/**
 * @file test_gsdf.cpp
 * @brief GSDF mathematics and LUT validation tests
 */

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

extern "C" {
#include "dicom_gsdf.h"
}

namespace {

TEST(GsdfMathTest, JndIsMonotonicAcrossLuminanceRange) {
    const float low = gsdf_luminance_to_jnd(0.5f);
    const float mid = gsdf_luminance_to_jnd(50.0f);
    const float high = gsdf_luminance_to_jnd(500.0f);

    EXPECT_LE(low, mid);
    EXPECT_LE(mid, high);
}

TEST(GsdfMathTest, JndRoundTripRemainsStable) {
    constexpr float target_luminance = 180.0f;
    const float jnd = gsdf_luminance_to_jnd(target_luminance);
    const float roundtrip = gsdf_jnd_to_luminance(jnd);

    EXPECT_TRUE(std::isfinite(jnd));
    EXPECT_NEAR(target_luminance, roundtrip, 5.0f);
}

TEST(GsdfMathTest, GeneratedLutIsNormalizedAndValid) {
    std::vector<float> lut(1024);
    ASSERT_EQ(0, gsdf_generate_lut(lut.data(), static_cast<int>(lut.size()), 10, 15.0f, 600.0f));

    EXPECT_GE(lut.front(), 0.0f);
    EXPECT_LE(lut.back(), 1.0f);
    for (size_t i = 1; i < lut.size(); ++i) {
        EXPECT_GE(lut[i], lut[i - 1]);
    }

    EXPECT_TRUE(gsdf_validate_lut(lut.data(), static_cast<int>(lut.size()), 1e-4f));
    EXPECT_GE(gsdf_calculate_delta_jnd(lut.data(), static_cast<int>(lut.size())), 0.0f);
}

TEST(GsdfMathTest, PValueRoundTripAndAccuracyStayFinite) {
    const float pvalue = gsdf_calculate_pvalue(240.0f, 10.0f);
    const float luminance = gsdf_pvalue_to_luminance(pvalue, 10.0f);
    const float accuracy = gsdf_measure_jnd_accuracy(luminance, gsdf_luminance_to_jnd(241.0f), 10.0f);

    EXPECT_GE(pvalue, 0.0f);
    EXPECT_LE(pvalue, 1.0f);
    EXPECT_GT(luminance, 0.0f);
    EXPECT_TRUE(std::isfinite(accuracy));
}

}  // namespace
