/**
 * @file test_predictive_maintenance.cpp
 * @brief 预测性维护模块单元测试
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "predictive_maintenance.h"
}

class PredictiveMaintenanceTest : public ::testing::Test {
protected:
    PredictiveMaintenanceEngine* engine = nullptr;

    void SetUp() override {
        engine = predictive_engine_create(DEVICE_TYPE_DIAGNOSTIC);
        ASSERT_NE(engine, nullptr);
    }

    void TearDown() override {
        predictive_engine_destroy(engine);
        engine = nullptr;
    }
};

TEST_F(PredictiveMaintenanceTest, CreateAndDestroy) {
    PredictiveMaintenanceEngine* e = predictive_engine_create(DEVICE_TYPE_CLINICAL);
    ASSERT_NE(e, nullptr);
    predictive_engine_destroy(e);
}

TEST_F(PredictiveMaintenanceTest, NullCreateReturnsNull) {
    // 边界测试：destroy null 不崩溃
    predictive_engine_destroy(nullptr);
}

TEST_F(PredictiveMaintenanceTest, HealthyDeviceScoresHigh) {
    MaintenanceMetrics m = {};
    m.current_luminance = 450.0f;
    m.max_luminance = 500.0f;
    m.luminance_ratio = 0.90f;
    m.delta_e = 1.0f;
    m.panel_temp_celsius = 35.0f;
    m.ambient_temp_celsius = 22.0f;
    m.backlight_hours = 5000;
    m.calibration_age_days = 5;
    m.recommended_calibration_interval_days = 30;
    m.calibration_quality_score = 95.0f;
    m.uniformity_score = 95.0f;
    m.dead_pixel_count = 0;

    ASSERT_EQ(predictive_engine_update_metrics(engine, &m), 0);

    PredictionResult result;
    ASSERT_EQ(predictive_engine_analyze(engine, &result), 0);

    EXPECT_GE(result.health_score, 80.0f);
    EXPECT_EQ(result.overall_status, HEALTH_STATUS_EXCELLENT);
}

TEST_F(PredictiveMaintenanceTest, CriticalDeviceScoresLow) {
    MaintenanceMetrics m = {};
    m.current_luminance = 300.0f;
    m.max_luminance = 500.0f;
    m.luminance_ratio = 0.60f;
    m.delta_e = 5.0f;
    m.panel_temp_celsius = 42.0f;
    m.ambient_temp_celsius = 26.0f;
    m.backlight_hours = 25000;
    m.calibration_age_days = 90;
    m.recommended_calibration_interval_days = 30;
    m.calibration_quality_score = 35.0f;
    m.uniformity_score = 70.0f;
    m.dead_pixel_count = 3;

    ASSERT_EQ(predictive_engine_update_metrics(engine, &m), 0);

    PredictionResult result;
    ASSERT_EQ(predictive_engine_analyze(engine, &result), 0);

    EXPECT_LE(result.health_score, 70.0f);
    EXPECT_GE(result.risk_score, 50.0f);
    EXPECT_GE(result.num_issues, 1u);
}

TEST_F(PredictiveMaintenanceTest, GetRecommendationsReturnsActions) {
    MaintenanceMetrics m = {};
    m.current_luminance = 400.0f;
    m.max_luminance = 500.0f;
    m.luminance_ratio = 0.80f;
    m.delta_e = 2.5f;
    m.panel_temp_celsius = 38.0f;
    m.ambient_temp_celsius = 24.0f;
    m.backlight_hours = 12000;
    m.calibration_age_days = 35;
    m.recommended_calibration_interval_days = 30;
    m.calibration_quality_score = 70.0f;
    m.uniformity_score = 85.0f;
    m.dead_pixel_count = 0;

    ASSERT_EQ(predictive_engine_update_metrics(engine, &m), 0);

    MaintenanceRecommendation recs[10];
    int count = predictive_engine_get_recommendations(engine, recs, 10);
    EXPECT_GE(count, 1);
    // 至少应有一条关于校准的建议
    bool has_calibration = false;
    for (int i = 0; i < count; i++) {
        if (recs[i].action == ACTION_SCHEDULE_CALIBRATION) {
            has_calibration = true;
        }
    }
    EXPECT_TRUE(has_calibration);
}

TEST_F(PredictiveMaintenanceTest, GenerateQCReport) {
    MaintenanceMetrics m = {};
    m.current_luminance = 480.0f;
    m.max_luminance = 500.0f;
    m.luminance_ratio = 0.96f;
    m.delta_e = 1.0f;
    m.panel_temp_celsius = 33.0f;
    m.ambient_temp_celsius = 21.0f;
    m.backlight_hours = 3000;
    m.calibration_age_days = 7;
    m.recommended_calibration_interval_days = 30;
    m.calibration_quality_score = 98.0f;
    m.uniformity_score = 97.0f;
    m.dead_pixel_count = 0;

    ASSERT_EQ(predictive_engine_update_metrics(engine, &m), 0);

    char report[4096];
    ASSERT_EQ(predictive_engine_generate_qc_report(engine, report, sizeof(report)), 0);
    EXPECT_GT(strlen(report), 10u);
    EXPECT_NE(strstr(report, "report_type"), nullptr);
}

TEST_F(PredictiveMaintenanceTest, SetThresholdsWorks) {
    EXPECT_EQ(predictive_engine_set_thresholds(engine, 400.0f, 2.0f, 42.0f), 0);
}
