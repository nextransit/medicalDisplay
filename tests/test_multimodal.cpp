#include <gtest/gtest.h>
#include <cstring>
extern "C" {
#include "multimodal_fusion.h"
#include "ai_engine.h"
}

class MultimodalFusionTest : public ::testing::Test {
protected:
    MultimodalFusionEngine* engine = nullptr;
    void SetUp() override { engine = fusion_engine_create(false); }
    void TearDown() override { fusion_engine_destroy(engine); }
};

TEST_F(MultimodalFusionTest, CreateAndDestroy) {
    MultimodalFusionEngine* e = fusion_engine_create(false);
    ASSERT_NE(e, nullptr);
    fusion_engine_destroy(e);
}
TEST_F(MultimodalFusionTest, SetAnatomicalWorks) {
    ASSERT_NE(engine, nullptr);
    uint16_t data[100];
    memset(data, 0, sizeof(data));
    EXPECT_EQ(fusion_set_anatomical_volume(engine, data, 10, 10, 1, 1.0f, 1.0f, 1.0f), 0);
}
TEST_F(MultimodalFusionTest, AutoRegisterWorks) {
    ASSERT_NE(engine, nullptr);
    uint16_t data[100];
    memset(data, 0, sizeof(data));
    fusion_set_anatomical_volume(engine, data, 10, 10, 1, 1.0f, 1.0f, 1.0f);
    float pet_data[100]; memset(pet_data,0,sizeof(pet_data)); fusion_set_functional_volume(engine, pet_data, 8, 8, 1, 1.0f, 1.0f, 1.0f, 1.0f);
    FusionAlignmentParams params = {};
    EXPECT_EQ(fusion_auto_register(engine, &params), 0);
}
