#ifndef PREDICTIVE_MAINTENANCE_H
#define PREDICTIVE_MAINTENANCE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 设备类型
// ============================================================================
typedef enum {
    DEVICE_TYPE_DIAGNOSTIC = 0,   // 诊断显示器
    DEVICE_TYPE_CLINICAL,          // 临床显示器
    DEVICE_TYPE_SURGICAL,          // 手术显示器
    DEVICE_TYPE_COUNT
} DeviceType;

// ============================================================================
// 预测性维护指标
// ============================================================================
typedef struct {
    // 亮度相关
    float current_luminance;        // 当前亮度 (cd/m²)
    float max_luminance;           // 标称最大亮度
    float luminance_ratio;         // 当前/最大比值
    float luminance_drift_percent; // 亮度漂移百分比
    
    // 色彩相关
    float delta_e;                 // 色彩偏差 (Delta E)
    float white_point_x;           // 白点X
    float white_point_y;           // 白点Y
    float color_temp_kelvin;       // 色温 (K)
    
    // 运行时长
    uint32_t backlight_hours;     // 背光使用时长 (小时)
    uint32_t power_on_hours;       // 累计开机时长
    float daily_usage_hours;       // 日均使用时长
    
    // 温度
    float ambient_temp_celsius;    // 环境温度
    float panel_temp_celsius;     // 面板温度
    float peak_temp_celsius;      // 峰值温度
    
    // 校准
    uint16_t calibration_age_days; // 距上次校准天数
    uint16_t recommended_calibration_interval_days; // 推荐校准周期
    float calibration_quality_score; // 校准质量评分 (0-100)
    
    // 使用模式
    float peak_usage_ratio;        // 峰值使用占比
    float hdr_usage_ratio;         // HDR使用占比
    
    // 面板状态
    uint16_t dead_pixel_count;    // 死像素数量
    float uniformity_score;        // 均匀性评分
} MaintenanceMetrics;

// ============================================================================
// 预测结果
// ============================================================================
typedef enum {
    HEALTH_STATUS_EXCELLENT = 0,  // 优秀
    HEALTH_STATUS_GOOD,            // 良好
    HEALTH_STATUS_FAIR,            // 一般
    HEALTH_STATUS_POOR,            // 较差
    HEALTH_STATUS_CRITICAL        // 危急
} HealthStatus;

typedef struct {
    HealthStatus overall_status;    // 总体健康状态
    float health_score;            // 健康评分 (0-100)
    float risk_score;              // 风险评分 (0-100)
    
    // 预测寿命
    uint16_t predicted_luminance_failure_days;    // 亮度故障预测天数
    uint16_t predicted_color_failure_days;       // 色彩故障预测天数
    uint16_t predicted_calibration_due_days;      // 距下次校准建议天数
    
    // 问题列表
    uint8_t num_issues;           // 问题数量
    char issues[10][128];         // 问题描述
    float issue_severity[10];     // 问题严重度 (0-1)
    
    // 置信度
    float prediction_confidence;   // 预测置信度
    uint8_t model_version;         // 使用的模型版本
} PredictionResult;

// ============================================================================
// 维护建议
// ============================================================================
typedef enum {
    ACTION_NONE = 0,
    ACTION_MONITOR,               // 继续监控
    ACTION_SCHEDULE_CALIBRATION,  // 安排校准
    ACTION_REDUCE_BRIGHTNESS,     // 降低亮度
    ACTION_SCHEDULE_INSPECTION,   // 安排检查
    ACTION_REPLACE_PANEL,         // 更换面板
    ACTION_URGENT_REPLACEMENT    // 紧急更换
} MaintenanceAction;

typedef struct {
    MaintenanceAction action;      // 建议操作
    int priority;                  // 优先级 (1=最高)
    char title[128];              // 建议标题
    char description[512];        // 详细描述
    char rationale[256];          // 理由
    uint16_t recommended_days;    // 建议执行天数
    float estimated_cost_usd;      // 预估成本
    char procedure_id[64];        // 操作规程ID
} MaintenanceRecommendation;

