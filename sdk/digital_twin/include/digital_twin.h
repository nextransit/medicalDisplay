#ifndef DIGITAL_TWIN_H
#define DIGITAL_TWIN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 设备状态枚举
// ============================================================================
typedef enum {
    DEVICE_STATE_UNKNOWN = 0,
    DEVICE_STATE_ONLINE,
    DEVICE_STATE_OFFLINE,
    DEVICE_STATE_MAINTENANCE,
    DEVICE_STATE_ERROR,
    DEVICE_STATE_UPGRADING
} DeviceState;

// ============================================================================
// 设备型号
// ============================================================================
typedef enum {
    DEVICE_MODEL_DIAGNOSTIC = 0,  // 诊断显示器
    DEVICE_MODEL_CLINICAL,          // 临床显示器
    DEVICE_MODEL_SURGICAL,          // 手术显示器
    DEVICE_MODEL_PACS_WORKSTATION   // PACS工作站
} DeviceModel;

// ============================================================================
// 设备信息
// ============================================================================
typedef struct {
    char device_id[64];           // 设备唯一ID
    char device_name[128];         // 设备名称
    DeviceModel model;             // 型号
    DeviceState state;             // 状态
    char hospital_id[64];          // 医院ID
    char department[64];           // 科室
    char ip_address[32];          // IP地址
    char mac_address[32];          // MAC地址
    char firmware_version[32];     // 固件版本
    char hardware_version[32];     // 硬件版本
    uint64_t install_date;         // 安装日期
    uint64_t last_seen;            // 最后在线时间
    uint64_t total_uptime_hours;   // 累计运行时间
    int32_t brightness_level;      // 当前亮度
    int32_t target_brightness;     // 目标亮度
    float panel_temperature;       // 面板温度
    float ambient_temperature;     // 环境温度
    float humidity_percent;        // 环境湿度
} DeviceInfo;

// ============================================================================
// 性能指标
// ============================================================================
typedef struct {
    uint64_t timestamp;           // 时间戳
    
    // 显示性能
    float luminance_current;       // 当前亮度 (cd/m²)
    float luminance_max;           // 最大亮度
    float delta_e;                // 色彩偏差
    float white_point_x;          // 白点X
    float white_point_y;          // 白点Y
    
    // 运行状态
    float cpu_usage_percent;       // CPU使用率
    float memory_usage_percent;    // 内存使用率
    float gpu_usage_percent;       // GPU使用率
    float temperature_celsius;     // 温度
    float power_watts;           // 功耗 (W)
    
    // 网络
    float network_latency_ms;     // 网络延迟
    uint64_t bytes_sent;          // 发送字节数
    uint64_t bytes_received;     // 接收字节数
    
    // AI推理
    uint32_t inference_count;     // 推理次数
    float avg_inference_ms;       // 平均推理延迟
} DeviceMetrics;

// ============================================================================
// 告警信息
// ============================================================================
typedef enum {
    ALERT_NONE = 0,
    ALERT_INFO,
    ALERT_WARNING,
    ALERT_ERROR,
    ALERT_CRITICAL
} AlertSeverity;

typedef struct {
    uint32_t alert_id;           // 告警ID
    AlertSeverity severity;        // 严重程度
    char device_id[64];           // 设备ID
    uint64_t timestamp;            // 发生时间
    char message[512];            // 告警消息
    char description[1024];        // 详细描述
    bool acknowledged;            // 是否已确认
    bool resolved;               // 是否已解决
    uint64_t resolved_time;       // 解决时间
} AlertInfo;

// ============================================================================
// 预测结果
// ============================================================================
typedef struct {
    char component[64];           // 组件名称
    uint16_t days_to_failure;    // 预计故障天数
    float failure_probability;    // 故障概率
    char recommended_action[256]; // 建议操作
    uint64_t prediction_time;    // 预测时间
} FailurePrediction;

// ============================================================================
// 数字孪生引擎句柄
// ============================================================================
typedef struct DigitalTwinEngine DigitalTwinEngine;

// ============================================================================
// 引擎生命周期
// ============================================================================

/**
 * 创建数字孪生引擎
 * @param hospital_id 医院ID
 * @return 引擎句柄，失败返回NULL
 */
DigitalTwinEngine* dt_engine_create(const char* hospital_id);

/**
 * 销毁数字孪生引擎
 * @param engine 引擎句柄
 */
void dt_engine_destroy(DigitalTwinEngine* engine);

// ============================================================================
// 设备管理
// ============================================================================

/**
 * 注册设备
 * @param engine 引擎句柄
 * @param device 设备信息
 * @return 0成功，-1失败
 */
int dt_engine_register_device(DigitalTwinEngine* engine, const DeviceInfo* device);

/**
 * 更新设备信息
 * @param engine 引擎句柄
 * @param device 设备信息
 * @return 0成功，-1失败
 */
int dt_engine_update_device(DigitalTwinEngine* engine, const DeviceInfo* device);

