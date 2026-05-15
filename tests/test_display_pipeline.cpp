/**
 * @file test_display_pipeline.cpp
 * @brief End-to-end display pipeline tests
 */

#include <gtest/gtest.h>

#include <vector>

extern "C" {
#include "ai_engine.h"
#include "dicom_gsdf.h"
#include "dicom_reader.h"
}

#include "display_engine_internal.h"
#include "test_helpers.h"

namespace {

TEST(DisplayPipelineTest, DicomToAiToDisplayFlow) {
    auto mono = test_helpers::make_mono16_frame(16, 16, 1200);
    auto ai_config = test_helpers::make_ai_config(16, 16);
    AIEngine* ai = ai_engine_create(&ai_config);
    ASSERT_NE(nullptr, ai);

    AIRecognitionResult recognition{};
    ASSERT_EQ(0, ai_engine_recognize_from_dicom(ai, mono.data(), 16, 16, 12, &recognition));
    EXPECT_GE(recognition.confidence, 0.0f);

    Display_Device display = display_open(nullptr);
    ASSERT_NE(nullptr, display);

    auto display_config = test_helpers::make_display_config();
    display_config.gsdf_profile = display_get_recommended_gsdf(recognition.modality);
    display_config.window_center = recognition.strategy.window_center;
    display_config.window_width = recognition.strategy.window_width;

    ASSERT_EQ(0, display_apply_config(display, &display_config));
    display_set_window_level(display, display_config.window_width, display_config.window_center);

    std::vector<float> gsdf_lut(256);
    ASSERT_EQ(0, gsdf_generate_lut(gsdf_lut.data(), static_cast<int>(gsdf_lut.size()), 8, 8.0f, 500.0f));

    const float pvalue = dicom_apply_modality_and_voi_lut(
        dicom_pixel_to_hu(mono[0], 1.0f, -1024.0f),
        1.0f,
        -1024.0f,
        nullptr,
        0,
        0,
        gsdf_lut.data(),
        static_cast<int>(gsdf_lut.size()),
        8.0f);
    EXPECT_GE(pvalue, 0.0f);

    auto frame = test_helpers::make_rgb_frame(16, 16, 72);
    Display_Frame handle = display_frame_create(display, 16, 16, 0, frame.data());
    ASSERT_NE(nullptr, handle);
    EXPECT_EQ(0, display_present(display, handle));

    Display_State state{};
    display_get_state(display, &state);
    EXPECT_EQ(display_config.gsdf_profile, state.gsdf_profile);
    EXPECT_FLOAT_EQ(display_config.window_width, state.window_width);
    EXPECT_FLOAT_EQ(display_config.window_center, state.window_center);

    display_frame_destroy(handle);
    display_close(display);
    ai_engine_destroy(ai);
}

TEST(DisplayPipelineTest, MetadataOnlyPathConfiguresDisplayWithoutPixelInference) {
    auto ai_config = test_helpers::make_ai_config();
    AIEngine* ai = ai_engine_create(&ai_config);
    ASSERT_NE(nullptr, ai);

    AIRecognitionResult metadata_result{};
    ASSERT_EQ(0, ai_engine_recognize_from_metadata(ai, "MR", "Brain T1", 1, &metadata_result));
    EXPECT_EQ(MODALITY_MR, metadata_result.modality);

    Display_Device display = display_open(nullptr);
    ASSERT_NE(nullptr, display);

    auto config = test_helpers::make_display_config();
    config.gsdf_profile = display_get_recommended_gsdf(metadata_result.modality);
    config.window_center = metadata_result.strategy.window_center;
    config.window_width = metadata_result.strategy.window_width;

    ASSERT_EQ(0, display_apply_config(display, &config));

    Display_State state{};
    display_get_state(display, &state);
    EXPECT_EQ(DISPLAY_GSDF_MR, state.gsdf_profile);
    EXPECT_FLOAT_EQ(metadata_result.strategy.window_center, state.window_center);
    EXPECT_FLOAT_EQ(metadata_result.strategy.window_width, state.window_width);

    display_close(display);
    ai_engine_destroy(ai);
}

}  // namespace
