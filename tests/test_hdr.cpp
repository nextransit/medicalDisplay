/**
 * @file test_hdr.cpp
 * @brief HDR (HLG/PQ) Tone Mapping Tests
 */

#include <gtest/gtest.h>
#include <cmath>

// HLG constants
const float HLG_A = 0.17883277f;
const float HLG_B = 0.28466892f;
const float HLG_C = 0.55991073f;

// PQ constants
const float PQ_M1 = 0.1593017578125f;
const float PQ_M2 = 78.84375f;
const float PQ_C1 = 0.8359375f;
const float PQ_C2 = 18.8515625f;
const float PQ_C3 = 18.6875f;

// HLG OETF
float hlg_oetf(float E) {
    E = std::max(0.0f, std::min(1.0f, E));
    if (E <= 1.0f/12.0f) {
        return std::sqrt(3.0f * E);
    } else {
        return HLG_A * std::log(12.0f * E - HLG_B) + HLG_C;
    }
}

// HLG EOTF
float hlg_eotf(float H) {
    H = std::max(0.0f, std::min(1.0f, H));
    if (H <= 0.5f) {
        return H * H / 3.0f;
    } else {
        float x = (H - HLG_C) / HLG_A;
        return (std::exp(x) + HLG_B) / 12.0f;
    }
}

// ============================================================================
// HLG Tests
// ============================================================================

class HLGTest : public ::testing::Test {};

TEST_F(HLGTest, OETFZero) {
    EXPECT_FLOAT_EQ(hlg_oetf(0.0f), 0.0f);
}

TEST_F(HLGTest, OETFOne) {
    float result = hlg_oetf(1.0f);
    EXPECT_GE(result, 0.9f);
    EXPECT_LE(result, 1.0f);
}

TEST_F(HLGTest, EOTFZero) {
    EXPECT_FLOAT_EQ(hlg_eotf(0.0f), 0.0f);
}

TEST_F(HLGTest, EOTFOne) {
    float result = hlg_eotf(1.0f);
    EXPECT_GE(result, 0.9f);
    EXPECT_LE(result, 1.0f);
}

TEST_F(HLGTest, RoundTrip) {
    for (float E = 0.0f; E <= 1.0f; E += 0.1f) {
        float H = hlg_oetf(E);
        float recovered_E = hlg_eotf(H);
        EXPECT_NEAR(E, recovered_E, 0.01f) << "Failed at E=" << E;
    }
}

TEST_F(HLGTest, MonotonicOETF) {
    float prev = -1.0f;
    for (float E = 0.0f; E <= 1.0f; E += 0.01f) {
        float H = hlg_oetf(E);
        EXPECT_GE(H, prev);
        prev = H;
    }
}

TEST_F(HLGTest, MonotonicEOTF) {
    float prev = -1.0f;
    for (float H = 0.0f; H <= 1.0f; H += 0.01f) {
        float E = hlg_eotf(H);
        EXPECT_GE(E, prev);
        prev = E;
    }
}

// ============================================================================
// PQ (ST.2084) Tests
// ============================================================================

class PQTest : public ::testing::Test {};

TEST_F(PQTest, OETFReferenceWhite) {
    // Reference white (100 cd/m² normalized to 1.0) should map to ~0.5
    float N = 0.5f;  // 100/10000 = 0.01, normalized
    float Y = std::pow((PQ_C1 + std::pow(N, PQ_M1)) / 
              (PQ_C2 - PQ_C3 * std::pow(N, PQ_M1)), 
              PQ_M2);
    EXPECT_GE(Y, 0.4f);
    EXPECT_LE(Y, 0.6f);
}

TEST_F(PQTest, Clamping) {
    // Values outside [0,1] should be clamped
    EXPECT_FLOAT_EQ(hlg_oetf(-0.5f), 0.0f);
    EXPECT_FLOAT_EQ(hlg_oetf(1.5f), 1.0f);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
