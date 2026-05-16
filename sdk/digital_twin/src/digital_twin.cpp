/**
 * @file digital_twin.cpp
 * @brief 数字孪生运维系统实现
 * 
 * 设备状态管理、预测性维护、告警和仿真
 */

#include "digital_twin.h"
#include <cstring>
#include <algorithm>
#include <vector>
#include <map>
#include <random>
#include <sstream>
#include <iomanip>
#include <cmath>

// ============================================================================
// 内部数据结构
// ============================================================================

struct DeviceRecord {
    DeviceInfo info;
    std::vector<DeviceMetrics> metrics_history;
    std::vector<AlertInfo> alerts;
    float health_score;
    
    DeviceRecord() : health_score(100.0f) {
        memset(&info, 0, sizeof(info));
    }
};

struct DigitalTwinEngine {
    char hospital_id[64];
    std::map<std::string, DeviceRecord> devices;
    uint32_t next_alert_id;
    std::mt19937 rng;
    
    DigitalTwinEngine(const char* hospital) : rng(std::random_device{}()) {
        strncpy(hospital_id, hospital ? hospital : "default", sizeof(hospital_id) - 1);
        next_alert_id = 1;
    }
};

// ============================================================================
// 辅助函数
// ============================================================================

static const char* device_state_to_string(DeviceState state) {
    switch (state) {
        case DEVICE_STATE_ONLINE: return "在线";
        case DEVICE_STATE_OFFLINE: return "离线";
        case DEVICE_STATE_MAINTENANCE: return "维护中";
        case DEVICE_STATE_ERROR: return "故障";
        case DEVICE_STATE_UPGRADING: return "升级中";
        default: return "未知";
    }
}

static const char* alert_severity_to_string(AlertSeverity severity) {
    switch (severity) {
        case ALERT_INFO: return "信息";
        case ALERT_WARNING: return "警告";
        case ALERT_ERROR: return "错误";
        case ALERT_CRITICAL: return "严重";
        default: return "无";
    }
}

static float compute_health_score(const DeviceMetrics& metrics, uint32_t uptime_hours) {
    float score = 100.0f;
    
    // 亮度影响
    if (metrics.luminance_max > 0) {
        float lum_ratio = metrics.luminance_current / metrics.luminance_max;
        if (lum_ratio < 0.8f) score -= (0.8f - lum_ratio) * 30;
    }
    
    // 色彩偏差影响
    if (metrics.delta_e > 3.0f) {
        score -= (metrics.delta_e - 3.0f) * 5;
    }
    
    // 温度影响
    if (metrics.temperature_celsius > 40.0f) {
        score -= (metrics.temperature_celsius - 40.0f) * 2;
    }
    
    // 使用率影响
    if (metrics.cpu_usage_percent > 80.0f) {
        score -= (metrics.cpu_usage_percent - 80.0f) * 0.3f;
    }
    
    // 运行时长影响 (磨损)
    if (uptime_hours > 10000) {
        score -= std::min(10.0f, (uptime_hours - 10000) * 0.001f);
    }
    
    return std::max(0.0f, std::min(100.0f, score));
}

static bool check_alert_conditions(const DeviceMetrics& metrics, DeviceState state) {
    // 检查是否需要告警
    if (state == DEVICE_STATE_OFFLINE) return true;
    if (metrics.luminance_max > 0 && 
        (metrics.luminance_current / metrics.luminance_max) < 0.6f) return true;
    if (metrics.delta_e > 5.0f) return true;
    if (metrics.temperature_celsius > 45.0f) return true;
    if (metrics.cpu_usage_percent > 95.0f) return true;
    return false;
}

// ============================================================================
// 引擎生命周期
// ============================================================================

