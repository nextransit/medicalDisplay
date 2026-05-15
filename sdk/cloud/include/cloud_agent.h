#ifndef CLOUD_AGENT_H
#define CLOUD_AGENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 云端连接状态
// ============================================================================
typedef enum {
    CLOUD_STATE_DISCONNECTED = 0,
    CLOUD_STATE_CONNECTING,
    CLOUD_STATE_CONNECTED,
    CLOUD_STATE_ERROR
} CloudState;

// ============================================================================
// 设备信息
// ============================================================================
typedef struct {
    char        device_id[128];        // 设备唯一ID
    char        model[64];             // 设备型号
    char        firmware_version[32];   // 固件版本
    char        hardware_version[32];  // 硬件版本
    char        hospital_id[64];       // 医院ID
    char        department[64];        // 科室
    double      latitude;              // 位置
    double      longitude;
} DeviceInfo;

// ============================================================================
// OTA更新信息
// ============================================================================
typedef struct {
    char        version[32];           // 新版本号
    char        release_date[32];      // 发布日期
    size_t      full_size;             // 完整包大小
    size_t      delta_size;            // 增量包大小
    char        checksum[128];         // SHA256校验和
    char        signature[1024];       // Detached signature (Base64)
    char        download_url[512];     // 下载URL
    char        delta_url[512];        // 差分包URL
    char        base_version[32];      // 差分更新基线版本
    char        install_path[512];     // 目标安装路径
    char        changelog[2048];       // 更新日志
    bool        is_mandatory;          // 强制更新
    bool        is_security_update;    // 安全更新
} UpdateInfo;

// ============================================================================
// 质控报告
// ============================================================================
typedef struct {
    char        report_id[64];
    char        device_id[64];
    char        timestamp[32];
    float       delta_e;               // 色彩偏差
    float       luminance;             // 亮度 (cd/m²)
    float       backlight_hours;       // 背光使用时长
    float       temperature;           // 运行温度
    float       health_score;          // 健康评分 (0-100)
    char        recommendations[1024];  // 建议
} QCReport;

// ============================================================================
// 推理统计
// ============================================================================
typedef struct {
    uint64_t    total_inferences;     // 总推理次数
    uint64_t    successful_inferences; // 成功次数
    float       avg_latency_ms;        // 平均延迟
    float       p50_latency_ms;        // P50延迟
    float       p95_latency_ms;        // P95延迟
    float       p99_latency_ms;        // P99延迟
    uint64_t    modality_counts[13];   // 各模态识别次数
} InferenceStats;

// ============================================================================
// 遥测数据
// ============================================================================
typedef struct {
    char        timestamp[32];
    float       hours_used;
    float       ai_inference_latency_ms;
    float       avg_fps;
    float       cpu_usage_percent;
    float       memory_usage_mb;
    float       gpu_temperature_c;
    uint64_t    network_tx_bytes;
    uint64_t    network_rx_bytes;
    uint32_t    error_count;
} CloudTelemetry;

// ============================================================================
// 模型更新信息
// ============================================================================
typedef struct {
    char        model_id[64];
    char        version[32];
    char        base_version[32];
    size_t      full_size;
    size_t      delta_size;
    char        checksum[128];
    char        signature[1024];
    char        download_url[512];
    char        delta_url[512];
    char        install_path[512];
    char        release_notes[1024];
    bool        is_mandatory;
} CloudModelInfo;

// ============================================================================
// 远程命令
// ============================================================================
typedef struct {
    char        command_id[64];
    char        command_type[64];
    char        correlation_id[64];
    char        payload[2048];
    char        received_at[32];
    bool        requires_response;
} CloudCommand;

typedef struct {
    char        command_id[64];
    int         status_code;
    char        message[256];
    char        payload[2048];
} CloudCommandResponse;

// ============================================================================
// 校准报告
// ============================================================================
typedef struct {
    char        calibration_id[64];
    char        device_id[128];
    char        timestamp[32];
    float       delta_e;
    float       luminance;
    float       uniformity;
    float       gamma;
    char        report_json[4096];
} CloudCalibrationReport;

