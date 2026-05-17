# API 参考 / API Reference

## AI Engine API

### ai_engine_create

创建AI推理引擎实例。

```c
void* ai_engine_create(const AI_EngineConfig* config);
```

**参数:**
- `config` - 引擎配置

**返回:** 引擎句柄，失败返回NULL

**示例:**
```c
AI_EngineConfig config = ai_engine_default_config();
config.enable_npu = true;
config.rknn_model_path = "/data/models/medical.rknn";
void* engine = ai_engine_create(&config);
```

### ai_engine_recognize

执行同步影像识别。

```c
int ai_engine_recognize(void* engine, 
                        const AI_ImageBuffer* image, 
                        AI_RecognitionResult* result);
```

**参数:**
- `engine` - 引擎句柄
- `image` - 输入影像
- `result` - 输出识别结果

**返回:** 0成功，负值失败

### ai_engine_get_window_recommendations

获取窗口/层级推荐。

```c
int ai_engine_get_window_recommendations(void* engine, 
                                          AI_ModalityType modality,
                                          const char* metadata,
                                          AI_WindowLevel* output, 
                                          int max_output);
```

**参数:**
- `modality` - 影像模态
- `output` - 输出预设数组
- `max_output` - 最大输出数量

**返回:** 实际输出数量

---

## Display Engine API

### display_open

打开显示设备。

```c
Display_Device display_open(const char* device_path);
```

**参数:**
- `device_path` - DRM设备路径，如 `/dev/dri/card0`

**返回:** 显示设备句柄

### display_apply_config

应用显示配置。

```c
int display_apply_config(Display_Device device, 
                         const Display_Config* config);
```

**配置示例 (CT):**
```c
Display_Config config = {
    .color_space = COLOR_SPACE_DICOM_GSDF,
    .gsdf_profile = DISPLAY_GSDF_CT,
    .gamma = DISPLAY_GSDF_DICOM,
    .window_width = 400,
    .window_center = 40,
    .enable_gsdf = true,
    .target_luminance = 500.0f
};
display_apply_config(device, &config);
```

### display_switch_gsdf

切换GSDF曲线。

```c
void display_switch_gsdf(Display_Device device, 
                          Display_GSDFProfile profile);
```

**可用配置:**
- `DISPLAY_GSDF_CT` - CT标准
- `DISPLAY_GSDF_MR` - MR标准
- `DISPLAY_GSDF_US` - 超声标准
- `DISPLAY_GSDF_DR` - DR标准
- `DISPLAY_GSDF_SURGICAL` - 手术/HDR

### display_load_lut

加载LUT数据。

```c
int display_load_lut(Display_Device device, 
                      const Display_LUT* lut);
```

### surgical_engine_process_batch_ex

按每帧输入元数据批量处理术野视频，避免旧批处理接口里固定 `1920x1080 RGB` 的限制。

```c
int surgical_engine_process_batch_ex(SurgicalVideoEngine* engine,
                                     const uint8_t** frames,
                                     const VideoFrameInfo* frame_infos,
                                     int count,
                                     uint8_t** outputs,
                                     VideoFrameInfo* output_infos);
```

---

## DICOM API

### dicom_open

打开DICOM文件。

```c
DICOM_Dataset dicom_open(const char* file_path);
```

### dicom_read_pixels

读取像素数据。

```c
int dicom_read_pixels(DICOM_Dataset dataset, 
                       DICOM_PixelData* pixel_info);
```

**返回数据结构:**
```c
typedef struct {
    uint32_t rows;
    uint32_t columns;
    uint32_t bits_allocated;
    uint32_t bits_stored;
    uint32_t pixel_representation;
    const void* pixel_data;
    size_t pixel_data_size;
} DICOM_PixelData;
```

### dicom_pixel_to_hu

像素值转换为HU (Hounsfield Unit)。

```c
float dicom_pixel_to_hu(int raw_pixel, 
                        float slope, 
                        float intercept);
```

### dicom_pixels_to_hu_batch

批量将 16-bit 原始像素转换为 HU，内部会走 SIMD 加速路径（若平台可用）。

```c
int dicom_pixels_to_hu_batch(const uint16_t* raw_pixels,
                             size_t count,
                             float slope,
                             float intercept,
                             float* hu_values);
```

---

## Cloud API

### cloud_agent_create

创建云端Agent。

