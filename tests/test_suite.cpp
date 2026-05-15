/**
 * @file test_suite.cpp
 * @brief Core unit tests for Medical Display SDK
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cmath>
#include <vector>

extern "C" {
#include "ai_engine.h"
#include "dicom_gsdf.h"
#include "dicom_reader.h"
}

#include "display_engine_internal.h"
#include "test_helpers.h"

namespace {

class AIEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        config = test_helpers::make_ai_config(8, 8);
        engine = ai_engine_create(&config);
        ASSERT_NE(nullptr, engine);
    }

    void TearDown() override {
        ai_engine_destroy(engine);
        engine = nullptr;
    }

    AIEngineConfig config{};
    AIEngine* engine = nullptr;
};

TEST_F(AIEngineTest, CreateAndResetStats) {
    uint64_t total = 123;
    float avg = 9.0f;

    ai_engine_reset_stats(engine);
    ai_engine_get_stats(engine, &total, &avg);

    EXPECT_EQ(0u, total);
    EXPECT_FLOAT_EQ(0.0f, avg);
}

TEST_F(AIEngineTest, RecognizeFromMetadataUsesStrategiesAndCaching) {
    AIRecognitionResult first{};
    ASSERT_EQ(0, ai_engine_recognize_from_metadata(engine, "CT", "Chest", 2, &first));

    EXPECT_EQ(MODALITY_CT, first.modality);
    EXPECT_GT(first.confidence, 0.9f);
    EXPECT_FLOAT_EQ(40.0f, first.strategy.window_center);
    EXPECT_FLOAT_EQ(400.0f, first.strategy.window_width);
    EXPECT_EQ(2, first.body_part);
    EXPECT_GT(first.inference_time_ms, 0.0f);

    AIRecognitionResult second{};
    ASSERT_EQ(0, ai_engine_recognize_from_metadata(engine, "CT", "Chest", 2, &second));

    EXPECT_EQ(MODALITY_CT, second.modality);
    EXPECT_FLOAT_EQ(0.1f, second.inference_time_ms);
    EXPECT_FLOAT_EQ(first.strategy.window_center, second.strategy.window_center);
    EXPECT_FLOAT_EQ(first.strategy.window_width, second.strategy.window_width);
}

TEST_F(AIEngineTest, RecognizeFromMetadataSupportsNullStrings) {
    AIRecognitionResult result{};

    EXPECT_EQ(0, ai_engine_recognize_from_metadata(engine, nullptr, nullptr, 0, &result));
    EXPECT_EQ(MODALITY_UNKNOWN, result.modality);
    EXPECT_GT(result.confidence, 0.0f);
}

TEST_F(AIEngineTest, RecognizeFromImageUpdatesStats) {
    auto frame = test_helpers::make_rgb_frame(8, 8, 96);
    AIRecognitionResult result{};

    EXPECT_EQ(0, ai_engine_recognize_from_image(engine, frame.data(), 8, 8, 3, &result));
    EXPECT_GE(result.confidence, 0.0f);
    EXPECT_LE(result.confidence, 1.0f);
    EXPECT_GT(result.inference_time_ms, 0.0f);

    uint64_t total = 0;
    float avg = 0.0f;
    ai_engine_get_stats(engine, &total, &avg);

    EXPECT_EQ(1u, total);
    EXPECT_GT(avg, 0.0f);
}

TEST_F(AIEngineTest, RecognizeFromDicomAndBatchProcessing) {
    auto dicom_pixels = test_helpers::make_mono16_frame(8, 8, 1536);
    AIRecognitionResult dicom_result{};

    EXPECT_EQ(0, ai_engine_recognize_from_dicom(engine, dicom_pixels.data(), 8, 8, 12, &dicom_result));
    EXPECT_GE(dicom_result.confidence, 0.0f);
    EXPECT_LE(dicom_result.confidence, 1.0f);

    auto frame_a = test_helpers::make_rgb_frame(8, 8, 32);
    auto frame_b = test_helpers::make_rgb_frame(8, 8, 180);
    const uint8_t* frames[] = {frame_a.data(), frame_b.data()};
    AIRecognitionResult batch_results[2]{};

    EXPECT_EQ(2, ai_engine_recognize_batch(engine, frames, 2, batch_results));
    EXPECT_GE(batch_results[0].confidence, 0.0f);
    EXPECT_GE(batch_results[1].confidence, 0.0f);
}

TEST_F(AIEngineTest, ReloadModelPathPersistsAndBadArgsFail) {
    EXPECT_EQ(0, ai_engine_reload_model(engine, "/tmp/fake-model.bin"));

    AIRecognitionResult result{};
    EXPECT_EQ(-1, ai_engine_recognize_from_image(nullptr, nullptr, 0, 0, 0, &result));
    EXPECT_EQ(-1, ai_engine_recognize_from_metadata(engine, "CT", "Series", 0, nullptr));
}

TEST(DisplayEngineUnitTest, GsdfRoundTripAndLutAreMonotonic) {
    constexpr float luminance = 120.0f;
    const float jnd = display_luminance_to_jnd(luminance);
    const float recovered = display_jnd_to_luminance(jnd);

    EXPECT_TRUE(std::isfinite(jnd));
    EXPECT_TRUE(std::isfinite(recovered));
    EXPECT_NEAR(luminance, recovered, 2.0f);

    std::vector<uint16_t> lut(1u << 12u);
    display_generate_gsdf_lut(8.0f, 500.0f, 12, lut.data());

    EXPECT_EQ(0u, lut.front());
    EXPECT_EQ(4095u, lut.back());
    for (size_t i = 1; i < lut.size(); ++i) {
        EXPECT_GE(lut[i], lut[i - 1]);
    }
}

TEST(DisplayEngineUnitTest, DeviceLifecycleConfigAndState) {
    Display_Device devices[2]{};
    ASSERT_EQ(1, display_enumerate_devices(devices, 2));

    Display_Device device = display_open(nullptr);
    ASSERT_NE(nullptr, device);

    Display_Capabilities caps{};
    display_get_capabilities(device, &caps);
    EXPECT_GE(caps.max_width, 1920u);
    EXPECT_GE(caps.max_height, 1080u);
    EXPECT_TRUE(caps.supports_hdr);

    auto config = test_helpers::make_display_config();
    ASSERT_EQ(0, display_apply_config(device, &config));

    Display_State state{};
    display_get_state(device, &state);
    EXPECT_EQ(config.color_space, state.color_space);
    EXPECT_EQ(config.gsdf_profile, state.gsdf_profile);
    EXPECT_FLOAT_EQ(config.gamma, state.gamma);

    display_switch_colorspace(device, DISPLAY_COLORSPACE_DCI_P3);
    display_switch_gsdf(device, DISPLAY_GSDF_MR);

    const float hdr_metadata[] = {1200.0f, 400.0f, 0.15f};
    display_switch_hdr(device, DISPLAY_HDR_HDR10, hdr_metadata);
    display_set_window_level(device, 1600.0f, 80.0f);
    display_set_local_enhancement(device, DISPLAY_ENHANCE_BONE, true);
    display_get_state(device, &state);

    EXPECT_EQ(DISPLAY_COLORSPACE_DCI_P3, state.color_space);
    EXPECT_EQ(DISPLAY_GSDF_MR, state.gsdf_profile);
    EXPECT_EQ(DISPLAY_HDR_HDR10, state.hdr_mode);
    EXPECT_FLOAT_EQ(1200.0f, state.hdr_max_luminance);
    EXPECT_FLOAT_EQ(1600.0f, state.window_width);
    EXPECT_FLOAT_EQ(80.0f, state.window_center);
    EXPECT_EQ(DISPLAY_ENHANCE_BONE, state.enhance_type);

    std::vector<uint16_t> gsdf_lut(4096, 2048);
    Display_LUT lut{static_cast<int>(gsdf_lut.size()), gsdf_lut.data()};
    EXPECT_EQ(0, display_load_lut(device, &lut));

    std::vector<uint16_t> lut3d(4u * 4u * 4u * 4u, 1024);
    Display_3DLUT three_d{4, 4, 4, lut3d.data()};
    EXPECT_EQ(0, display_load_3d_lut(device, &three_d));

    auto frame_bytes = test_helpers::make_rgb_frame(8, 8, 50);
    Display_Frame frame = display_frame_create(device, 8, 8, 0, frame_bytes.data());
    ASSERT_NE(nullptr, frame);
    EXPECT_EQ(0, display_present(device, frame));
    display_present_async(device, frame, nullptr);
    display_wait(device, nullptr);
    display_frame_destroy(frame);

    std::vector<float> uniformity(9);
    display_measure_uniformity(device, 3, uniformity.data());
    for (float value : uniformity) {
        EXPECT_FLOAT_EQ(500.0f, value);
    }

    EXPECT_EQ(0, display_calibrate(device, nullptr));
    EXPECT_STREQ("1.0.0", display_engine_version());

    display_close(device);
}

TEST(DisplayEngineUnitTest, RecommendedGsdfProfilesMatchModalities) {
    EXPECT_EQ(DISPLAY_GSDF_CT, display_get_recommended_gsdf(MODALITY_CT));
    EXPECT_EQ(DISPLAY_GSDF_MR, display_get_recommended_gsdf(MODALITY_MR));
    EXPECT_EQ(DISPLAY_GSDF_DR, display_get_recommended_gsdf(MODALITY_CR));
    EXPECT_EQ(DISPLAY_GSDF_US, display_get_recommended_gsdf(MODALITY_US));
    EXPECT_EQ(DISPLAY_GSDF_SURGICAL, display_get_recommended_gsdf(MODALITY_SURGICAL));
}

TEST(DicomReaderUnitTest, OpenAndInspectMinimalDicom) {
    const std::string path = test_helpers::create_minimal_dicom_file();
    ASSERT_FALSE(path.empty());

    DICOM_Dataset dataset = dicom_open(path.c_str());
    ASSERT_NE(nullptr, dataset);

    EXPECT_EQ(DICOM_TRANSFER_IMPLICIT_VR_LITTLE_ENDIAN, dicom_get_transfer_syntax(dataset));

    DICOM_PixelData pixel_info{};
    ASSERT_EQ(0, dicom_read_pixels(dataset, &pixel_info));
    EXPECT_EQ(512u, pixel_info.rows);
    EXPECT_EQ(512u, pixel_info.columns);
    EXPECT_EQ(12u, pixel_info.bits_stored);
    EXPECT_STREQ("MONOCHROME2", pixel_info.photometric_interp);

    float center = 0.0f;
    float width = 0.0f;
    dicom_read_window_level(dataset, &center, &width);
    EXPECT_FLOAT_EQ(40.0f, center);
    EXPECT_FLOAT_EQ(400.0f, width);

    DICOM_Metadata metadata{};
    dicom_extract_metadata(dataset, &metadata);
    EXPECT_STREQ("CT", metadata.modality);
    EXPECT_FLOAT_EQ(1.0f, metadata.rescale_slope);
    EXPECT_FLOAT_EQ(-1024.0f, metadata.rescale_intercept);

    EXPECT_TRUE(dicom_is_monochrome(dataset));
    EXPECT_FALSE(dicom_needs_inversion(dataset));
    EXPECT_EQ(1, dicom_get_frame_count(dataset));

    DICOM_PixelData frame_info{};
    EXPECT_EQ(0, dicom_read_frame(dataset, 0, &frame_info));
    EXPECT_EQ(-1, dicom_read_frame(dataset, 1, &frame_info));

    dicom_close(dataset);
    std::remove(path.c_str());
}

TEST(DicomReaderUnitTest, SopClassHuAndModalityLutBehaviors) {
    EXPECT_STREQ("CT", dicom_sop_class_to_modality("1.2.840.10008.5.1.4.1.1.2"));
    EXPECT_STREQ("MR", dicom_sop_class_to_modality("1.2.840.10008.5.1.4.1.1.4"));
    EXPECT_STREQ("CR", dicom_sop_class_to_modality("1.2.840.10008.5.1.4.1.1.1"));
    EXPECT_STREQ("OT", dicom_sop_class_to_modality("1.2.3.4"));

    EXPECT_FLOAT_EQ(-1024.0f, dicom_pixel_to_hu(0, 1.0f, -1024.0f));
    EXPECT_FLOAT_EQ(0.0f, dicom_pixel_to_hu(1024, 1.0f, -1024.0f));
    EXPECT_FLOAT_EQ(1536.0f, dicom_pixel_to_hu(512, 2.0f, 512.0f));

    const uint16_t modality_lut[] = {0, 100, 250, 400};
    EXPECT_EQ(0, dicom_apply_modality_lut(-5, modality_lut, 4));
    EXPECT_EQ(250, dicom_apply_modality_lut(2, modality_lut, 4));
    EXPECT_EQ(400, dicom_apply_modality_lut(99, modality_lut, 4));
    EXPECT_EQ(7, dicom_apply_modality_lut(7, nullptr, 0));
}

TEST(ColorAndGsdfUnitTest, PValueAndBatchLutApplication) {
    const float pvalue = gsdf_calculate_pvalue(350.0f, 12.0f);
    EXPECT_GE(pvalue, 0.0f);
    EXPECT_LE(pvalue, 1.0f);

    const float luminance = gsdf_pvalue_to_luminance(pvalue, 12.0f);
    EXPECT_GT(luminance, 0.0f);

    std::vector<float> lut(256);
    ASSERT_EQ(0, gsdf_generate_lut(lut.data(), static_cast<int>(lut.size()), 8, 10.0f, 500.0f));
    EXPECT_TRUE(gsdf_validate_lut(lut.data(), static_cast<int>(lut.size()), 1e-4f));

    std::vector<float> input{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    std::vector<float> output(input.size(), 0.0f);
    gsdf_apply_lut_batch(input.data(), output.data(), static_cast<int>(input.size()), lut.data(), static_cast<int>(lut.size()));

    EXPECT_LE(output.front(), output.back());
    for (size_t i = 1; i < output.size(); ++i) {
        EXPECT_GE(output[i], output[i - 1]);
    }

    const float accuracy = gsdf_measure_jnd_accuracy(300.0f, 15.0f, 8.0f);
    EXPECT_TRUE(std::isfinite(accuracy));

    const float delta_jnd = gsdf_calculate_delta_jnd(lut.data(), static_cast<int>(lut.size()));
    EXPECT_GE(delta_jnd, 0.0f);

    const uint16_t voi_lut[] = {0, 256, 1024, 2048, 4095};
    const float mapped = dicom_apply_modality_and_voi_lut(
        200.0f, 1.0f, -1024.0f, voi_lut, 5, 12, lut.data(), static_cast<int>(lut.size()), 5.0f);
    EXPECT_GE(mapped, 0.0f);
}

}  // namespace