extern "C" {

DigitalTwinEngine* dt_engine_create(const char* hospital_id) {
    return new (std::nothrow) DigitalTwinEngine(hospital_id);
}

void dt_engine_destroy(DigitalTwinEngine* engine) {
    delete engine;
}

// ============================================================================
// 设备管理
// ============================================================================

int dt_engine_register_device(DigitalTwinEngine* engine, const DeviceInfo* device) {
    if (!engine || !device) return -1;
    
    DeviceRecord record;
    record.info = *device;
    record.health_score = 100.0f;
    
    engine->devices[device->device_id] = record;
    
    return 0;
}

int dt_engine_update_device(DigitalTwinEngine* engine, const DeviceInfo* device) {
    if (!engine || !device) return -1;
    
    auto it = engine->devices.find(device->device_id);
    if (it == engine->devices.end()) {
        return dt_engine_register_device(engine, device);
    }
    
    it->second.info = *device;
    return 0;
}

int dt_engine_get_device(DigitalTwinEngine* engine, const char* device_id,
                          DeviceInfo* device) {
    if (!engine || !device || !device_id) return -1;
    
    auto it = engine->devices.find(device_id);
    if (it == engine->devices.end()) return -1;
    
    *device = it->second.info;
    return 0;
}

int dt_engine_get_all_devices(DigitalTwinEngine* engine,
                              DeviceInfo* devices,
                              int max_count) {
    if (!engine || !devices || max_count <= 0) return 0;
    
    int count = 0;
    for (const auto& pair : engine->devices) {
        if (count >= max_count) break;
        devices[count++] = pair.second.info;
    }
    
    return count;
}

// ============================================================================
// 指标管理
// ============================================================================

int dt_engine_report_metrics(DigitalTwinEngine* engine,
                            const char* device_id,
                            const DeviceMetrics* metrics) {
    if (!engine || !device_id || !metrics) return -1;
    
    auto it = engine->devices.find(device_id);
    if (it == engine->devices.end()) return -1;
    
    DeviceRecord& record = it->second;
    
    // 添加到历史
    record.metrics_history.push_back(*metrics);
    
    // 保持历史记录数量限制
    if (record.metrics_history.size() > 1000) {
        record.metrics_history.erase(record.metrics_history.begin());
    }
    
    // 更新健康评分
    record.health_score = compute_health_score(*metrics, record.info.total_uptime_hours);
    
    // 检查告警条件
    if (check_alert_conditions(*metrics, record.info.state)) {
        AlertInfo alert = {};
        alert.alert_id = engine->next_alert_id++;
        alert.device_id[0] = '\0';
        strncpy(alert.device_id, device_id, sizeof(alert.device_id) - 1);
        alert.timestamp = metrics->timestamp;
        
        if (metrics->luminance_max > 0 && 
            (metrics->luminance_current / metrics->luminance_max) < 0.6f) {
            alert.severity = ALERT_WARNING;
            snprintf(alert.message, sizeof(alert.message), "亮度低于60%%阈值");
            snprintf(alert.description, sizeof(alert.description),
                    "当前亮度 %.1f cd/m²，低于安全阈值", metrics->luminance_current);
        } else if (metrics->delta_e > 5.0f) {
            alert.severity = ALERT_ERROR;
            snprintf(alert.message, sizeof(alert.message), "色彩偏差超标");
            snprintf(alert.description, sizeof(alert.description),
                    "Delta E = %.2f，超过可接受范围", metrics->delta_e);
        } else if (metrics->temperature_celsius > 45.0f) {
            alert.severity = ALERT_CRITICAL;
            snprintf(alert.message, sizeof(alert.message), "温度过高");
            snprintf(alert.description, sizeof(alert.description),
                    "面板温度 %.1f°C，存在损坏风险", metrics->temperature_celsius);
        } else {
            alert.severity = ALERT_INFO;
            snprintf(alert.message, sizeof(alert.message), "设备状态变更");
            strncpy(alert.description, "设备状态异常", sizeof(alert.description) - 1);
        }
        
        record.alerts.push_back(alert);
    }
    
    return 0;
}

int dt_engine_get_metrics_history(DigitalTwinEngine* engine,
                                 const char* device_id,
                                 uint64_t start_time,
                                 uint64_t end_time,
                                 DeviceMetrics* metrics,
                                 int max_count) {
    if (!engine || !device_id || !metrics || max_count <= 0) return 0;
    
    auto it = engine->devices.find(device_id);
    if (it == engine->devices.end()) return 0;
    
    const std::vector<DeviceMetrics>& history = it->second.metrics_history;
    
    int count = 0;
    for (const auto& m : history) {
        if (count >= max_count) break;
        if (m.timestamp >= start_time && m.timestamp <= end_time) {
            metrics[count++] = m;
        }
    }
    
    return count;
}

// ============================================================================
// 告警管理
// ============================================================================

int dt_engine_get_active_alerts(DigitalTwinEngine* engine,
                                AlertInfo* alerts,
                                int max_count) {
    if (!engine || !alerts || max_count <= 0) return 0;
    
    int count = 0;
    for (const auto& pair : engine->devices) {
        for (const auto& alert : pair.second.alerts) {
            if (alert.resolved) continue;
            if (count >= max_count) break;
            alerts[count++] = alert;
        }
    }
    
    return count;
}

int dt_engine_acknowledge_alert(DigitalTwinEngine* engine, uint32_t alert_id) {
    if (!engine) return -1;
    
    for (auto& pair : engine->devices) {
        for (auto& alert : pair.second.alerts) {
            if (alert.alert_id == alert_id) {
                alert.acknowledged = true;
                return 0;
            }
        }
    }
    
    return -1;
}

int dt_engine_resolve_alert(DigitalTwinEngine* engine, uint32_t alert_id) {
    if (!engine) return -1;
    
    for (auto& pair : engine->devices) {
        for (auto& alert : pair.second.alerts) {
            if (alert.alert_id == alert_id) {
                alert.resolved = true;
                alert.resolved_time = 0;  // 当前时间
                return 0;
            }
        }
    }
    
    return -1;
}

// ============================================================================
// 预测性维护
// ============================================================================

int dt_engine_predict_failures(DigitalTwinEngine* engine,
                              const char* device_id,
                              FailurePrediction* predictions,
                              int max_count) {
    if (!engine || !device_id || !predictions || max_count <= 0) return 0;
    
    auto it = engine->devices.find(device_id);
    if (it == engine->devices.end()) return 0;
    
    const DeviceRecord& record = it->second;
    int count = 0;
    
    // 亮度故障预测
    if (record.metrics_history.size() > 0) {
        const DeviceMetrics& latest = record.metrics_history.back();
        
        if (latest.luminance_max > 0 && latest.luminance_current > 0) {
            float decay_rate = 0.001f;  // 每天衰减率 (示例)
            float days_to_min = (latest.luminance_current - 300.0f) / 
                               (latest.luminance_current * decay_rate);
            
            if (days_to_min > 0 && count < max_count) {
                strncpy(predictions[count].component, "背光模组", sizeof(predictions[count].component) - 1);
                predictions[count].days_to_failure = static_cast<uint16_t>(std::max(0.0f, days_to_min));
                predictions[count].failure_probability = std::min(1.0f, 1.0f / (days_to_min + 1));
                strncpy(predictions[count].recommended_action, "安排亮度校准或更换背光",
                        sizeof(predictions[count].recommended_action) - 1);
                predictions[count].prediction_time = 0;
                count++;
            }
        }
        
        // 色彩漂移预测
        if (latest.delta_e > 1.0f) {
            float days_to_limit = (latest.delta_e - 5.0f) / 0.05f;  // 每天增长0.05
            
            if (days_to_limit > 0 && count < max_count) {
                strncpy(predictions[count].component, "色彩校准",
                        sizeof(predictions[count].component) - 1);
                predictions[count].days_to_failure = static_cast<uint16_t>(std::max(0.0f, days_to_limit));
                predictions[count].failure_probability = std::min(1.0f, 1.0f / (days_to_limit + 1));
                strncpy(predictions[count].recommended_action, "安排专业校准",
                        sizeof(predictions[count].recommended_action) - 1);
                predictions[count].prediction_time = 0;
                count++;
            }
        }
        
        // 温度故障预测
        if (latest.temperature_celsius > 35.0f) {
            float days_to_limit = (latest.temperature_celsius - 45.0f) / 0.1f;
            
            if (days_to_limit > 0 && count < max_count) {
                strncpy(predictions[count].component, "散热系统",
                        sizeof(predictions[count].component) - 1);
                predictions[count].days_to_failure = static_cast<uint16_t>(std::max(0.0f, days_to_limit));
                predictions[count].failure_probability = std::min(1.0f, 1.0f / (days_to_limit + 1));
                strncpy(predictions[count].recommended_action, "检查散热风扇和环境温度",
                        sizeof(predictions[count].recommended_action) - 1);
                predictions[count].prediction_time = 0;
                count++;
            }
        }
    }
    
    return count;
}

int dt_engine_get_health_score(DigitalTwinEngine* engine, const char* device_id) {
    if (!engine || !device_id) return -1;
    
    auto it = engine->devices.find(device_id);
    if (it == engine->devices.end()) return -1;
    
    return static_cast<int>(it->second.health_score);
}

int dt_engine_get_maintenance_schedule(DigitalTwinEngine* engine,
                                     const char* device_id,
                                     char* schedule_json,
                                     size_t buffer_size) {
    if (!engine || !device_id || !schedule_json || buffer_size == 0) return -1;
    
    auto it = engine->devices.find(device_id);
    if (it == engine->devices.end()) return -1;
    
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"device_id\": \"" << device_id << "\",\n";
    oss << "  \"hospital_id\": \"" << engine->hospital_id << "\",\n";
    oss << "  \"schedule\": [\n";
    
    // 校准计划
    oss << "    {\"type\": \"校准\", \"interval_days\": 30, \"next_due\": "
        << (it->second.info.total_uptime_hours / 24 + 30) << "},\n";
    
    // 清洁计划
    oss << "    {\"type\": \"清洁\", \"interval_days\": 90, \"next_due\": 60},\n";
    
    // 全面检查
    oss << "    {\"type\": \"全面检查\", \"interval_days\": 365, \"next_due\": 300}\n";
    
    oss << "  ]\n";
    oss << "}\n";
    
    std::string result = oss.str();
    if (result.size() >= buffer_size) return -1;
    
    strncpy(schedule_json, result.c_str(), buffer_size - 1);
    schedule_json[buffer_size - 1] = '\0';
    
    return 0;
}

// ============================================================================
// 仿真
// ============================================================================

int dt_engine_simulate(DigitalTwinEngine* engine,
                      const char* device_id,
                      int hours,
                      DeviceMetrics* metrics,
                      int max_count) {
    if (!engine || !device_id || !metrics || max_count <= 0 || hours <= 0) return 0;
    
    auto it = engine->devices.find(device_id);
    if (it == engine->devices.end()) return 0;
    
    const DeviceInfo& device = it->second.info;
    
    std::normal_distribution<float> lum_dist(device.brightness_level, 5.0f);
    std::normal_distribution<float> temp_dist(35.0f, 2.0f);
    std::normal_distribution<float> de_dist(2.0f, 0.3f);
    
    int samples = std::min(hours, max_count);
    for (int i = 0; i < samples; i++) {
        DeviceMetrics& m = metrics[i];
        m.timestamp = 0 + i * 3600;  // 每小时一个样本
        
        m.luminance_current = std::max(0.0f, lum_dist(engine->rng));
        m.luminance_max = device.target_brightness > 0 ? device.target_brightness : 500.0f;
        m.delta_e = std::max(0.0f, de_dist(engine->rng));
        m.white_point_x = 0.3127f;
        m.white_point_y = 0.3290f;
        
        m.cpu_usage_percent = 20.0f + (engine->rng() % 30);
        m.memory_usage_percent = 40.0f + (engine->rng() % 20);
        m.gpu_usage_percent = 30.0f + (engine->rng() % 40);
        m.temperature_celsius = std::max(0.0f, temp_dist(engine->rng));
        m.power_watts = 50.0f + (engine->rng() % 30);
        
        m.network_latency_ms = 5.0f + (engine->rng() % 20);
        m.bytes_sent = 1000000 * (i + 1);
        m.bytes_received = 2000000 * (i + 1);
        
        m.inference_count = 100 + (engine->rng() % 50);
        m.avg_inference_ms = 15.0f + (engine->rng() % 10);
    }
    
    return samples;
}

int dt_engine_simulate_all(DigitalTwinEngine* engine,
                           const char** device_ids,
                           int device_count,
                           int hours) {
    if (!engine || !device_ids || device_count <= 0 || hours <= 0) return -1;
    
    DeviceMetrics metrics[168];  // 最多7天 * 24小时
    
    for (int i = 0; i < device_count; i++) {
        int count = dt_engine_simulate(engine, device_ids[i], hours, metrics, 168);
        if (count > 0) {
            // 存储仿真结果到设备记录
            auto it = engine->devices.find(device_ids[i]);
            if (it != engine->devices.end()) {
                for (int j = 0; j < count; j++) {
                    it->second.metrics_history.push_back(metrics[j]);
                }
            }
        }
    }
    
    return 0;
}

// ============================================================================
// 报表
// ============================================================================

int dt_engine_generate_status_report(DigitalTwinEngine* engine,
                                    char* report_json,
                                    size_t buffer_size) {
    if (!engine || !report_json || buffer_size == 0) return -1;
    
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"hospital_id\": \"" << engine->hospital_id << "\",\n";
    oss << "  \"timestamp\": " << 0 << ",\n";
    oss << "  \"summary\": {\n";
    oss << "    \"total_devices\": " << engine->devices.size() << ",\n";
    
    int online_count = 0, offline_count = 0, error_count = 0;
    float avg_health = 0.0f;
    
    for (const auto& pair : engine->devices) {
        if (pair.second.info.state == DEVICE_STATE_ONLINE) online_count++;
        else if (pair.second.info.state == DEVICE_STATE_OFFLINE) offline_count++;
        else if (pair.second.info.state == DEVICE_STATE_ERROR) error_count++;
        avg_health += pair.second.health_score;
    }
    
    if (!engine->devices.empty()) {
        avg_health /= engine->devices.size();
    }
    
    oss << "    \"online_devices\": " << online_count << ",\n";
    oss << "    \"offline_devices\": " << offline_count << ",\n";
    oss << "    \"error_devices\": " << error_count << ",\n";
    oss << "    \"average_health_score\": " << avg_health << "\n";
    oss << "  },\n";
    
    // 设备列表
    oss << "  \"devices\": [\n";
    bool first = true;
    for (const auto& pair : engine->devices) {
        if (!first) oss << ",\n";
        first = false;
        
        oss << "    {\"id\": \"" << pair.first << "\", ";
        oss << "\"name\": \"" << pair.second.info.device_name << "\", ";
        oss << "\"state\": \"" << device_state_to_string(pair.second.info.state) << "\", ";
        oss << "\"health_score\": " << pair.second.health_score << "}";
    }
    oss << "\n  ]\n";
    oss << "}\n";
    
    std::string result = oss.str();
    if (result.size() >= buffer_size) return -1;
    
    strncpy(report_json, result.c_str(), buffer_size - 1);
    report_json[buffer_size - 1] = '\0';
    
    return 0;
}

int dt_engine_generate_health_trend(DigitalTwinEngine* engine,
                                     const char* device_id,
                                     int days,
                                     char* report_json,
                                     size_t buffer_size) {
    if (!engine || !device_id || !report_json || buffer_size == 0) return -1;
    
    auto it = engine->devices.find(device_id);
    if (it == engine->devices.end()) return -1;
    
    const std::vector<DeviceMetrics>& history = it->second.metrics_history;
    
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"device_id\": \"" << device_id << "\",\n";
    oss << "  \"period_days\": " << days << ",\n";
    oss << "  \"data_points\": [\n";
    
    bool first = true;
    for (const auto& m : history) {
        if (!first) oss << ",\n";
        first = false;
        
        oss << "    {\"timestamp\": " << m.timestamp << ", ";
        oss << "\"luminance\": " << m.luminance_current << ", ";
        oss << "\"delta_e\": " << m.delta_e << ", ";
        oss << "\"temperature\": " << m.temperature_celsius << "}";
    }
    
    oss << "\n  ]\n";
    oss << "}\n";
    
    std::string result = oss.str();
    if (result.size() >= buffer_size) return -1;
    
    strncpy(report_json, result.c_str(), buffer_size - 1);
    report_json[buffer_size - 1] = '\0';
    
    return 0;
}

} // extern "C"