```c
Cloud_Agent cloud_agent_create(const Cloud_AgentConfig* config);
```

**配置示例:**
```c
Cloud_AgentConfig config = {
    .server_url = "https://api.medical-display.ai",
    .server_port = 8883,
    .connection_type = CONNECTION_MQTT,
    .mqtt_topic_prefix = "medical-display",
    .telemetry_interval_sec = 60,
    .auto_check_updates = true
};
Cloud_Agent agent = cloud_agent_create(&config);
```

### cloud_agent_send_telemetry

发送遥测数据。

```c
int cloud_agent_send_telemetry(Cloud_Agent agent, 
                               const Cloud_Telemetry* telemetry);
```

**遥测数据结构:**
```c
typedef struct {
    uint32_t display_count;
    uint32_t hours_used;
    float avg_fps;
    float ai_inference_latency_ms;
    float backlight_hours;
    float color_accuracy_delta_e;
} Cloud_Telemetry;
```

---

## 数据类型

### AI_ModalityType

```c
typedef enum {
    MODALITY_UNKNOWN = 0,
    MODALITY_CT,           // 计算机断层扫描
    MODALITY_MR,           // 磁共振成像
    MODALITY_DX,           // 数字X射线
    MODALITY_CR,           // 计算机X射线
    MODALITY_US,           // 超声
    MODALITY_ES,           // 内窥镜
    MODALITY_SM,           // 数字病理
    MODALITY_PT,           // PET
    MODALITY_XA,           // X射线血管造影
    MODALITY_RF,           // 放射透视
    MODALITY_OP,           // 眼科摄影
    MODALITY_SURGICAL,     // 术野视频
    MODALITY_COUNT
} ModalityType;
```

### Display_ColorSpace

```c
typedef enum {
    COLOR_SPACE_sRGB = 0,
    COLOR_SPACE_DCI_P3,
    COLOR_SPACE_Rec709,
    COLOR_SPACE_Rec2020,
    COLOR_SPACE_AdobeRGB,
    COLOR_SPACE_DICOM_GSDF,    // 医疗灰阶
    COLOR_SPACE_NATIVE,
    COLOR_SPACE_COUNT
} ColorSpace;
```

### Result Codes

```c
typedef enum {
    RESULT_SUCCESS = 0,
    RESULT_ERROR_INVALID_PARAM = -1,
    RESULT_ERROR_OUT_OF_MEMORY = -2,
    RESULT_ERROR_NOT_FOUND = -3,
    RESULT_ERROR_TIMEOUT = -4,
    RESULT_ERROR_PERMISSION = -5,
    RESULT_ERROR_NOT_SUPPORTED = -6,
    RESULT_ERROR_DEVICE_BUSY = -7,
    RESULT_ERROR_CONNECTION_FAILED = -8,
    RESULT_ERROR_VERIFICATION_FAILED = -9,
    RESULT_ERROR_INTERNAL = -100
} ResultCode;
```

---

## Federated Learning API (`federated_client.h`)

### 概述

联邦学习客户端支持 FedAvg 协议，实现隐私保护的协同训练。

### 类型定义

```c
// 客户端状态
typedef enum {
    FL_CLIENT_IDLE = 0,
    FL_CLIENT_DOWNLOADING_MODEL,
    FL_CLIENT_TRAINING,
    FL_CLIENT_UPLOADING,
    FL_CLIENT_WAITING,
    FL_CLIENT_ERROR
} FLClientState;

// 客户端配置
typedef struct {
    char server_url[512];
    char model_id[128];
    char device_id[64];
    char hospital_id[64];
    int local_epochs;           // 本地训练轮数
    int batch_size;             // 批次大小
    float learning_rate;        // 学习率
    float noise_multiplier;     // 差分隐私噪声 (0=禁用)
    float clipping_norm;        // 梯度裁剪范数
    int connection_timeout_ms;
    int max_retries;
} FLClientConfig;

// 训练统计
typedef struct {
    uint32_t round_number;
    uint32_t local_epoch;
    float training_loss;
    float validation_accuracy;
    uint32_t samples_used;
    float gradient_norm;
    float privacy_budget_used;
    uint64_t training_time_ms;
} FLTrainingStats;

// 梯度元数据
typedef struct {
    uint32_t round_number;
    uint32_t client_id_hash;    // 哈希不暴露真实ID
    size_t gradient_size;
    float gradient_norm;
    float noise_added;
    uint32_t samples_count;
} FLGradientMeta;
```

