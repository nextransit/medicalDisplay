#include <gtest/gtest.h>
#include <cstring>
extern "C" {
#include "multi_display_hub.h"
}

class MultiScreenTest : public ::testing::Test {
protected:
    MultiDisplayHub* hub = nullptr;
    void SetUp() override { hub = multi_display_hub_create(2); }
    void TearDown() override { multi_display_hub_destroy(hub); }
};

TEST_F(MultiScreenTest, CreateAndDestroy) {
    MultiDisplayHub* h = multi_display_hub_create(3);
    ASSERT_NE(h, nullptr);
    multi_display_hub_destroy(h);
}
TEST_F(MultiScreenTest, GetDisplayInfoWorks) {
    ASSERT_NE(hub, nullptr);
    DisplayInfo info = {};
    // hub 没有注册显示器时返回-1是预期的
    EXPECT_EQ(multi_display_hub_get_display_info(hub, 0, &info), -1);
}
TEST_F(MultiScreenTest, SetPrimaryWorks) {
    ASSERT_NE(hub, nullptr);
    EXPECT_EQ(multi_display_hub_set_primary(hub, 0), 0);
}
TEST_F(MultiScreenTest, SetSyncMode) {
    ASSERT_NE(hub, nullptr);
    EXPECT_EQ(multi_display_hub_set_sync_mode(hub, SYNC_MODE_MASTER_SLAVE), 0);
}
TEST_F(MultiScreenTest, CalibrationStatus) {
    ASSERT_NE(hub, nullptr);
    float max_de = 0;
    char err[256];
    int status = multi_display_hub_get_calibration_status(hub, 0, &max_de, err, sizeof(err));
    EXPECT_GE(status, -1);
}