// ============================================================================
// 云端Agent配置
// ============================================================================
typedef struct {
    char        server_url[512];       // 服务器地址
    char        api_key[256];          // API密钥
    int         reconnect_interval_sec; // 重连间隔
    int         stats_report_interval_sec; // 统计上报间隔
    bool        enable_ota;             // 启用OTA
    bool        enable_federated_learning; // 启用联邦学习
    size_t      max_cache_mb;           // 最大缓存 (MB)
    int         heartbeat_interval_sec; // 心跳间隔
    int         telemetry_batch_size;   // 遥测批大小
    int         command_timeout_ms;     // 命令等待超时
    char        staging_dir[512];       // 下载/回滚缓存目录
    char        public_key_path[512];   // OTA/模型验签公钥
} CloudAgentConfig;

// ============================================================================
// 云端Agent句柄
// ============================================================================
typedef struct CloudAgent CloudAgent;

// ============================================================================
// 生命周期
// ============================================================================

/**
 * 创建云端Agent
 * @param config 配置
 * @param device_info 设备信息
 * @return Agent句柄
 */
CloudAgent* cloud_agent_create(const CloudAgentConfig* config, const DeviceInfo* device_info);

/**
 * 销毁云端Agent
 * @param agent Agent句柄
 */
void cloud_agent_destroy(CloudAgent* agent);

/**
 * 连接到云端
 * @param agent Agent句柄
 * @return 0成功
 */
int cloud_agent_connect(CloudAgent* agent);

/**
 * 断开连接
 * @param agent Agent句柄
 */
void cloud_agent_disconnect(CloudAgent* agent);

/**
 * 获取连接状态
 * @param agent Agent句柄
 * @return 状态
 */
CloudState cloud_agent_get_state(CloudAgent* agent);

// ============================================================================
// 注册与遥测
// ============================================================================

/**
 * 设备注册
 * @param agent Agent句柄
 * @param device_info 设备信息
 * @return 0成功
 */
int cloud_agent_register(CloudAgent* agent, const DeviceInfo* device_info);

/**
 * 发送遥测数据
 * @param agent Agent句柄
 * @param telemetry 遥测数据
 * @return 0成功
 */
int cloud_agent_send_telemetry(CloudAgent* agent, const CloudTelemetry* telemetry);

// ============================================================================
// 模型管理
// ============================================================================

typedef void (*UpdateProgressCallback)(size_t downloaded, size_t total, void* userdata);

/**
 * 检查模型更新
 * @param agent Agent句柄
 * @param model_info 输出模型更新信息
 * @return 0有更新，1无更新，-1错误
 */
int cloud_agent_check_model_update(CloudAgent* agent, CloudModelInfo* model_info);

/**
 * 下载模型
 * @param agent Agent句柄
 * @param model_info 模型更新信息
 * @param target_path 目标路径 (可为NULL，使用默认缓存路径)
 * @param callback 进度回调
 * @param progress_userdata 用户数据
 * @return 0成功，-1失败
 */
int cloud_agent_download_model(CloudAgent* agent,
                               const CloudModelInfo* model_info,
                               const char* target_path,
                               UpdateProgressCallback callback,
                               void* progress_userdata);

// ============================================================================
// OTA操作
// ============================================================================

/**
 * 检查更新
 * @param agent Agent句柄
 * @param update_info 输出更新信息 (可为NULL)
 * @return 0有更新，1无更新，-1错误
 */
int cloud_agent_check_update(CloudAgent* agent, UpdateInfo* update_info);

/**
 * 下载更新
 * @param agent Agent句柄
 * @param update 更新信息
 * @param progress_callback 进度回调 (可为NULL)
 * @param progress_userdata 回调用户数据
 * @return 0成功，-1失败
 */
int cloud_agent_download_update(CloudAgent* agent, const UpdateInfo* update,
                                UpdateProgressCallback callback, void* progress_userdata);

/**
 * 应用更新
 * @param agent Agent句柄
 * @param update 更新信息
 * @param verify_before_apply 应用前验证
 * @return 0成功，-1失败
 */
int cloud_agent_apply_update(CloudAgent* agent, const UpdateInfo* update, bool verify_before_apply);

/**
 * 回滚更新
 * @param agent Agent句柄
 * @return 0成功，-1失败
 */
int cloud_agent_rollback(CloudAgent* agent);