### 回调函数

```c
typedef void (*FLStateCallback)(FLClientState new_state, void* userdata);
typedef void (*FLProgressCallback)(int current, int total, const char* message, void* userdata);
typedef void (*FLTrainingCompleteCallback)(const FLTrainingStats* stats, void* userdata);
typedef void (*FLModelUpdateCallback)(const uint8_t* model_data, size_t model_size, void* userdata);
```

### 生命周期

```c
FederatedClient* fl_client_create(const FLClientConfig* config);
void fl_client_destroy(FederatedClient* client);

void fl_client_set_state_callback(FederatedClient* client, FLStateCallback callback, void* userdata);
void fl_client_set_progress_callback(FederatedClient* client, FLProgressCallback callback, void* userdata);
void fl_client_set_training_callback(FederatedClient* client, FLTrainingCompleteCallback callback, void* userdata);
void fl_client_set_model_callback(FederatedClient* client, FLModelUpdateCallback callback, void* userdata);
```

### 模型与训练

```c
// 连接服务器
int fl_client_connect(FederatedClient* client);
void fl_client_disconnect(FederatedClient* client);
bool fl_client_is_connected(FederatedClient* client);

// 下载全局模型
int fl_client_download_model(FederatedClient* client, uint8_t* model_buffer,
                            size_t buffer_size, size_t* actual_size);
int fl_client_get_model_version(FederatedClient* client);

// 注册本地数据集
int fl_client_register_dataset(FederatedClient* client, uint32_t data_samples, const char* metadata);

// 执行本地训练
int fl_client_train(FederatedClient* client, const uint8_t* model_data,
                   size_t model_size, FLTrainingStats* stats);
int fl_client_train_async(FederatedClient* client, const uint8_t* model_data, size_t model_size);
int fl_client_wait_training(FederatedClient* client, uint32_t timeout_ms);
```

### 梯度与隐私

```c
// 获取本地梯度
int fl_client_get_gradient(FederatedClient* client, uint8_t* gradient_buffer,
                           size_t buffer_size, FLGradientMeta* meta);

// 上传梯度
int fl_client_upload_gradient(FederatedClient* client, const uint8_t* gradient_data,
                              size_t gradient_size, const FLGradientMeta* meta);

// 差分隐私处理
int fl_client_apply_privacy(FederatedClient* client, uint8_t* gradient_data, size_t gradient_size);
int fl_client_get_privacy_budget(FederatedClient* client, float* epsilon, float* delta);
int fl_client_reset_privacy_budget(FederatedClient* client);
```

### 状态查询

```c
FLClientState fl_client_get_state(FederatedClient* client);
int fl_client_get_stats(FederatedClient* client, FLTrainingStats* stats);
void fl_client_reset_stats(FederatedClient* client);
```

### 使用示例

```c
FLClientConfig config = {
    .server_url = "https://federated.example.com",
    .model_id = "medical_display_v1",
    .device_id = "DISPLAY-001",
    .hospital_id = "HOSPITAL-A",
    .local_epochs = 5,
    .batch_size = 32,
    .learning_rate = 0.001f,
    .noise_multiplier = 0.1f,  // 启用差分隐私
    .clipping_norm = 1.0f,
    .max_retries = 3
};

FederatedClient* client = fl_client_create(&config);
fl_client_connect(client);

// 注册本地数据
fl_client_register_dataset(client, 1000, "{\"modality\": \"CT\"}");

// 下载模型并训练
uint8_t model_buffer[1024*1024];
size_t actual_size;
fl_client_download_model(client, model_buffer, sizeof(model_buffer), &actual_size);

FLTrainingStats stats;
fl_client_train(client, model_buffer, actual_size, &stats);

// 上报梯度
uint8_t gradient_buffer[512*1024];
FLGradientMeta meta;
fl_client_get_gradient(client, gradient_buffer, sizeof(gradient_buffer), &meta);
fl_client_apply_privacy(client, gradient_buffer, meta.gradient_size);
fl_client_upload_gradient(client, gradient_buffer, meta.gradient_size, &meta);

fl_client_destroy(client);
```

---

## Predictive Maintenance API (`predictive_maintenance.h`)

### 概述

预测性维护引擎用于医疗显示器的健康监测、故障预测和维护建议。

### 类型定义

