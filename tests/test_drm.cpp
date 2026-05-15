/**
 * @file test_drm.cpp
 * @brief DRM device and color pipeline tests
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <array>
#include <vector>

extern "C" {
#include "drm_display.h"
}

#include "drm_mock.h"

namespace {

class DrmTest : public ::testing::Test {
protected:
    void SetUp() override {
        drm_mock_reset();
    }
};

TEST_F(DrmTest, EnumeratesResourcesAndConnectorModes) {
    DRM_Resources resources{};
    ASSERT_EQ(0, drm_get_resources(10, &resources));

    EXPECT_EQ(2, resources.encoder_count);
    EXPECT_EQ(2, resources.connector_count);
    EXPECT_EQ(1, resources.crtc_count);
    EXPECT_EQ(1, resources.plane_count);
    EXPECT_GE(resources.max_width, 1920u);

    uint32_t encoder_id = 0;
    bool connected = false;
    DRM_Mode* modes = nullptr;
    int mode_count = 0;
    ASSERT_EQ(0, drm_get_connector(10, resources.connectors[0], &encoder_id, &connected, &modes, &mode_count));

    EXPECT_TRUE(connected);
    EXPECT_EQ(11u, encoder_id);
    ASSERT_EQ(2, mode_count);
    EXPECT_STREQ("1920x1080@60", modes[0].name);

    std::free(modes);
    drm_free_resources(&resources);
}

TEST_F(DrmTest, AppliesLutAndColorBlobs) {
    std::array<uint16_t, 12> rgb_lut{};
    for (size_t index = 0; index < rgb_lut.size(); ++index) {
        rgb_lut[index] = static_cast<uint16_t>(index * 32u);
    }

    uint32_t gamma_blob = 0;
    ASSERT_EQ(0, drm_mode_create_gamma_lut(10, rgb_lut.data(), 4, 12, &gamma_blob));
    EXPECT_NE(0u, gamma_blob);

    const float ctm[] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    uint32_t ctm_blob = 0;
    ASSERT_EQ(0, drm_mode_create_ctm_blob(10, ctm, &ctm_blob));
    EXPECT_NE(0u, ctm_blob);

    ASSERT_EQ(0, drm_mode_set_color(10, 31, DRM_COLORSPACE_DICOM_GSDF, 101, ctm_blob, gamma_blob));
    const DrmMockState* state = drm_mock_state();
    EXPECT_EQ(101u, state->last_set_color_degamma_blob);
    EXPECT_EQ(ctm_blob, state->last_set_color_ctm_blob);
    EXPECT_EQ(gamma_blob, state->last_set_color_gamma_blob);
}

TEST_F(DrmTest, PageFlipAndFormatsWorkAsExpected) {
    uint32_t fb_id = 0;
    const uint32_t handles[] = {1, 0, 0, 0};
    const uint32_t pitches[] = {7680, 0, 0, 0};
    const uint32_t offsets[] = {0, 0, 0, 0};
    ASSERT_EQ(0, drm_add_fb(10, 1920, 1080, drm_format_from_string("XRGB8888"), handles, pitches, offsets, &fb_id));
    EXPECT_NE(0u, fb_id);

    ASSERT_EQ(0, drm_page_flip(10, 31, fb_id, 0, reinterpret_cast<void*>(0x1234)));
    const DrmMockState* state = drm_mock_state();
    EXPECT_EQ(31u, state->last_page_flip_crtc);
    EXPECT_EQ(fb_id, state->last_page_flip_fb);
    EXPECT_EQ(static_cast<uintptr_t>(0x1234), state->last_page_flip_user_data);

    EXPECT_STREQ("XR24", drm_format_to_string(drm_format_from_string("XRGB8888")));
    EXPECT_STREQ("NV12", drm_format_to_string(drm_format_from_string("NV12")));
    EXPECT_STREQ("UNKNOWN", drm_format_to_string(0));
}

}  // namespace