// ============================================================================
// 远程命令
// ============================================================================

/**
 * 接收远程命令
 * @param agent Agent句柄
 * @param command 输出命令
 * @param timeout_ms 等待超时
 * @return 0成功，1超时，-1失败
 */
int cloud_agent_receive_command(CloudAgent* agent, CloudCommand* command, uint32_t timeout_ms);

/**
 * 发送命令响应
 * @param agent Agent句柄
 * @param response 响应数据
 * @return 0成功
 */
int cloud_agent_send_response(CloudAgent* agent, const CloudCommandResponse* response);

// ============================================================================
// 质控上报
// ============================================================================

/**
 * 上报质控报告
 * @param agent Agent句柄
 * @param report 质控报告
 * @return 0成功
 */
int cloud_agent_report_qc(CloudAgent* agent, const QCReport* report);

/**
 * 获取质控建议
 * @param agent Agent句柄
 * @param device_id 设备ID
 * @param recommendations 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 0成功
 */
int cloud_agent_get_recommendations(CloudAgent* agent, const char* device_id,
                                    char* recommendations, size_t buffer_size);

// ============================================================================
// 推理统计
// ============================================================================

/**
 * 上报推理统计
 * @param agent Agent句柄
 * @param stats 统计信息
 * @return 0成功
 */
int cloud_agent_report_inference_stats(CloudAgent* agent, const InferenceStats* stats);

// ============================================================================
// 联邦学习
// ============================================================================

/**
 * 下载联邦学习模型
 * @param agent Agent句柄
 * @param model_buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @param actual_size 实际大小
 * @return 0成功
 */
int cloud_agent_download_federated_model(CloudAgent* agent,
                                         uint8_t* model_buffer,
                                         size_t buffer_size,
                                         size_t* actual_size);

/**
 * 上报本地梯度
 * @param agent Agent句柄
 * @param gradients 梯度数据
 * @param size 数据大小
 * @return 0成功
 */
int cloud_agent_upload_gradients(CloudAgent* agent, const uint8_t* gradients, size_t size);

// ============================================================================
// 同步操作
// ============================================================================

/**
 * 同步时间 (NTP)
 * @param agent Agent句柄
 * @param offset 输出时间偏移 (秒)
 * @return 0成功
 */
int cloud_agent_sync_time(CloudAgent* agent, double* offset);

/**
 * 同步校准参数
 * @param agent Agent句柄
 * @param calibration_data 校准数据
 * @param size 数据大小
 * @return 0成功
 */
int cloud_agent_sync_calibration(CloudAgent* agent, const uint8_t* calibration_data, size_t size);

// ============================================================================
// 校准报告
// ============================================================================

/**
 * 上传校准报告
 * @param agent Agent句柄
 * @param report 校准报告
 * @return 0成功
 */
int cloud_agent_upload_calibration(CloudAgent* agent, const CloudCalibrationReport* report);

/**
 * 请求校准建议
 * @param agent Agent句柄
 * @param device_id 设备ID
 * @param guidance 输出建议
 * @param buffer_size 缓冲区大小
 * @return 0成功
 */
int cloud_agent_request_calibration_guidance(CloudAgent* agent,
                                             const char* device_id,
                                             char* guidance,
                                             size_t buffer_size);

// ============================================================================
// 事件回调
// ============================================================================

typedef enum {
    CLOUD_EVENT_CONNECTED,
    CLOUD_EVENT_DISCONNECTED,
    CLOUD_EVENT_UPDATE_AVAILABLE,
    CLOUD_EVENT_UPDATE_DOWNLOADED,
    CLOUD_EVENT_UPDATE_READY,
    CLOUD_EVENT_UPDATE_FAILED,
    CLOUD_EVENT_ERROR
} CloudEventType;

typedef void (*CloudEventCallback)(CloudEventType event, void* event_data, void* userdata);

/**
 * 设置事件回调
 * @param agent Agent句柄
 * @param callback 回调函数
 * @param userdata 用户数据
 */
void cloud_agent_set_event_callback(CloudAgent* agent, CloudEventCallback callback, void* userdata);

#ifdef __cplusplus
}
#endif

#endif // CLOUD_AGENT_H