```c
// 设备类型
typedef enum {
    DEVICE_TYPE_DIAGNOSTIC = 0,  // 诊断显示器
    DEVICE_TYPE_CLINICAL,         // 临床显示器
    DEVICE_TYPE_SURGICAL,         // 手术显示器
    DEVICE_TYPE_COUNT
} DeviceType;

// 设备指标
typedef struct {
    float current_luminance;        // 当前亮度 (cd/m²)
    float max_luminance;           // 标称最大亮度
    float luminance_drift_percent; // 亮度漂移百分比
    float delta_e;                 // 色彩偏差
    float white_point_x, white_point_y;
    float color_temp_kelvin;       // 色温 (K)
    uint32_t backlight_hours;     // 背光使用时长
    uint32_t power_on_hours;       // 累计开机时长
    float ambient_temp_celsius;    // 环境温度
    float panel_temp_celsius;     // 面板温度
    uint16_t calibration_age_days; // 距上次校准天数
    float calibration_quality_score; // 校准质量 (0-100)
    uint16_t dead_pixel_count;    // 死像素数量
    float uniformity_score;         // 均匀性评分
} MaintenanceMetrics;

// 健康状态
typedef enum {
    HEALTH_STATUS_EXCELLENT = 0,  // 优秀
    HEALTH_STATUS_GOOD,            // 良好
    HEALTH_STATUS_FAIR,            // 一般
    HEALTH_STATUS_POOR,            // 较差
    HEALTH_STATUS_CRITICAL        // 危急
} HealthStatus;

// 预测结果
typedef struct {
    HealthStatus overall_status;
    float health_score;            // 健康评分 (0-100)
    float risk_score;              // 风险评分 (0-100)
    uint16_t predicted_luminance_failure_days;
    uint16_t predicted_color_failure_days;
    uint16_t predicted_calibration_due_days;
    uint8_t num_issues;
    char issues[10][128];
    float issue_severity[10];
    float prediction_confidence;
} PredictionResult;

// 维护建议
typedef enum {
    ACTION_NONE = 0,
    ACTION_MONITOR,
    ACTION_SCHEDULE_CALIBRATION,
    ACTION_REDUCE_BRIGHTNESS,
    ACTION_SCHEDULE_INSPECTION,
    ACTION_REPLACE_PANEL,
    ACTION_URGENT_REPLACEMENT
} MaintenanceAction;

typedef struct {
    MaintenanceAction action;
    int priority;                  // 1=最高
    char title[128];
    char description[512];
    char rationale[256];
    uint16_t recommended_days;
    float estimated_cost_usd;
} MaintenanceRecommendation;
```

### 引擎生命周期

```c
PredictiveMaintenanceEngine* predictive_engine_create(DeviceType device_type);
void predictive_engine_destroy(PredictiveMaintenanceEngine* engine);
```

### 指标管理

```c
int predictive_engine_update_metrics(PredictiveMaintenanceEngine* engine,
                                     const MaintenanceMetrics* metrics);
int predictive_engine_get_metrics(PredictiveMaintenanceEngine* engine,
                                  MaintenanceMetrics* metrics);
```

### 预测分析

```c
int predictive_engine_analyze(PredictiveMaintenanceEngine* engine,
                             PredictionResult* result);
int predictive_engine_get_recommendations(PredictiveMaintenanceEngine* engine,
                                          MaintenanceRecommendation* recommendations,
                                          int max_count);
int predictive_engine_generate_qc_report(PredictiveMaintenanceEngine* engine,
                                         char* report_json, size_t buffer_size);
```

### 历史数据

```c
int predictive_engine_add_history(PredictiveMaintenanceEngine* engine,
                                  const MaintenanceMetrics* metrics, uint64_t timestamp);
int predictive_engine_get_trend(PredictiveMaintenanceEngine* engine,
                                int metric_type,       // 0=luminance, 1=delta_e, 2=temp
                                uint64_t start_time, uint64_t end_time,
                                float* values, int max_count);
```

### 配置与报表

```c
int predictive_engine_set_thresholds(PredictiveMaintenanceEngine* engine,
                                      float luminance_min, float delta_e_max, float temp_max);
int predictive_engine_export_report(PredictiveMaintenanceEngine* engine,
                                   const char* report_path);
```

### 使用示例