// ============================================================================
// 预测性维护引擎句柄
// ============================================================================
typedef struct PredictiveMaintenanceEngine PredictiveMaintenanceEngine;

// ============================================================================
// 引擎生命周期
// ============================================================================

/**
 * 创建预测性维护引擎
 * @param device_type 设备类型
 * @return 引擎句柄，失败返回NULL
 */
PredictiveMaintenanceEngine* predictive_engine_create(DeviceType device_type);

/**
 * 销毁预测性维护引擎
 * @param engine 引擎句柄
 */
void predictive_engine_destroy(PredictiveMaintenanceEngine* engine);

/**
 * 更新设备指标
 * @param engine 引擎句柄
 * @param metrics 设备指标
 * @return 0成功，-1失败
 */
int predictive_engine_update_metrics(PredictiveMaintenanceEngine* engine,
                                     const MaintenanceMetrics* metrics);

/**
 * 获取设备指标
 * @param engine 引擎句柄
 * @param metrics 输出指标
 * @return 0成功，-1失败
 */
int predictive_engine_get_metrics(PredictiveMaintenanceEngine* engine,
                                  MaintenanceMetrics* metrics);

// ============================================================================
// 预测分析
// ============================================================================

/**
 * 执行健康预测分析
 * @param engine 引擎句柄
 * @param result 输出预测结果
 * @return 0成功，-1失败
 */
int predictive_engine_analyze(PredictiveMaintenanceEngine* engine,
                             PredictionResult* result);

/**
 * 获取维护建议
 * @param engine 引擎句柄
 * @param recommendations 输出建议数组
 * @param max_count 最大建议数量
 * @return 建议数量，-1失败
 */
int predictive_engine_get_recommendations(PredictiveMaintenanceEngine* engine,
                                          MaintenanceRecommendation* recommendations,
                                          int max_count);

/**
 * 生成质控报告
 * @param engine 引擎句柄
 * @param report_json 输出JSON报告
 * @param buffer_size 缓冲区大小
 * @return 0成功，-1失败
 */
int predictive_engine_generate_qc_report(PredictiveMaintenanceEngine* engine,
                                         char* report_json,
                                         size_t buffer_size);

// ============================================================================
// 历史数据管理
// ============================================================================

/**
 * 添加历史数据点
 * @param engine 引擎句柄
 * @param metrics 指标数据
 * @param timestamp 时间戳 (Unix epoch)
 * @return 0成功，-1失败
 */
int predictive_engine_add_history(PredictiveMaintenanceEngine* engine,
                                  const MaintenanceMetrics* metrics,
                                  uint64_t timestamp);

/**
 * 获取趋势数据
 * @param engine 引擎句柄
 * @param metric_type 指标类型 (0=luminance, 1=delta_e, 2=temp)
 * @param start_time 开始时间
 * @param end_time 结束时间
 * @param values 输出值数组
 * @param max_count 最大数量
 * @return 实际数量，-1失败
 */
int predictive_engine_get_trend(PredictiveMaintenanceEngine* engine,
                                int metric_type,
                                uint64_t start_time,
                                uint64_t end_time,
                                float* values,
                                int max_count);

// ============================================================================
// 配置
// ============================================================================

/**
 * 设置报警阈值
 * @param engine 引擎句柄
 * @param luminance_min 最小亮度 (cd/m²)
 * @param delta_e_max 最大Delta E
 * @param temp_max 最大温度 (°C)
 * @return 0成功
 */
int predictive_engine_set_thresholds(PredictiveMaintenanceEngine* engine,
                                      float luminance_min,
                                      float delta_e_max,
                                      float temp_max);

/**
 * 导出设备健康报告
 * @param engine 引擎句柄
 * @param report_path 报告路径
 * @return 0成功，-1失败
 */
int predictive_engine_export_report(PredictiveMaintenanceEngine* engine,
                                    const char* report_path);

#ifdef __cplusplus
}
#endif

#endif // PREDICTIVE_MAINTENANCE_H
