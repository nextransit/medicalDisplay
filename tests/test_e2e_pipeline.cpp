#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>

extern "C" {
#include "ai_engine.h"
#include "display_engine.h"
#include "predictive_maintenance.h"
#include "federated_client.h"
#include "ar_overlay.h"
#include "digital_twin.h"
}

TEST(EndToEndPipeline, AIRecognition) {
    AIEngineConfig ai_cfg = {};
    strcpy(ai_cfg.model_path, "");
    ai_cfg.input_height = 64;
    ai_cfg.input_width = 64;
    AIEngine* ai = ai_engine_create(&ai_cfg);
    ASSERT_NE(ai, nullptr);

    uint8_t fake_img[64 * 64];
    memset(fake_img, 128, sizeof(fake_img));

    AIRecognitionResult result;
    memset(&result, 0, sizeof(result));
    EXPECT_EQ(ai_engine_recognize_from_image(ai, fake_img, 64, 64, 1, &result), 0);
    EXPECT_GE(result.confidence, 0.0f);

    uint64_t total_inf = 0; float avg_ms = 0;
    ai_engine_get_stats(ai, &total_inf, &avg_ms);
    EXPECT_GE(total_inf, 1u);

    ai_engine_destroy(ai);
}

TEST(EndToEndPipeline, PredictiveMaintenanceQC) {
    PredictiveMaintenanceEngine* e = predictive_engine_create(DEVICE_TYPE_SURGICAL);
    ASSERT_NE(e, nullptr);
    predictive_engine_set_thresholds(e, 500.0f, 2.5f, 38.0f);

    MaintenanceMetrics m = {};
    m.current_luminance = 480.0f; m.max_luminance = 550.0f; m.luminance_ratio = 0.87f;
    m.delta_e = 2.0f; m.panel_temp_celsius = 36.0f; m.ambient_temp_celsius = 22.0f;
    m.backlight_hours = 12000; m.calibration_age_days = 45;
    m.recommended_calibration_interval_days = 30;
    m.calibration_quality_score = 72.0f; m.uniformity_score = 88.0f;

    predictive_engine_update_metrics(e, &m);
    PredictionResult r;
    predictive_engine_analyze(e, &r);
    EXPECT_GE(r.health_score, 0.0f); EXPECT_LE(r.health_score, 100.0f);

    char report[4096];
    predictive_engine_generate_qc_report(e, report, sizeof(report));
    EXPECT_NE(strstr(report, "report_type"), nullptr);
    predictive_engine_destroy(e);
}

TEST(EndToEndPipeline, FederatedLearning) {
    FLClientConfig c = {};
    strcpy(c.server_url, "http://localhost:8080");
    strcpy(c.model_id, "medical-v1");
    strcpy(c.device_id, "e2e-test");
    strcpy(c.hospital_id, "e2e-hosp");

    FederatedClient* cl = fl_client_create(&c);
    ASSERT_NE(cl, nullptr);
    EXPECT_EQ(fl_client_get_state(cl), FL_CLIENT_IDLE);
    EXPECT_EQ(fl_client_register_dataset(cl, 1000, "{\"modality\":\"CT\"}"), 0);
    fl_client_destroy(cl);
}

TEST(EndToEndPipeline, AROverlayRender) {
    AROverlayEngine* ar = ar_engine_create(800, 600);
    ASSERT_NE(ar, nullptr);

    AnnotationData ann = {};
    ann.type = ANNOTATION_RECT;
    ann.x = 100.0f; ann.y = 100.0f; ann.width = 80.0f; ann.height = 60.0f;
    strcpy(ann.label, "lesion");
    EXPECT_GE(ar_engine_add_annotation(ar, &ann), 1);

    AnnotationSession s = {};
    EXPECT_EQ(ar_engine_create_session(ar, &s), 0);

    uint8_t frame[800*600*3]; memset(frame, 64, sizeof(frame));
    uint8_t out[800*600*3];
    EXPECT_EQ(ar_engine_render(ar, frame, out), 0);
    ar_engine_destroy(ar);
}

TEST(EndToEndPipeline, DigitalTwinSim) {
    DigitalTwinEngine* dt = dt_engine_create("e2e-hosp");
    ASSERT_NE(dt, nullptr);

    DeviceInfo dev = {};
    strcpy(dev.device_id, "dpy-001");
    strcpy(dev.device_name, "E2E Monitor");
    EXPECT_EQ(dt_engine_register_device(dt, &dev), 0);

    const char* ids[] = {"dpy-001"};
    EXPECT_EQ(dt_engine_simulate_all(dt, ids, 1, 500), 0);
    dt_engine_destroy(dt);
}