```c
PredictiveMaintenanceEngine* engine = predictive_engine_create(DEVICE_TYPE_DIAGNOSTIC);

// 更新设备指标
MaintenanceMetrics metrics = {
    .current_luminance = 450.0f,
    .max_luminance = 600.0f,
    .luminance_drift_percent = -5.2f,
    .delta_e = 2.5f,
    .white_point_x = 0.3127f,
    .white_point_y = 0.3290f,
    .backlight_hours = 8760,
    .calibration_age_days = 365,
    .calibration_quality_score = 85.0f,
    .dead_pixel_count = 0,
    .uniformity_score = 95.0f
};
predictive_engine_update_metrics(engine, &metrics);

// 执行健康分析
PredictionResult result;
predictive_engine_analyze(engine, &result);

printf("Health Score: %.1f\n", result.health_score);
printf("Status: %d\n", result.overall_status);

// 获取维护建议
MaintenanceRecommendation recs[5];
int count = predictive_engine_get_recommendations(engine, recs, 5);
for (int i = 0; i < count; i++) {
    printf("[%d] %s: %s\n", recs[i].priority, recs[i].title, recs[i].description);
}

predictive_engine_destroy(engine);
```

---

## AR Overlay API (`ar_overlay.h`)

### 概述

AR标注叠加引擎支持手术导航中的实时标注、测量和AR渲染。

### 类型定义

```c
// 标注类型
typedef enum {
    ANNOTATION_POINT = 0,     // 关键点
    ANNOTATION_LINE,           // 线条
    ANNOTATION_RECT,           // 矩形
    ANNOTATION_ELLIPSE,        // 椭圆
    ANNOTATION_POLYGON,        // 多边形
    ANNOTATION_TEXT,           // 文字
    ANNOTATION_MEASUREMENT,    // 测量
    ANNOTATION_ARROW,          // 箭头
    ANNOTATION_HIGHLIGHT       // 高亮
} AnnotationType;

// 标注样式
typedef struct {
    uint8_t color_r, color_g, color_b, color_a;
    float line_width;
    float font_size;
    bool fill;
    bool dashed;
    bool visible;
} AnnotationStyle;

// 标注数据
typedef struct {
    uint32_t id;
    AnnotationType type;
    float x, y;              // 归一化坐标 (0-1)
    float width, height;
    float rotation;           // 旋转角度
    char text[256];
    float value;             // 测量值
    char unit[32];
    AnnotationStyle style;
    char label[128];
    uint32_t z_order;
    bool selected;
    bool locked;
} AnnotationData;

// 标注会话
typedef struct {
    uint32_t session_id;
    char patient_id[64];
    char study_id[64];
    char series_id[64];
    uint64_t created_time;
    uint32_t annotation_count;
} AnnotationSession;
```

### 引擎生命周期

```c
AROverlayEngine* ar_engine_create(int canvas_width, int canvas_height);
void ar_engine_destroy(AROverlayEngine* engine);
```

### 标注管理

```c
int ar_engine_add_annotation(AROverlayEngine* engine, const AnnotationData* annotation);
int ar_engine_update_annotation(AROverlayEngine* engine, uint32_t annotation_id,
                              const AnnotationData* annotation);
int ar_engine_remove_annotation(AROverlayEngine* engine, uint32_t annotation_id);
int ar_engine_get_annotation(AROverlayEngine* engine, uint32_t annotation_id,
                            AnnotationData* annotation);
int ar_engine_get_all_annotations(AROverlayEngine* engine, AnnotationData* annotations, int max_count);
void ar_engine_clear_annotations(AROverlayEngine* engine);
```

### 会话管理

```c
int ar_engine_create_session(AROverlayEngine* engine, AnnotationSession* session);
int ar_engine_load_session(AROverlayEngine* engine, uint32_t session_id);
int ar_engine_save_session(AROverlayEngine* engine, const char* path);
int ar_engine_export_json(AROverlayEngine* engine, char* json_output, size_t buffer_size);
int ar_engine_import_json(AROverlayEngine* engine, const char* json_input);
```

### 渲染与交互

```c
int ar_engine_render(AROverlayEngine* engine, const uint8_t* base_image, uint8_t* output);
int ar_engine_render_to_texture(AROverlayEngine* engine, uint32_t texture_id);
uint32_t ar_engine_hit_test(AROverlayEngine* engine, float x, float y);
int ar_engine_select(AROverlayEngine* engine, uint32_t annotation_id, bool selected);
int ar_engine_move(AROverlayEngine* engine, uint32_t annotation_id, float dx, float dy);
int ar_engine_scale(AROverlayEngine* engine, uint32_t annotation_id, float scale, float center_x, float center_y);
```

