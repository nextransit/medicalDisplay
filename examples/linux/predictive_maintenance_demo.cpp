/**
 * @file predictive_maintenance_demo.cpp
 * @brief 预测性维护系统示例程序
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include "predictive_maintenance.h"

static void print_metrics(const MaintenanceMetrics* m) {
    printf("  亮度: %.1f / %.1f cd/m² (%.1f%%)\n", 
           m->current_luminance, m->max_luminance, m->luminance_ratio * 100);
    printf("  色彩偏差: ΔE = %.2f\n", m->delta_e);
    printf("  白点: (%.4f, %.4f) @ %.0fK\n", 
           m->white_point_x, m->white_point_y, m->color_temp_kelvin);
    printf("  面板温度: %.1f°C (环境 %.1f°C)\n", 
           m->panel_temp_celsius, m->ambient_temp_celsius);
    printf("  背光时长: %u 小时\n", m->backlight_hours);
    printf("  校准年龄: %u 天 (推荐 %u 天)\n", 
           m->calibration_age_days, m->recommended_calibration_interval_days);
    printf("  均匀性: %.1f%%\n", m->uniformity_score);
    printf("  死像素: %u\n", m->dead_pixel_count);
}

static void print_prediction(const PredictionResult* p) {
    const char* status_str[] = {"优秀", "良好", "一般", "较差", "危急"};
    printf("健康评分: %.1f / 100\n", p->health_score);
    printf("健康状态: %s\n", status_str[p->overall_status]);
    printf("风险评分: %.1f / 100\n", p->risk_score);
    printf("预测置信度: %.0f%%\n", p->prediction_confidence * 100);
    printf("\n预测天数:\n");
    printf("  亮度故障: %u 天\n", p->predicted_luminance_failure_days);
    printf("  色彩故障: %u 天\n", p->predicted_color_failure_days);
    printf("  建议校准: %u 天\n", p->predicted_calibration_due_days);
    
    if (p->num_issues > 0) {
        printf("\n发现问题 (%d):\n", p->num_issues);
        for (int i = 0; i < p->num_issues; i++) {
            printf("  [%d] %s (严重度: %.0f%%)\n", 
                   i + 1, p->issues[i], p->issue_severity[i] * 100);
        }
    }
}

static void print_recommendations(PredictiveMaintenanceEngine* engine) {
    MaintenanceRecommendation recs[10];
    int count = predictive_engine_get_recommendations(engine, recs, 10);
    
    if (count > 0) {
        printf("\n维护建议 (%d):\n", count);
        for (int i = 0; i < count; i++) {
            printf("  [优先级 %d] %s\n", recs[i].priority, recs[i].title);
            printf("    操作: %s\n", recs[i].action == ACTION_NONE ? "无需操作" :
                                       recs[i].action == ACTION_MONITOR ? "继续监控" :
                                       recs[i].action == ACTION_SCHEDULE_CALIBRATION ? "安排校准" :
                                       recs[i].action == ACTION_REDUCE_BRIGHTNESS ? "降低亮度" :
                                       recs[i].action == ACTION_REPLACE_PANEL ? "更换面板" : "其他");
            printf("    描述: %s\n", recs[i].description);
            printf("    建议时间: %d 天内\n", recs[i].recommended_days);
            if (recs[i].estimated_cost_usd > 0) {
                printf("    预估成本: $%.0f\n", recs[i].estimated_cost_usd);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    printf("=== AI 预测性维护系统演示 ===\n\n");
    
    // 创建引擎
    PredictiveMaintenanceEngine* engine = predictive_engine_create(DEVICE_TYPE_DIAGNOSTIC);
    if (!engine) {
        printf("错误: 无法创建引擎\n");
        return 1;
    }
    
    // 设置报警阈值
    predictive_engine_set_thresholds(engine, 350.0f, 3.0f, 40.0f);
    
    // 模拟健康设备
    printf("【场景1: 健康设备】\n");
    {
        MaintenanceMetrics m = {};
        m.current_luminance = 450.0f;
        m.max_luminance = 500.0f;
        m.luminance_ratio = 0.90f;
        m.luminance_drift_percent = 3.0f;
        m.delta_e = 1.5f;
        m.white_point_x = 0.3127f;
        m.white_point_y = 0.3290f;
        m.color_temp_kelvin = 6500.0f;
        m.panel_temp_celsius = 35.0f;
        m.ambient_temp_celsius = 22.0f;
        m.backlight_hours = 5000;
        m.power_on_hours = 8000;
        m.calibration_age_days = 5;
        m.recommended_calibration_interval_days = 30;
        m.calibration_quality_score = 95.0f;
        m.uniformity_score = 95.0f;
        m.dead_pixel_count = 0;
        
        predictive_engine_update_metrics(engine, &m);
        print_metrics(&m);
        
        PredictionResult result;
        predictive_engine_analyze(engine, &result);
        print_prediction(&result);
        print_recommendations(engine);
    }
    
    printf("\n");
    printf("【场景2: 需要校准的设备】\n");
    {
        MaintenanceMetrics m = {};
        m.current_luminance = 420.0f;
        m.max_luminance = 500.0f;
        m.luminance_ratio = 0.84f;
        m.luminance_drift_percent = 8.0f;
        m.delta_e = 2.5f;
        m.white_point_x = 0.3150f;
        m.white_point_y = 0.3320f;
        m.color_temp_kelvin = 6400.0f;
        m.panel_temp_celsius = 38.0f;
        m.ambient_temp_celsius = 24.0f;
        m.backlight_hours = 12000;
        m.power_on_hours = 15000;
        m.calibration_age_days = 35;
        m.recommended_calibration_interval_days = 30;
        m.calibration_quality_score = 70.0f;
        m.uniformity_score = 88.0f;
        m.dead_pixel_count = 0;
        
        predictive_engine_update_metrics(engine, &m);
        print_metrics(&m);
        
        PredictionResult result;
        predictive_engine_analyze(engine, &result);
        print_prediction(&result);
        print_recommendations(engine);
    }
    
    printf("\n");
    printf("【场景3: 危急设备】\n");
    {
        MaintenanceMetrics m = {};
        m.current_luminance = 320.0f;  // 低于阈值
        m.max_luminance = 500.0f;
        m.luminance_ratio = 0.64f;
        m.luminance_drift_percent = 18.0f;
        m.delta_e = 4.5f;  // 超过阈值
        m.white_point_x = 0.3200f;
        m.white_point_y = 0.3380f;
        m.color_temp_kelvin = 6200.0f;
        m.panel_temp_celsius = 42.0f;  // 接近危险
        m.ambient_temp_celsius = 26.0f;
        m.backlight_hours = 25000;
        m.power_on_hours = 30000;
        m.calibration_age_days = 90;  // 严重过期
        m.recommended_calibration_interval_days = 30;
        m.calibration_quality_score = 40.0f;
        m.uniformity_score = 75.0f;
        m.dead_pixel_count = 3;  // 出现死像素
        
        predictive_engine_update_metrics(engine, &m);
        print_metrics(&m);
        
        PredictionResult result;
        predictive_engine_analyze(engine, &result);
        print_prediction(&result);
        print_recommendations(engine);
    }
    
    printf("\n");
    printf("【场景4: 质控报告生成】\n");
    {
        MaintenanceMetrics m = {};
        m.current_luminance = 480.0f;
        m.max_luminance = 500.0f;
        m.luminance_ratio = 0.96f;
        m.luminance_drift_percent = 2.0f;
        m.delta_e = 1.0f;
        m.white_point_x = 0.3128f;
        m.white_point_y = 0.3292f;
        m.color_temp_kelvin = 6500.0f;
        m.panel_temp_celsius = 33.0f;
        m.ambient_temp_celsius = 21.0f;
        m.backlight_hours = 3000;
        m.power_on_hours = 5000;
        m.calibration_age_days = 7;
        m.recommended_calibration_interval_days = 30;
        m.calibration_quality_score = 98.0f;
        m.uniformity_score = 97.0f;
        m.dead_pixel_count = 0;
        
        predictive_engine_update_metrics(engine, &m);
        
        char report[4096];
        if (predictive_engine_generate_qc_report(engine, report, sizeof(report)) == 0) {
            printf("质控报告 (JSON):\n%s\n", report);
        }
    }
    
    // 清理
    predictive_engine_destroy(engine);
    
    printf("\n=== 演示完成 ===\n");
    return 0;
}
