/**
 * @file predictive_maintenance.cpp
 * @brief AI预测性维护系统实现
 * 
 * 基于设备遥测数据的健康预测、寿命预测和维护建议生成
 */

#include "predictive_maintenance.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <queue>
#include <sstream>

// ============================================================================
// 内部数据结构
// ============================================================================

struct TrendDataPoint {
    uint64_t timestamp;
    MaintenanceMetrics metrics;
};

struct PredictiveMaintenanceEngine {
    DeviceType device_type;
    MaintenanceMetrics current_metrics;
    std::vector<TrendDataPoint> history;
    
    // 报警阈值
    float luminance_min;
    float delta_e_max;
    float temp_max;
    
    // 预测模型参数 (简化线性回归)
    float luminance_slope;         // 亮度衰减斜率
    float delta_e_slope;          // 色彩漂移斜率
    float temp_slope;             // 温度上升斜率
    
    uint64_t last_update_time;
    
    PredictiveMaintenanceEngine(DeviceType type) 
        : device_type(type), luminance_min(0.0f), delta_e_max(5.0f), temp_max(45.0f),
          luminance_slope(0.0f), delta_e_slope(0.0f), temp_slope(0.0f),
          last_update_time(0) {
        
        // 设置设备特定阈值
        switch (type) {
            case DEVICE_TYPE_DIAGNOSTIC:
                luminance_min = 350.0f;  // 诊断显示器最低亮度
                delta_e_max = 3.0f;      // 严格色彩要求
                temp_max = 40.0f;
                break;
            case DEVICE_TYPE_CLINICAL:
                luminance_min = 250.0f;
                delta_e_max = 5.0f;
                temp_max = 45.0f;
                break;
            case DEVICE_TYPE_SURGICAL:
                luminance_min = 500.0f;  // 手术室高亮度要求
                delta_e_max = 2.5f;       // 极高色彩要求
                temp_max = 38.0f;
                break;
            default:
                break;
        }
        
        memset(&current_metrics, 0, sizeof(current_metrics));
    }
};

// ============================================================================
// 辅助函数
// ============================================================================

static HealthStatus calculate_health_status(float score) {
    if (score >= 90.0f) return HEALTH_STATUS_EXCELLENT;
    if (score >= 75.0f) return HEALTH_STATUS_GOOD;
    if (score >= 60.0f) return HEALTH_STATUS_FAIR;
    if (score >= 40.0f) return HEALTH_STATUS_POOR;
    return HEALTH_STATUS_CRITICAL;
}

static const char* status_to_string(HealthStatus status) {
    switch (status) {
        case HEALTH_STATUS_EXCELLENT: return "优秀";
        case HEALTH_STATUS_GOOD: return "良好";
        case HEALTH_STATUS_FAIR: return "一般";
        case HEALTH_STATUS_POOR: return "较差";
        case HEALTH_STATUS_CRITICAL: return "危急";
        default: return "未知";
    }
}

static const char* action_to_string(MaintenanceAction action) {
    switch (action) {
        case ACTION_NONE: return "无需操作";
        case ACTION_MONITOR: return "继续监控";
        case ACTION_SCHEDULE_CALIBRATION: return "安排校准";
        case ACTION_REDUCE_BRIGHTNESS: return "降低亮度";
        case ACTION_SCHEDULE_INSPECTION: return "安排检查";
        case ACTION_REPLACE_PANEL: return "更换面板";
        case ACTION_URGENT_REPLACEMENT: return "紧急更换";
        default: return "未知";
    }
}

// 线性回归计算斜率
static float compute_slope(const std::vector<std::pair<uint64_t, float>>& data) {
    if (data.size() < 2) return 0.0f;
    
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
    int n = static_cast<int>(data.size());
    
    for (const auto& point : data) {
        sum_x += point.first;
        sum_y += point.second;
        sum_xy += point.first * point.second;
        sum_xx += point.first * point.first;
    }
    
    double denominator = n * sum_xx - sum_x * sum_x;
    if (std::abs(denominator) < 1e-10) return 0.0f;
    
    return static_cast<float>((n * sum_xy - sum_x * sum_y) / denominator);
}

// ============================================================================
// 引擎生命周期
// ============================================================================