### 预设样式

```c
void ar_get_anatomy_style(AnnotationStyle* style);
void ar_get_surgical_style(AnnotationStyle* style);
void ar_get_measurement_style(AnnotationStyle* style);
void ar_get_warning_style(AnnotationStyle* style);
```

### 使用示例

```c
AROverlayEngine* engine = ar_engine_create(1920, 1080);

// 添加测量标注
AnnotationStyle measurement_style;
ar_get_measurement_style(&measurement_style);

AnnotationData measure = {
    .id = 0,
    .type = ANNOTATION_MEASUREMENT,
    .x = 0.3f, .y = 0.4f,
    .width = 0.2f, .height = 0.1f,
    .value = 12.5f,
    .unit = "mm",
    .style = measurement_style,
    .label = "Tumor Diameter"
};
ar_engine_add_annotation(engine, &measure);

// 添加箭头指向
AnnotationStyle arrow_style;
ar_get_surgical_style(&arrow_style);
arrow_style.color_r = 255;
arrow_style.color_g = 0;
arrow_style.color_b = 0;

AnnotationData arrow = {
    .id = 1,
    .type = ANNOTATION_ARROW,
    .x = 0.5f, .y = 0.3f,
    .rotation = 45.0f,
    .style = arrow_style,
    .label = "Critical Area"
};
ar_engine_add_annotation(engine, &arrow);

// 渲染叠加
std::vector<uint8_t> output(1920 * 1080 * 3);
ar_engine_render(engine, base_image.data(), output.data());

// 保存会话
ar_engine_save_session(engine, "/data/annotations/session_001.json");

ar_engine_destroy(engine);
```

---

## Digital Twin API (`digital_twin.h`)

### 概述

数字孪生引擎实现设备仿真、健康预测和批量设备管理。

### 类型定义

```c
// 设备状态
typedef enum {
    DEVICE_STATE_UNKNOWN = 0,
    DEVICE_STATE_ONLINE,
    DEVICE_STATE_OFFLINE,
    DEVICE_STATE_MAINTENANCE,
    DEVICE_STATE_ERROR,
    DEVICE_STATE_UPGRADING
} DeviceState;

// 设备型号
typedef enum {
    DEVICE_MODEL_DIAGNOSTIC = 0,
    DEVICE_MODEL_CLINICAL,
    DEVICE_MODEL_SURGICAL,
    DEVICE_MODEL_PACS_WORKSTATION
} DeviceModel;

// 设备信息
typedef struct {
    char device_id[64];
    char device_name[128];
    DeviceModel model;
    DeviceState state;
    char hospital_id[64];
    char department[64];
    char firmware_version[32];
    uint64_t install_date;
    uint64_t last_seen;
    int32_t brightness_level;
    float panel_temperature;
    float ambient_temperature;
} DeviceInfo;

// 性能指标
typedef struct {
    uint64_t timestamp;
    float luminance_current;
    float delta_e;
    float cpu_usage_percent;
    float memory_usage_percent;
    float temperature_celsius;
    float power_watts;
    float network_latency_ms;
    uint32_t inference_count;
    float avg_inference_ms;
} DeviceMetrics;

// 告警信息
typedef enum {
    ALERT_NONE = 0,
    ALERT_INFO,
    ALERT_WARNING,
    ALERT_ERROR,
    ALERT_CRITICAL
} AlertSeverity;

typedef struct {
    uint32_t alert_id;
    AlertSeverity severity;
    char device_id[64];
    uint64_t timestamp;
    char message[512];
    bool acknowledged;
    bool resolved;
} AlertInfo;

// 故障预测
typedef struct {
    char component[64];
    uint16_t days_to_failure;
    float failure_probability;
    char recommended_action[256];
} FailurePrediction;
```

### 引擎生命周期

```c
DigitalTwinEngine* dt_engine_create(const char* hospital_id);
void dt_engine_destroy(DigitalTwinEngine* engine);
```

### 设备注册

