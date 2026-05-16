#include <gtest/gtest.h>
#include <cstring>
extern "C" {
#include "digital_twin.h"
}

class DigitalTwinTest : public ::testing::Test {
protected:
    DigitalTwinEngine* dt = nullptr;
    void SetUp() override { dt = dt_engine_create("hospital-001"); }
    void TearDown() override { dt_engine_destroy(dt); }
};

TEST_F(DigitalTwinTest, CreateAndDestroy) {
    DigitalTwinEngine* d = dt_engine_create("test");
    ASSERT_NE(d, nullptr);
    dt_engine_destroy(d);
}
TEST_F(DigitalTwinTest, RegisterAndUpdateDevice) {
    ASSERT_NE(dt, nullptr);
    DeviceInfo dev = {};
    strcpy(dev.device_id, "dev-001");
    strcpy(dev.device_name, "Diagnostic Monitor 1");
    EXPECT_EQ(dt_engine_register_device(dt, &dev), 0);
    EXPECT_EQ(dt_engine_update_device(dt, &dev), 0);
}
TEST_F(DigitalTwinTest, SimulateAll) {
    ASSERT_NE(dt, nullptr);
    DeviceInfo dev = {};
    strcpy(dev.device_id, "dev-001");
    dt_engine_register_device(dt, &dev);
    const char* ids[] = {"dev-001"};
    EXPECT_EQ(dt_engine_simulate_all(dt, ids, 1, 100), 0);
}
