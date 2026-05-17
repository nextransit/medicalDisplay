/**
 * @file ambient_light_sensor.cpp
 * @brief 环境光自适应传感器实现
 */

#include "ambient_light_sensor.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>

// ============================================================================
// 内部数据结构
// ============================================================================

struct ALSSensor {
    ALSConfig config;
    std::vector<float> lux_history;
    size_t history_index;
    float current_lux;
    uint64_t last_read_us;
};

// ============================================================================
// 生命周期
// ============================================================================

ALSSensor* als_sensor_create(const ALSConfig* config) {
    if (!config) return nullptr;

    auto* sensor = new (std::nothrow) ALSSensor();
    if (!sensor) return nullptr;

    sensor->config = *config;
    sensor->current_lux = 10.0f;  // 默认诊断室环境
    sensor->last_read_us = 0;

    if (sensor->config.enable_averaging) {
        sensor->lux_history.resize(sensor->config.average_window > 0 ?
                                   sensor->config.average_window : 8, 10.0f);
    }
    sensor->history_index = 0;

    return sensor;
}

void als_sensor_destroy(ALSSensor* sensor) {
    delete sensor;
}

// ============================================================================
// 读数
// ============================================================================

int als_sensor_read(ALSSensor* sensor, ALSReading* reading) {
    if (!sensor || !reading) return -1;

    memset(reading, 0, sizeof(*reading));

    // 尝试读取真实 I2C 传感器
    float lux = 0.0f;

    switch (sensor->config.sensor_type) {
        case ALS_SOFTWARE_ESTIMATE:
            // 软件估算 (基于摄像头或时间)
            lux = 10.0f;  // 返回模拟值
            break;

        case ALS_VEML7700:
        case ALS_TCS34725:
        case ALS_OPT3001:
        case ALS_BH1750:
            // 真实 I2C 传感器读取
            // 在实际硬件上通过 /dev/i2c-* 读取
            // 这里返回一个模拟值作为 fallback
            lux = 100.0f;  // 模拟办公室环境
            break;

        default:
            lux = 10.0f;
            break;
    }

    reading->lux = lux;
    reading->timestamp_us = 0;  // 模拟时间戳

    // 更新滑动平均历史
    if (sensor->config.enable_averaging && !sensor->lux_history.empty()) {
        sensor->lux_history[sensor->history_index % sensor->lux_history.size()] = lux;
        sensor->history_index++;
    }

    sensor->current_lux = lux;
    return 0;
}

float als_sensor_get_average_lux(ALSSensor* sensor) {
    if (!sensor) return 0.0f;

    if (!sensor->config.enable_averaging || sensor->lux_history.empty()) {
        return sensor->current_lux;
    }

    float sum = 0.0f;
    size_t count = std::min(sensor->lux_history.size(), sensor->history_index);
    if (count == 0) return sensor->current_lux;

    for (size_t i = 0; i < count; i++) {
        sum += sensor->lux_history[i];
    }
    return sum / static_cast<float>(count);
}

// ============================================================================
// 自适应计算
// ============================================================================