```c
int dt_engine_register_device(DigitalTwinEngine* engine, const DeviceInfo* info);
int dt_engine_unregister_device(DigitalTwinEngine* engine, const char* device_id);
int dt_engine_update_device_state(DigitalTwinEngine* engine, const char* device_id, DeviceState state);
int dt_engine_get_device_info(DigitalTwinEngine* engine, const char* device_id, DeviceInfo* info);
int dt_engine_list_devices(DigitalTwinEngine* engine, char** device_ids, int max_count);
```

### 指标管理

```c
int dt_engine_report_metrics(DigitalTwinEngine* engine, const char* device_id, const DeviceMetrics* metrics);
int dt_engine_get_metrics_history(DigitalTwinEngine* engine, const char* device_id,
                                  uint64_t start_time, uint64_t end_time,
                                  DeviceMetrics* metrics, int max_count);
```

### 告警管理

```c
int dt_engine_get_active_alerts(DigitalTwinEngine* engine, AlertInfo* alerts, int max_count);
int dt_engine_acknowledge_alert(DigitalTwinEngine* engine, uint32_t alert_id);
int dt_engine_resolve_alert(DigitalTwinEngine* engine, uint32_t alert_id);
```

### 预测性维护

```c
int dt_engine_predict_failures(DigitalTwinEngine* engine, const char* device_id,
                              FailurePrediction* predictions, int max_count);
int dt_engine_get_health_score(DigitalTwinEngine* engine, const char* device_id);
int dt_engine_get_maintenance_schedule(DigitalTwinEngine* engine, const char* device_id,
                                       char* schedule_json, size_t buffer_size);
```

### 仿真

```c
int dt_engine_simulate(DigitalTwinEngine* engine, const char* device_id, int hours,
                      DeviceMetrics* metrics, int max_count);
int dt_engine_simulate_all(DigitalTwinEngine* engine, const char** device_ids,
                           int device_count, int hours);
```

### 报表

```c
int dt_engine_generate_status_report(DigitalTwinEngine* engine, char* report_json, size_t buffer_size);
int dt_engine_generate_health_trend(DigitalTwinEngine* engine, const char* device_id,
                                    int days, char* report_json, size_t buffer_size);
```

### 使用示例

```c
DigitalTwinEngine* engine = dt_engine_create("HOSPITAL-A");

// 注册设备
DeviceInfo device = {
    .device_id = "DISPLAY-001",
    .device_name = "诊断显示器1号",
    .model = DEVICE_MODEL_DIAGNOSTIC,
    .state = DEVICE_STATE_ONLINE,
    .hospital_id = "HOSPITAL-A",
    .department = "放射科"
};
dt_engine_register_device(engine, &device);

// 上报指标
DeviceMetrics metrics = {
    .timestamp = time(NULL),
    .luminance_current = 450.0f,
    .delta_e = 2.5f,
    .temperature_celsius = 35.0f,
    .cpu_usage_percent = 15.0f,
    .inference_count = 1000
};
dt_engine_report_metrics(engine, "DISPLAY-001", &metrics);

// 故障预测
FailurePrediction predictions[5];
int count = dt_engine_predict_failures(engine, "DISPLAY-001", predictions, 5);
for (int i = 0; i < count; i++) {
    printf("%s: %.1f%% chance in %d days\n",
           predictions[i].component,
           predictions[i].failure_probability * 100,
           predictions[i].days_to_failure);
}

// 获取健康评分
int health_score = dt_engine_get_health_score(engine, "DISPLAY-001");
printf("Health Score: %d/100\n", health_score);

// 仿真未来7天
DeviceMetrics sim_results[168];  // 每小时一个数据点
dt_engine_simulate(engine, "DISPLAY-001", 168, sim_results, 168);

dt_engine_destroy(engine);
```

---

## Multi-Display Hub API (`multi_display_hub.h`)

### 概述

多屏协同引擎实现多显示器的统一校准、色彩一致性管理和协同显示。

### 类型定义