/**
 * 获取设备信息
 * @param engine 引擎句柄
 * @param device_id 设备ID
 * @param device 输出设备信息
 * @return 0成功，-1失败
 */
int dt_engine_get_device(DigitalTwinEngine* engine, const char* device_id,
                         DeviceInfo* device);

/**
 * 获取所有设备
 * @param engine 引擎句柄
 * @param devices 输出数组
 * @param max_count 最大数量
 * @return 设备数量
 */
int dt_engine_get_all_devices(DigitalTwinEngine* engine,
                               DeviceInfo* devices,
                               int max_count);

// ============================================================================
// 指标管理
// ============================================================================

/**
 * 上报设备指标
 * @param engine 引擎句柄
 * @param device_id 设备ID
 * @param metrics 指标数据
 * @return 0成功，-1失败
 */
int dt_engine_report_metrics(DigitalTwinEngine* engine,
                           const char* device_id,
                           const DeviceMetrics* metrics);

/**
 * 获取设备历史指标
 * @param engine 引擎句柄
 * @param device_id 设备ID
 * @param start_time 开始时间
 * @param end_time 结束时间
 * @param metrics 输出数组
 * @param max_count 最大数量
 * @return 实际数量
 */
int dt_engine_get_metrics_history(DigitalTwinEngine* engine,
                                  const char* device_id,
                                  uint64_t start_time,
                                  uint64_t end_time,
                                  DeviceMetrics* metrics,
                                  int max_count);

// ============================================================================
// 告警管理
// ============================================================================

/**
 * 获取活动告警
 * @param engine 引擎句柄
 * @param alerts 输出数组
 * @param max_count 最大数量
 * @return 告警数量
 */
int dt_engine_get_active_alerts(DigitalTwinEngine* engine,
                               AlertInfo* alerts,
                               int max_count);

/**
 * 确认告警
 * @param engine 引擎句柄
 * @param alert_id 告警ID
 * @return 0成功
 */
int dt_engine_acknowledge_alert(DigitalTwinEngine* engine, uint32_t alert_id);

/**
 * 解决告警
 * @param engine 引擎句柄
 * @param alert_id 告警ID
 * @return 0成功
 */
int dt_engine_resolve_alert(DigitalTwinEngine* engine, uint32_t alert_id);

// ============================================================================
// 预测性维护
// ============================================================================

/**
 * 执行故障预测
 * @param engine 引擎句柄
 * @param device_id 设备ID
 * @param predictions 输出预测数组
 * @param max_count 最大数量
 * @return 预测数量
 */
int dt_engine_predict_failures(DigitalTwinEngine* engine,
                              const char* device_id,
                              FailurePrediction* predictions,
                              int max_count);

/**
 * 获取健康评分
 * @param engine 引擎句柄
 * @param device_id 设备ID
 * @return 健康评分 (0-100)，-1失败
 */
int dt_engine_get_health_score(DigitalTwinEngine* engine, const char* device_id);

/**
 * 获取维护计划
 * @param engine 引擎句柄
 * @param device_id 设备ID
 * @param schedule_json 输出JSON
 * @param buffer_size 缓冲区大小
 * @return 0成功
 */
int dt_engine_get_maintenance_schedule(DigitalTwinEngine* engine,
                                      const char* device_id,
                                      char* schedule_json,
                                      size_t buffer_size);

// ============================================================================
// 仿真
// ============================================================================

/**
 * 运行设备仿真
 * @param engine 引擎句柄
 * @param device_id 设备ID
 * @param hours 仿真时长 (小时)
 * @param metrics 输出指标数组
 * @param max_count 最大数量
 * @return 生成的指标数量
 */
int dt_engine_simulate(DigitalTwinEngine* engine,
                      const char* device_id,
                      int hours,
                      DeviceMetrics* metrics,
                      int max_count);

/**
 * 运行批量仿真
 * @param engine 引擎句柄
 * @param device_ids 设备ID数组
 * @param device_count 设备数量
 * @param hours 仿真时长
 * @return 0成功
 */
int dt_engine_simulate_all(DigitalTwinEngine* engine,
                           const char** device_ids,
                           int device_count,
                           int hours);

// ============================================================================
// 报表
// ============================================================================

/**
 * 生成设备状态报表
 * @param engine 引擎句柄
 * @param report_json 输出JSON
 * @param buffer_size 缓冲区大小
 * @return 0成功
 */
int dt_engine_generate_status_report(DigitalTwinEngine* engine,
                                   char* report_json,
                                   size_t buffer_size);

/**
 * 生成健康趋势报表
 * @param engine 引擎句柄
 * @param device_id 设备ID
 * @param days 报表天数
 * @param report_json 输出JSON
 * @param buffer_size 缓冲区大小
 * @return 0成功
 */
int dt_engine_generate_health_trend(DigitalTwinEngine* engine,
                                   const char* device_id,
                                   int days,
                                   char* report_json,
                                   size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // DIGITAL_TWIN_H