extern "C" {

PredictiveMaintenanceEngine* predictive_engine_create(DeviceType device_type) {
    if (device_type >= DEVICE_TYPE_COUNT) {
        return nullptr;
    }
    return new (std::nothrow) PredictiveMaintenanceEngine(device_type);
}

void predictive_engine_destroy(PredictiveMaintenanceEngine* engine) {
    delete engine;
}

// ============================================================================
// 指标管理
// ============================================================================

int predictive_engine_update_metrics(PredictiveMaintenanceEngine* engine,
                                    const MaintenanceMetrics* metrics) {
    if (!engine || !metrics) {
        return -1;
    }
    
    // 保存历史
    TrendDataPoint point;
    point.timestamp = engine->last_update_time;
    point.metrics = engine->current_metrics;
    
    if (engine->history.size() >= 365) {  // 保留一年数据
        engine->history.erase(engine->history.begin());
    }
    engine->history.push_back(point);
    
    // 更新当前指标
    engine->current_metrics = *metrics;
    engine->last_update_time++;
    
    return 0;
}

int predictive_engine_get_metrics(PredictiveMaintenanceEngine* engine,
                                  MaintenanceMetrics* metrics) {
    if (!engine || !metrics) {
        return -1;
    }
    
    *metrics = engine->current_metrics;
    return 0;
}

// ============================================================================
// 预测分析
// ============================================================================

int predictive_engine_analyze(PredictiveMaintenanceEngine* engine,
                             PredictionResult* result) {
    if (!engine || !result) {
        return -1;
    }
    
    const MaintenanceMetrics& m = engine->current_metrics;
    
    // 计算亮度健康评分
    float luminance_score = 100.0f;
    if (m.max_luminance > 0) {
        float ratio = m.current_luminance / m.max_luminance;
        luminance_score = ratio * 100.0f;
        
        // 考虑漂移
        if (m.luminance_drift_percent > 10.0f) {
            luminance_score -= (m.luminance_drift_percent - 10.0f) * 2.0f;
        }
    }
    luminance_score = std::max(0.0f, std::min(100.0f, luminance_score));
    
    // 计算色彩健康评分
    float color_score = 100.0f;
    if (m.delta_e > 0) {
        // Delta E > 3 开始明显，> 5 不可接受
        if (m.delta_e > 5.0f) {
            color_score = 20.0f;
        } else if (m.delta_e > 3.0f) {
            color_score = 100.0f - (m.delta_e - 3.0f) * 40.0f;
        } else {
            color_score = 100.0f - m.delta_e * 10.0f;
        }
    }
    color_score = std::max(0.0f, std::min(100.0f, color_score));
    
    // 计算温度健康评分
    float temp_score = 100.0f;
    if (m.panel_temp_celsius > engine->temp_max) {
        temp_score = 100.0f - (m.panel_temp_celsius - engine->temp_max) * 10.0f;
    } else if (m.panel_temp_celsius > engine->temp_max - 5.0f) {
        temp_score = 100.0f - (engine->temp_max - m.panel_temp_celsius) * 2.0f;
    }
    temp_score = std::max(0.0f, std::min(100.0f, temp_score));
    
    // 计算校准健康评分
    float calibration_score = 100.0f;
    if (m.calibration_age_days > m.recommended_calibration_interval_days) {
        calibration_score = 50.0f;
    } else {
        float ratio = static_cast<float>(m.calibration_age_days) / m.recommended_calibration_interval_days;
        calibration_score = 100.0f - ratio * 50.0f;
    }
    calibration_score = std::max(0.0f, std::min(100.0f, calibration_score));
    
    // 计算均匀性评分
    float uniformity_score = m.uniformity_score;
    
    // 综合评分 (加权平均)
    float overall_score = 
        luminance_score * 0.30f +
        color_score * 0.25f +
        temp_score * 0.15f +
        calibration_score * 0.20f +
        uniformity_score * 0.10f;
    
    // 风险评分 (问题越多越高)
    float risk_score = 0.0f;
    if (m.luminance_ratio < 0.8f) risk_score += 20.0f;
    if (m.delta_e > engine->delta_e_max) risk_score += 25.0f;
    if (m.panel_temp_celsius > engine->temp_max) risk_score += 30.0f;
    if (m.calibration_age_days > m.recommended_calibration_interval_days) risk_score += 15.0f;
    if (m.dead_pixel_count > 0) risk_score += m.dead_pixel_count * 5.0f;
    risk_score = std::min(100.0f, risk_score);
    
    // 设置结果
    result->overall_status = calculate_health_status(overall_score);
    result->health_score = overall_score;
    result->risk_score = risk_score;
    
    // 预测天数计算 (简化模型)
    // 基于线性衰减斜率和阈值计算
    float daily_luminance_drop = std::abs(engine->luminance_slope);
    if (daily_luminance_drop < 0.1f) daily_luminance_drop = 0.5f;  // 默认衰减率
    
    float luminance_headroom = m.current_luminance - engine->luminance_min;
    result->predicted_luminance_failure_days = 
        luminance_headroom > 0 ? static_cast<uint16_t>(luminance_headroom / daily_luminance_drop) : 0;
    
    float delta_e_headroom = engine->delta_e_max - m.delta_e;
    float daily_delta_e_increase = std::abs(engine->delta_e_slope);
    if (daily_delta_e_increase < 0.01f) daily_delta_e_increase = 0.02f;
    
    result->predicted_color_failure_days = 
        delta_e_headroom > 0 ? static_cast<uint16_t>(delta_e_headroom / daily_delta_e_increase) : 0;
    
    result->predicted_calibration_due_days = 
        (m.recommended_calibration_interval_days > m.calibration_age_days) ?
        (m.recommended_calibration_interval_days - m.calibration_age_days) : 0;
    
    // 问题列表
    result->num_issues = 0;
    
    if (m.current_luminance < engine->luminance_min) {
        snprintf(result->issues[result->num_issues], 128, "亮度低于最低要求: %.1f cd/m²", m.current_luminance);
        result->issue_severity[result->num_issues] = 0.9f;
        result->num_issues++;
    }
    
    if (m.delta_e > engine->delta_e_max) {
        snprintf(result->issues[result->num_issues], 128, "色彩偏差超标: Delta E = %.2f", m.delta_e);
        result->issue_severity[result->num_issues] = 0.8f;
        result->num_issues++;
    }
    
    if (m.panel_temp_celsius > engine->temp_max) {
        snprintf(result->issues[result->num_issues], 128, "面板温度过高: %.1f°C", m.panel_temp_celsius);
        result->issue_severity[result->num_issues] = 0.7f;
        result->num_issues++;
    }
    
    if (m.calibration_age_days > m.recommended_calibration_interval_days) {
        snprintf(result->issues[result->num_issues], 128, "校准过期: %d天未校准", m.calibration_age_days);
        result->issue_severity[result->num_issues] = 0.5f;
        result->num_issues++;
    }
    
    if (m.dead_pixel_count > 0) {
        snprintf(result->issues[result->num_issues], 128, "发现死像素: %d个", m.dead_pixel_count);
        result->issue_severity[result->num_issues] = 0.6f;
        result->num_issues++;
    }
    
    if (m.uniformity_score < 80.0f) {
        snprintf(result->issues[result->num_issues], 128, "均匀性下降: %.1f%%", m.uniformity_score);
        result->issue_severity[result->num_issues] = 0.4f;
        result->num_issues++;
    }
    
    result->prediction_confidence = 0.85f;
    result->model_version = 1;
    
    return 0;
}

// ============================================================================
// 维护建议
// ============================================================================

int predictive_engine_get_recommendations(PredictiveMaintenanceEngine* engine,
                                         MaintenanceRecommendation* recommendations,
                                         int max_count) {
    if (!engine || !recommendations || max_count <= 0) {
        return -1;
    }
    
    PredictionResult analysis;
    predictive_engine_analyze(engine, &analysis);
    
    const MaintenanceMetrics& m = engine->current_metrics;
    int count = 0;
    
    // 基于分析结果生成建议
    if (analysis.health_score >= 90.0f) {
        // 优秀状态
        recommendations[count].action = ACTION_MONITOR;
        recommendations[count].priority = 5;
        snprintf(recommendations[count].title, 128, "设备状态优秀");
        snprintf(recommendations[count].description, 512, "所有指标正常，建议继续常规监控");
        snprintf(recommendations[count].rationale, 256, "健康评分: %.1f", analysis.health_score);
        recommendations[count].recommended_days = 30;
        recommendations[count].estimated_cost_usd = 0.0f;
        count++;
    }
    
    if (analysis.health_score >= 75.0f && analysis.health_score < 90.0f) {
        // 良好状态
        if (m.calibration_age_days > 20) {
            recommendations[count].action = ACTION_SCHEDULE_CALIBRATION;
            recommendations[count].priority = 3;
            snprintf(recommendations[count].title, 128, "安排定期校准");
            snprintf(recommendations[count].description, 512, "校准已超过20天，建议近期安排校准以保持最佳性能");
            snprintf(recommendations[count].rationale, 256, "校准年龄: %d天", m.calibration_age_days);
            recommendations[count].recommended_days = 14;
            recommendations[count].estimated_cost_usd = 500.0f;
            count++;
        }
    }
    
    if (analysis.health_score >= 60.0f && analysis.health_score < 75.0f) {
        // 一般状态 - 需要关注
        if (m.current_luminance < engine->luminance_min * 1.2f) {
            recommendations[count].action = ACTION_REDUCE_BRIGHTNESS;
            recommendations[count].priority = 2;
            snprintf(recommendations[count].title, 128, "考虑降低使用亮度");
            snprintf(recommendations[count].description, 512, "亮度接近下限，建议降低使用亮度以延长背光寿命");
            snprintf(recommendations[count].rationale, 256, "当前亮度: %.1f cd/m², 最低: %.1f", 
                    m.current_luminance, engine->luminance_min);
            recommendations[count].recommended_days = 7;
            recommendations[count].estimated_cost_usd = 0.0f;
            count++;
        }
        
        recommendations[count].action = ACTION_SCHEDULE_CALIBRATION;
        recommendations[count].priority = 2;
        snprintf(recommendations[count].title, 128, "尽快安排校准");
        snprintf(recommendations[count].description, 512, "设备状态一般，建议尽快安排专业校准");
        snprintf(recommendations[count].rationale, 256, "健康评分: %.1f", analysis.health_score);
        recommendations[count].recommended_days = 7;
        recommendations[count].estimated_cost_usd = 800.0f;
        count++;
    }
    
    if (analysis.health_score < 60.0f) {
        // 较差/危急状态
        recommendations[count].action = ACTION_SCHEDULE_INSPECTION;
        recommendations[count].priority = 1;
        snprintf(recommendations[count].title, 128, "安排全面检查");
        snprintf(recommendations[count].description, 512, "设备多项指标异常，需要进行全面检查");
        snprintf(recommendations[count].rationale, 256, "健康评分: %.1f, 风险评分: %.1f", 
                analysis.health_score, analysis.risk_score);
        recommendations[count].recommended_days = 3;
        recommendations[count].estimated_cost_usd = 300.0f;
        count++;
    }
    
    if (m.dead_pixel_count > 5) {
        recommendations[count].action = ACTION_REPLACE_PANEL;
        recommendations[count].priority = 1;
        snprintf(recommendations[count].title, 128, "考虑更换面板");
        snprintf(recommendations[count].description, 512, "死像素数量较多，建议更换面板");
        snprintf(recommendations[count].rationale, 256, "死像素数: %d", m.dead_pixel_count);
        recommendations[count].recommended_days = 30;
        recommendations[count].estimated_cost_usd = 5000.0f;
        count++;
    }
    
    if (analysis.risk_score > 80.0f) {
        recommendations[count].action = ACTION_URGENT_REPLACEMENT;
        recommendations[count].priority = 1;
        snprintf(recommendations[count].title, 128, "⚠️ 紧急: 考虑更换设备");
        snprintf(recommendations[count].description, 512, "风险评分极高，建议制定更换计划");
        snprintf(recommendations[count].rationale, 256, "风险评分: %.1f", analysis.risk_score);
        recommendations[count].recommended_days = 1;
        recommendations[count].estimated_cost_usd = 15000.0f;
        count++;
    }
    
    return count;
}

// ============================================================================
// 报告生成
// ============================================================================

int predictive_engine_generate_qc_report(PredictiveMaintenanceEngine* engine,
                                        char* report_json,
                                        size_t buffer_size) {
    if (!engine || !report_json || buffer_size == 0) {
        return -1;
    }
    
    PredictionResult analysis;
    MaintenanceRecommendation recommendations[10];
    
    predictive_engine_analyze(engine, &analysis);
    int rec_count = predictive_engine_get_recommendations(engine, recommendations, 10);
    
    const MaintenanceMetrics& m = engine->current_metrics;
    
    // 生成JSON报告
    int written = snprintf(report_json, buffer_size,
        "{\n"
        "  \"report_type\": \"predictive_maintenance\",\n"
        "  \"device_type\": %d,\n"
        "  \"health_score\": %.1f,\n"
        "  \"health_status\": \"%s\",\n"
        "  \"risk_score\": %.1f,\n"
        "  \"metrics\": {\n"
        "    \"luminance\": {\"current\": %.1f, \"max\": %.1f, \"ratio\": %.3f},\n"
        "    \"color\": {\"delta_e\": %.2f, \"white_point\": [%.4f, %.4f]},\n"
        "    \"temperature\": {\"panel\": %.1f, \"ambient\": %.1f},\n"
        "    \"usage\": {\"backlight_hours\": %u, \"power_on_hours\": %u},\n"
        "    \"calibration\": {\"age_days\": %u, \"quality_score\": %.1f}\n"
        "  },\n"
        "  \"predictions\": {\n"
        "    \"luminance_failure_days\": %u,\n"
        "    \"color_failure_days\": %u,\n"
        "    \"calibration_due_days\": %u,\n"
        "    \"confidence\": %.2f\n"
        "  },\n"
        "  \"issues\": [",
        engine->device_type,
        analysis.health_score,
        status_to_string(analysis.overall_status),
        analysis.risk_score,
        m.current_luminance, m.max_luminance, m.luminance_ratio,
        m.delta_e, m.white_point_x, m.white_point_y,
        m.panel_temp_celsius, m.ambient_temp_celsius,
        m.backlight_hours, m.power_on_hours,
        m.calibration_age_days, m.calibration_quality_score,
        analysis.predicted_luminance_failure_days,
        analysis.predicted_color_failure_days,
        analysis.predicted_calibration_due_days,
        analysis.prediction_confidence
    );
    
    // 添加问题列表
    for (int i = 0; i < analysis.num_issues && written < static_cast<int>(buffer_size) - 100; i++) {
        if (i > 0) written += snprintf(report_json + written, buffer_size - written, ",");
        written += snprintf(report_json + written, buffer_size - written,
            "\n    {\"description\": \"%s\", \"severity\": %.2f}",
            analysis.issues[i], analysis.issue_severity[i]);
    }
    
    written += snprintf(report_json + written, buffer_size - written,
        "\n  ],\n  \"recommendations\": [");
    
    // 添加建议列表
    for (int i = 0; i < rec_count && written < static_cast<int>(buffer_size) - 100; i++) {
        if (i > 0) written += snprintf(report_json + written, buffer_size - written, ",");
        written += snprintf(report_json + written, buffer_size - written,
            "\n    {\"action\": \"%s\", \"priority\": %d, \"title\": \"%s\", \"days\": %u}",
            action_to_string(recommendations[i].action),
            recommendations[i].priority,
            recommendations[i].title,
            recommendations[i].recommended_days);
    }
    
    written += snprintf(report_json + written, buffer_size - written,
        "\n  ]\n}\n");
    
    return 0;
}

// ============================================================================
// 历史数据
// ============================================================================

int predictive_engine_add_history(PredictiveMaintenanceEngine* engine,
                                  const MaintenanceMetrics* metrics,
                                  uint64_t timestamp) {
    if (!engine || !metrics) {
        return -1;
    }
    
    TrendDataPoint point;
    point.timestamp = timestamp;
    point.metrics = *metrics;
    
    engine->history.push_back(point);
    
    // 保持数据量限制
    while (engine->history.size() > 365) {
        engine->history.erase(engine->history.begin());
    }
    
    return 0;
}

int predictive_engine_get_trend(PredictiveMaintenanceEngine* engine,
                               int metric_type,
                               uint64_t start_time,
                               uint64_t end_time,
                               float* values,
                               int max_count) {
    if (!engine || !values || max_count <= 0) {
        return -1;
    }
    
    std::vector<std::pair<uint64_t, float>> data_points;
    
    // 提取相关数据
    for (const auto& point : engine->history) {
        if (point.timestamp >= start_time && point.timestamp <= end_time) {
            float value = 0.0f;
            switch (metric_type) {
                case 0: value = point.metrics.current_luminance; break;
                case 1: value = point.metrics.delta_e; break;
                case 2: value = point.metrics.panel_temp_celsius; break;
                default: return -1;
            }
            data_points.push_back({point.timestamp, value});
        }
    }
    
    // 计算趋势斜率
    float slope = compute_slope(data_points);
    switch (metric_type) {
        case 0: engine->luminance_slope = slope; break;
        case 1: engine->delta_e_slope = slope; break;
        case 2: engine->temp_slope = slope; break;
    }
    
    // 填充输出值
    int count = std::min(static_cast<int>(data_points.size()), max_count);
    for (int i = 0; i < count; i++) {
        values[i] = data_points[i].second;
    }
    
    return count;
}

// ============================================================================
// 配置
// ============================================================================

int predictive_engine_set_thresholds(PredictiveMaintenanceEngine* engine,
                                     float luminance_min,
                                     float delta_e_max,
                                     float temp_max) {
    if (!engine) {
        return -1;
    }
    
    engine->luminance_min = luminance_min;
    engine->delta_e_max = delta_e_max;
    engine->temp_max = temp_max;
    
    return 0;
}

int predictive_engine_export_report(PredictiveMaintenanceEngine* engine,
                                    const char* report_path) {
    if (!engine || !report_path) {
        return -1;
    }
    
    char report_json[4096];
    if (predictive_engine_generate_qc_report(engine, report_json, sizeof(report_json)) != 0) {
        return -1;
    }
    
    // 在实际实现中，这里会写入文件
    (void)report_path;
    
    return 0;
}

} // extern "C"