```c
// 显示器类型
typedef enum {
    DISPLAY_TYPE_DIAGNOSTIC = 0,
    DISPLAY_TYPE_CLINICAL,
    DISPLAY_TYPE_SURGICAL,
    DISPLAY_TYPE_CONSULTATION
} DisplayType;

// 显示器状态
typedef enum {
    DISPLAY_STATUS_OFFLINE = 0,
    DISPLAY_STATUS_ONLINE,
    DISPLAY_STATUS_CALIBRATING,
    DISPLAY_STATUS_ERROR
} DisplayStatus;

// 显示器信息
typedef struct {
    int display_id;
    DisplayType type;
    DisplayStatus status;
    int width, height;
    int bit_depth;
    float max_luminance;
    float min_luminance;
    float current_luminance;
    float calibration_age_days;
    int is_primary;
} DisplayInfo;

// 同步模式
typedef enum {
    SYNC_MODE_INDEPENDENT = 0,
    SYNC_MODE_MASTER_SLAVE,
    SYNC_MODE_BLIND_REVIEW,
    SYNC_MODE_CONSULTATION
} SyncMode;

// 色彩一致性配置
typedef struct {
    bool enable_color_matching;
    int target_colorspace;         // 0=sRGB, 1=DCI-P3, 2=Rec2020
    float target_white_point_x;
    float target_white_point_y;
    float target_luminance;
    bool apply_gsdf;
    int ambient_light_lux;
} ColorConsistencyConfig;
```

### 引擎生命周期

```c
MultiDisplayHub* multi_display_hub_create(int max_displays);
void multi_display_hub_destroy(MultiDisplayHub* hub);
```

### 显示器扫描

```c
int multi_display_hub_scan_displays(MultiDisplayHub* hub, DisplayInfo* displays, int max_count);
int multi_display_hub_get_display_info(MultiDisplayHub* hub, int display_id, DisplayInfo* info);
int multi_display_hub_set_primary(MultiDisplayHub* hub, int display_id);
```

### 色彩一致性

```c
int multi_display_hub_set_color_config(MultiDisplayHub* hub, const ColorConsistencyConfig* config);
int multi_display_hub_get_color_config(MultiDisplayHub* hub, ColorConsistencyConfig* config);
int multi_display_hub_apply_gsdf(MultiDisplayHub* hub, int display_id, int ambient_lux);
int multi_display_hub_apply_unified_lut(MultiDisplayHub* hub, const int* display_ids, int count);
```

### 协同显示

```c
int multi_display_hub_set_sync_mode(MultiDisplayHub* hub, SyncMode mode);
SyncMode multi_display_hub_get_sync_mode(MultiDisplayHub* hub);
int multi_display_hub_sync_render(MultiDisplayHub* hub, const int* display_ids, int count,
                                  const uint8_t* source_data, int width, int height);
```

### 校准管理

```c
int multi_display_hub_start_calibration(MultiDisplayHub* hub, int display_id);
int multi_display_hub_get_calibration_status(MultiDisplayHub* hub, int display_id,
                                             float* progress, char* status, int status_len);
int multi_display_hub_complete_calibration(MultiDisplayHub* hub, int display_id);
```

### 诊断

```c
int multi_display_hub_health_check(MultiDisplayHub* hub, int display_id,
                                   char** issues, int max_issues);
int multi_display_hub_export_calibration_report(MultiDisplayHub* hub,
                                                int display_id, const char* report_path);
```

### 使用示例

```c
MultiDisplayHub* hub = multi_display_hub_create(8);

// 扫描所有显示器
DisplayInfo displays[8];
int count = multi_display_hub_scan_displays(hub, displays, 8);
printf("Found %d displays\n", count);

// 配置色彩一致性
ColorConsistencyConfig color_config = {
    .enable_color_matching = true,
    .target_colorspace = 1,         // DCI-P3
    .target_white_point_x = 0.3127f,
    .target_white_point_y = 0.3290f,
    .target_luminance = 500.0f,
    .apply_gsdf = true,
    .ambient_light_lux = 50
};
multi_display_hub_set_color_config(hub, &color_config);

// 应用GSDF校准到所有显示器
int display_ids[8];
for (int i = 0; i < count; i++) {
    display_ids[i] = displays[i].display_id;
}
multi_display_hub_apply_unified_lut(hub, display_ids, count);

// 设置双盲阅片模式
multi_display_hub_set_sync_mode(hub, SYNC_MODE_BLIND_REVIEW);

// 同步渲染到两个显示器
multi_display_hub_sync_render(hub, display_ids, 2, image_data, 1920, 1080);

// 健康检查
char* issues[10];
int issue_count = multi_display_hub_health_check(hub, display_ids[0], issues, 10);
for (int i = 0; i < issue_count; i++) {
    printf("Issue: %s\n", issues[i]);
}

// 导出校准报告
multi_display_hub_export_calibration_report(hub, display_ids[0], 
                                            "/data/calibration_report.json");

multi_display_hub_destroy(hub);
```
