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
    .color_space = DISPLAY_COLORSPACE_DICOM_GSDF,
    .gsdf_profile = DISPLAY_GSDF_CT,
    .gamma = DISPLAY_GAMMA_DICOM,
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
    AI_MODALITY_UNKNOWN = 0,
    AI_MODALITY_CT,           // CT
    AI_MODALITY_MR,           // MRI
    AI_MODALITY_DX,           // Digital Radiography
    AI_MODALITY_CR,           // CR
    AI_MODALITY_US,           // Ultrasound
    AI_MODALITY_PT,           // PET
    AI_MODALITY_ES,           // Endoscopy
    AI_MODALITY_SM,           // Pathology
    AI_MODALITY_SV            // Surgical Video
} AI_ModalityType;
```

### Display_ColorSpace

```c
typedef enum {
    DISPLAY_COLORSPACE_sRGB = 0,
    DISPLAY_COLORSPACE_DCI_P3,
    DISPLAY_COLORSPACE_Rec709,
    DISPLAY_COLORSPACE_Rec2020,
    DISPLAY_COLORSPACE_DICOM_GSDF,    // 医疗灰阶
    DISPLAY_COLORSPACE_BT2100_HLG,
    DISPLAY_COLORSPACE_BT2100_PQ
} Display_ColorSpace;
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
