/**
 * @file test_federated.cpp
 * @brief 联邦学习客户端模块单元测试
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "federated_client.h"
}

class FederatedClientTest : public ::testing::Test {
protected:
    FederatedClient* client = nullptr;

    void SetUp() override {
        FLClientConfig config = {};
        strcpy(config.server_url, "http://localhost:8080");
        strcpy(config.model_id, "medical-v1");
        strcpy(config.device_id, "device-test-001");
        strcpy(config.hospital_id, "hospital-001");
        client = fl_client_create(&config);
    }

    void TearDown() override {
        fl_client_destroy(client);
        client = nullptr;
    }
};

TEST_F(FederatedClientTest, CreateWithNullConfigReturnsNull) {
    FederatedClient* c = fl_client_create(nullptr);
    EXPECT_EQ(c, nullptr);
}

TEST_F(FederatedClientTest, CreateAndGetState) {
    ASSERT_NE(client, nullptr);
    EXPECT_EQ(fl_client_get_state(client), FL_CLIENT_IDLE);
}

TEST_F(FederatedClientTest, DestroyNullSafe) {
    fl_client_destroy(nullptr);
    // 不崩溃
}

TEST_F(FederatedClientTest, InitialStateIsNotConnected) {
    ASSERT_NE(client, nullptr);
    EXPECT_FALSE(fl_client_is_connected(client));
}

TEST_F(FederatedClientTest, GetModelVersionUnconnected) {
    ASSERT_NE(client, nullptr);
    int v = fl_client_get_model_version(client);
    EXPECT_EQ(v, -1);  // 未连接
}

TEST_F(FederatedClientTest, RegisterDataset) {
    ASSERT_NE(client, nullptr);
    EXPECT_EQ(fl_client_register_dataset(client, 1000, "{\"modality\":\"CT\"}"), 0);
}

TEST_F(FederatedClientTest, GetStatsWorks) {
    ASSERT_NE(client, nullptr);
    FLTrainingStats stats;
    memset(&stats, 0, sizeof(stats));
    EXPECT_EQ(fl_client_get_stats(client, &stats), 0);
}

TEST_F(FederatedClientTest, ResetStatsWorks) {
    ASSERT_NE(client, nullptr);
    fl_client_reset_stats(client);
    // 不崩溃
}

TEST_F(FederatedClientTest, PrivacyBudget) {
    ASSERT_NE(client, nullptr);
    float epsilon = 0, delta = 0;
    EXPECT_EQ(fl_client_get_privacy_budget(client, &epsilon, &delta), 0);
    EXPECT_FLOAT_EQ(epsilon, 0.0f);
    EXPECT_FLOAT_EQ(delta, 0.0f);
}