int als_compute_adjustment(float lux,
                           const ALSAdaptiveConfig* config,
                           ALSDisplayAdjustment* adjustment) {
    if (!config || !adjustment) return -1;

    memset(adjustment, 0, sizeof(*adjustment));

    // 确保 lux 在有效范围内
    float clamped_lux = std::max(config->min_lux, std::min(config->max_lux, lux));

    float luminance = 0.0f;
    float ratio = 0.0f;

    switch (config->strategy) {
        case ALS_STRATEGY_LINEAR: {
            // 线性映射: L = L_min + (lux - lux_min) / (lux_max - lux_min) * (L_max - L_min)
            float t = (clamped_lux - config->min_lux) / (config->max_lux - config->min_lux);
            luminance = config->min_luminance + t * (config->max_luminance - config->min_luminance);
            ratio = t;
            break;
        }

        case ALS_STRATEGY_STEPPED: {
            // DICOM 推荐阶梯映射
            // < 10 lux: 诊断室 (350-450 cd/m²)
            // 10-50 lux: 阅片室 (450-550)
            // 50-100 lux: 会诊室 (550-700)
            // > 100 lux: 不建议
            if (lux < 10.0f) {
                luminance = 400.0f; ratio = 0.3f;
            } else if (lux < 30.0f) {
                luminance = 450.0f; ratio = 0.45f;
            } else if (lux < 50.0f) {
                luminance = 500.0f; ratio = 0.6f;
            } else if (lux < 75.0f) {
                luminance = 600.0f; ratio = 0.75f;
            } else if (lux < 100.0f) {
                luminance = 700.0f; ratio = 0.85f;
            } else {
                luminance = config->max_luminance; ratio = 1.0f;
            }
            break;
        }

        case ALS_STRATEGY_SIGMOID: {
            // S型曲线: 平滑过渡
            float mid = (config->min_lux + config->max_lux) * 0.5f;
            float steepness = 6.0f / (config->max_lux - config->min_lux);
            float sigmoid = 1.0f / (1.0f + std::exp(-steepness * (clamped_lux - mid)));
            luminance = config->min_luminance + sigmoid * (config->max_luminance - config->min_luminance);
            ratio = sigmoid;
            break;
        }

        case ALS_STRATEGY_CUSTOM:
        default:
            luminance = config->max_luminance * config->target_luminance_ratio;
            ratio = config->target_luminance_ratio;
            break;
    }

    adjustment->recommended_luminance = luminance;
    adjustment->luminance_ratio = ratio;

    // 色温调整 (如需要)
    if (config->adjust_color_temp) {
        // 环境光色温与显示色温匹配
        adjustment->recommended_color_temp = config->color_temp_target;
    } else {
        adjustment->recommended_color_temp = 6500.0f;  // D65 标准
    }

    // 预估 ΔE
    adjustment->delta_e_estimate = 0.5f + ratio * 1.5f;

    // 是否需要校准
    adjustment->needs_calibration = (ratio > 0.8f);

    // 生成描述
    if (lux < 10.0f) {
        snprintf(adjustment->description, sizeof(adjustment->description),
                "暗室环境 (%.1f lux)，建议亮度 %.0f cd/m²", lux, luminance);
    } else if (lux < 50.0f) {
        snprintf(adjustment->description, sizeof(adjustment->description),
                "标准阅片环境 (%.1f lux)，亮度 %.0f cd/m²", lux, luminance);
    } else if (lux < 100.0f) {
        snprintf(adjustment->description, sizeof(adjustment->description),
                "会诊环境 (%.1f lux)，亮度 %.0f cd/m²", lux, luminance);
    } else {
        snprintf(adjustment->description, sizeof(adjustment->description),
                "高亮环境 (%.1f lux)，建议降低环境光或使用遮光罩", lux);
    }

    return 0;
}

// ============================================================================
// 预设配置
// ============================================================================

ALSAdaptiveConfig als_preset_diagnostic(void) {
    ALSAdaptiveConfig config = {};
    config.strategy = ALS_STRATEGY_STEPPED;
    config.min_luminance = 350.0f;
    config.max_luminance = 550.0f;
    config.min_lux = 1.0f;
    config.max_lux = 50.0f;
    config.target_luminance_ratio = 0.7f;
    config.adjust_gsdf = true;
    config.adjust_color_temp = true;
    config.color_temp_target = 6500.0f;
    return config;
}

ALSAdaptiveConfig als_preset_surgical(void) {
    ALSAdaptiveConfig config = {};
    config.strategy = ALS_STRATEGY_LINEAR;
    config.min_luminance = 400.0f;
    config.max_luminance = 800.0f;
    config.min_lux = 50.0f;
    config.max_lux = 1000.0f;
    config.target_luminance_ratio = 0.8f;
    config.adjust_gsdf = false;
    config.adjust_color_temp = true;
    config.color_temp_target = 5500.0f;  // 手术室偏冷色调
    return config;
}

ALSAdaptiveConfig als_preset_consultation(void) {
    ALSAdaptiveConfig config = {};
    config.strategy = ALS_STRATEGY_SIGMOID;
    config.min_luminance = 250.0f;
    config.max_luminance = 500.0f;
    config.min_lux = 5.0f;
    config.max_lux = 200.0f;
    config.target_luminance_ratio = 0.6f;
    config.adjust_gsdf = true;
    config.adjust_color_temp = false;
    config.color_temp_target = 6500.0f;
    return config;
}

// ============================================================================
// 软件估算
// ============================================================================

float als_estimate_from_time(int hour, float latitude) {
    (void)latitude;  // 未来可基于位置+时间计算太阳高度角

    // 简化模型: 基于小时的照度曲线
    // 凌晨: 0 lux, 日出 6h: 50 lux, 中午 12h: 200 lux, 日落 18h: 50 lux
    if (hour < 6 || hour > 20) {
        return 1.0f;   // 夜间
    } else if (hour < 8 || hour > 18) {
        return 50.0f;  // 晨昏
    } else if (hour < 10 || hour > 16) {
        return 150.0f; // 上午/下午
    } else {
        return 200.0f; // 正午
    }
}
