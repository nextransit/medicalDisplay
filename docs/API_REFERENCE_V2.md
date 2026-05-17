# API 参考手册 v2.0

本文档是 AI 自适应医疗显示系统 SDK 的完整 API 参考。

---

## 一、SIMD 图像处理 (`simd_processing.h`)

### 1.1 头文件

```c
#include "simd_processing.h"
```

### 1.2 类型定义

```c
// SIMD后端类型
typedef enum {
    SIMD_NONE = 0,   // 无SIMD支持
    SIMD_SSE4,       // SSE4.2
    SIMD_AVX2,       // AVX2
    SIMD_NEON,       // ARM NEON
    SIMD_AUTO        // 自动检测
} SIMDBackend;

// 流水线配置
typedef struct {
    bool enable_colorspace_conversion;
    float brightness;     // -1.0 to 1.0
    float contrast;        // 0.5 to 2.0
    float saturation;      // 0.0 to 2.0
    const uint8_t* gsdf_lut;  // 256字节GSDF查找表
    bool enable_bloodless;
    float blood_suppress_level;  // 0.0 to 1.0
    float tissue_enhance;        // 0.0 to 1.0
    float edge_preserve;         // 0.0 to 1.0
    float edge_threshold;        // 边缘检测阈值
    bool enable_denoise;
    float sharpen_strength;
} SimdPipelineConfig;
```

### 1.3 后端查询

```c
// 获取当前SIMD后端
SIMDBackend simd_get_backend(void);

// 获取后端名称
const char* simd_get_backend_name(SIMDBackend backend);

// 检查后端是否支持
bool simd_is_supported(SIMDBackend backend);
```

**示例**:
```c
SIMDBackend backend = simd_get_backend();
printf("Using SIMD backend: %s\n", simd_get_backend_name(backend));
// 输出: Using SIMD backend: AVX2
```

### 1.4 基础图像处理

#### simd_adjust_brightness_contrast

调整图像的亮度和对比度。

```c
int simd_adjust_brightness_contrast(
    const uint8_t* input,   // RGB24输入
    uint8_t* output,         // RGB24输出
    int width,               // 宽度
    int height,              // 高度
    float brightness,         // 亮度: -1.0 to 1.0
    float contrast           // 对比度: 0.5 to 2.0
);
// 返回: 0成功, -1失败
```

#### simd_adjust_saturation

调整图像的饱和度。

```c
int simd_adjust_saturation(
    const uint8_t* input,
    uint8_t* output,
    int width,
    int height,
    float saturation   // 饱和度: 0.0 to 2.0
);
```

#### simd_rgb_to_grayscale

RGB转灰度。

```c
int simd_rgb_to_grayscale(
    const uint8_t* rgb,
    uint8_t* gray,
    int width,
    int height
);
```

#### simd_rgb_to_yuv420 / simd_yuv420_to_rgb

RGB与YUV420互转。

```c
int simd_rgb_to_yuv420(const uint8_t* rgb, uint8_t* yuv, int width, int height);
int simd_yuv420_to_rgb(const uint8_t* yuv, uint8_t* rgb, int width, int height);
```

### 1.5 医疗影像处理

#### simd_gsdf_lut_apply

应用GSDF查找表进行DICOM标准亮度校正。

```c
int simd_gsdf_lut_apply(
    const uint8_t* input,
    uint8_t* output,
    int width,
    int height,
    const uint8_t* lut   // 256字节GSDF LUT
);
```

**示例**:
```c
// 生成GSDF LUT
uint8_t gsdf_lut[256];
gsdf_generate_lut(gsdf_lut, GSDF_DICOM_PART14);

// 应用
simd_gsdf_lut_apply(input, output, width, height, gsdf_lut);
```

#### simd_edge_detection_sobel

Sobel边缘检测。

```c
int simd_edge_detection_sobel(
    const uint8_t* input,   // RGB24输入
    uint8_t* edge,         // 边缘图输出 (单通道)
    int width,
    int height,
    float threshold        // 边缘阈值: 0-255
);
```

#### simd_bloodless_enhance

无血术野增强，抑制血色并增强组织对比度。

```c
int simd_bloodless_enhance(
    const uint8_t* input,
    uint8_t* output,
    int width,
    int height,
    float suppress_level,   // 血色抑制强度: 0.0 to 1.0
    float tissue_enhance,   // 组织增强: 0.0 to 1.0
    float edge_preserve     // 边缘保留: 0.0 to 1.0
);
```

### 1.6 流水线处理

#### simd_pipeline_process

一次性执行所有配置的图像处理步骤。

```c
int simd_pipeline_process(
    const uint8_t* input,
    uint8_t* output,
    int width,
    int height,
    const SimdPipelineConfig* config
);
```

**示例**:
```c
SimdPipelineConfig config = {
    .brightness = 0.1f,
    .contrast = 1.1f,
    .saturation = 1.2f,
    .gsdf_lut = gsdf_lut,
    .enable_bloodless = true,
    .blood_suppress_level = 0.5f,
    .tissue_enhance = 0.3f,
};

simd_pipeline_process(input, output, 1920, 1080, &config);
// 性能: 1920x1080 @ 7.42ms (135fps)
```

### 1.7 性能基准

#### simd_benchmark

性能基准测试。

```c
int simd_benchmark(
    int width,
    int height,
    int iterations,
    double* ops_per_sec   // 输出: 操作数/秒
);
```

---

## 二、GPU 管线 (`gpu_pipeline.h`)

### 2.1 头文件

```c
#include "gpu_pipeline.h"
```

### 2.2 类型定义

```c
// GPU后端类型
typedef enum {
    GPU_BACKEND_NONE = 0,
    GPU_BACKEND_VULKAN,    // Vulkan Compute
    GPU_BACKEND_OPENGL,    // OpenGL 4.3 Compute
    GPU_BACKEND_METAL,     // Metal Compute (macOS/iOS)
    GPU_BACKEND_AUTO       // 自动选择
} GPUBackendType;

// 管线配置
typedef struct {
    GPUBackendType backend;
    uint32_t max_width;
    uint32_t max_height;
    bool enable_hdr;
    bool enable_async;
    uint32_t pipeline_depth;  // 帧缓冲深度
} GPUPipelineConfig;

// 处理参数
typedef struct {
    float brightness;
    float contrast;
    float saturation;
    float sharpness;
    float edge_strength;
    float edge_threshold;
    float bloodless_strength;
    uint32_t enable_bloodless;
    uint32_t enable_pseudo_color;
    uint32_t enable_hdr;
    uint32_t mode;
} GPUPipelineParams;
```

### 2.3 生命周期

```c
// 创建管线
int gpu_pipeline_create(GPUPipeline** pipeline, const GPUPipelineConfig* config);

// 销毁管线
void gpu_pipeline_destroy(GPUPipeline* pipeline);

// 检查可用性
bool gpu_pipeline_is_available(GPUBackendType preferred_backend);

// 获取后端
GPUBackendType gpu_pipeline_get_backend(GPUPipeline* pipeline);

// 获取设备信息
int gpu_pipeline_get_device_info(GPUPipeline* pipeline, 
                                char* gpu_name, size_t max_size);
```

### 2.4 帧处理

```c
// 处理RGBA帧
int gpu_pipeline_process_frame(
    GPUPipeline* pipeline,
    const uint8_t* input_rgba,
    uint8_t* output_rgba,
    uint32_t width,
    uint32_t height,
    const GPUPipelineParams* params
);

// 处理YUV帧
int gpu_pipeline_process_frame_yuv(
    GPUPipeline* pipeline,
    const uint8_t* input_yuv,
    uint8_t* output_yuv,
    uint32_t width,
    uint32_t height,
    int yuv_format,  // 0=YUV444, 1=YUV422, 2=YUV420
    const GPUPipelineParams* params
);

// 获取延迟统计
int64_t gpu_pipeline_get_last_latency_us(GPUPipeline* pipeline);
```

**示例**:
```c
GPUPipelineConfig config = {
    .backend = GPU_BACKEND_AUTO,
    .max_width = 3840,
    .max_height = 2160,
    .enable_hdr = true,
    .pipeline_depth = 3
};

GPUPipeline* pipeline;
gpu_pipeline_create(&pipeline, &config);

GPUPipelineParams params = {
    .brightness = 0.1f,
    .contrast = 1.1f,
    .saturation = 1.2f,
    .enable_bloodless = 1,
    .bloodless_strength = 0.5f
};

gpu_pipeline_process_frame(pipeline, input, output, 1920, 1080, &params);

int64_t latency = gpu_pipeline_get_last_latency_us(pipeline);
printf("Frame latency: %.2f ms\n", latency / 1000.0f);

gpu_pipeline_destroy(pipeline);
```

---

## 三、V4L2 视频采集 (`v4l2_capture.h`)

### 3.1 头文件

```c
#include "v4l2_capture.h"
```

> 注意: 仅Linux支持

### 3.2 类型定义

```c
// 像素格式
typedef enum {
    V4L2_PIX_FMT_YUYV = 0x56595559,
    V4L2_PIX_FMT_MJPEG = 0x47504A4D,
    V4L2_PIX_FMT_RGB24 = 0x33424752,
    V4L2_PIX_FMT_NV12 = 0x3231564E,
} V4L2PixelFormat;

// 帧格式
typedef struct {
    uint32_t width;
    uint32_t height;
    V4L2PixelFormat pixel_format;
    uint32_t bytes_per_line;
    uint32_t image_size;
    uint32_t framerate_numer;
} V4L2FrameFormat;

// 设备信息
typedef struct {
    char device_path[256];
    char device_name[256];
    uint32_t capabilities;
    uint32_t pixel_formats[16];
    int num_formats;
} V4L2DeviceInfo;

// 统计
typedef struct {
    uint64_t total_frames;
    uint64_t dropped_frames;
    double avg_latency_ms;
    uint32_t current_fps;
} V4L2Stats;
```

### 3.3 设备查询

```c
// 列出可用设备
int v4l2_list_devices(char** devices, int max_devices);

// 获取设备信息
int v4l2_get_device_info(const char* device_path, V4L2DeviceInfo* info);
```

**示例**:
```c
char* devices[16];
int count = v4l2_list_devices(devices, 16);
for (int i = 0; i < count; i++) {
    printf("Found: %s\n", devices[i]);
    free(devices[i]);
}
```

### 3.4 设备生命周期

```c
// 打开设备
V4L2Capture* v4l2_capture_open(
    const char* device_path,
    uint32_t width,
    uint32_t height,
    V4L2PixelFormat pixel_format,
    uint32_t framerate
);

// 关闭设备
void v4l2_capture_close(V4L2Capture* capture);

// 获取/设置格式
int v4l2_capture_get_format(V4L2Capture* capture, V4L2FrameFormat* format);
int v4l2_capture_set_format(V4L2Capture* capture, uint32_t width, 
                           uint32_t height, V4L2PixelFormat fmt, uint32_t fps);
```

### 3.5 帧采集

```c
// 启动/停止采集
int v4l2_capture_start(V4L2Capture* capture);
int v4l2_capture_stop(V4L2Capture* capture);

// 单帧采集 (阻塞)
int v4l2_capture_frame(V4L2Capture* capture, uint8_t* buffer, 
                      size_t buffer_size, int timeout_ms);
// 返回: 0成功, -1错误, 1超时

// 轮询模式
int v4l2_capture_poll(V4L2Capture* capture, int timeout_ms);
int v4l2_capture_get_frame(V4L2Capture* capture, uint8_t* buffer, size_t buffer_size);

// 转换
int v4l2_frame_to_rgb(V4L2Capture* capture, const uint8_t* frame, 
                      uint8_t* rgb_data, size_t rgb_buffer_size);
```

### 3.6 设备控制

```c
int v4l2_set_exposure(V4L2Capture* capture, int value);
int v4l2_set_gain(V4L2Capture* capture, int value);
int v4l2_set_brightness(V4L2Capture* capture, int value);
int v4l2_set_contrast(V4L2Capture* capture, int value);
int v4l2_set_saturation(V4L2Capture* capture, int value);
int v4l2_set_white_balance(V4L2Capture* capture, int auto_mode, int value);
int v4l2_set_focus(V4L2Capture* capture, int auto_focus, int value);
```

### 3.7 统计

```c
void v4l2_get_stats(V4L2Capture* capture, V4L2Stats* stats);
void v4l2_reset_stats(V4L2Capture* capture);
```

**完整示例**:
```c
// 打开摄像头
V4L2Capture* cap = v4l2_capture_open("/dev/video0", 1920, 1080,
                                        V4L2_PIX_FMT_YUYV, 60);
if (!cap) {
    fprintf(stderr, "Failed to open camera\n");
    return 1;
}

// 分配缓冲区
V4L2FrameFormat fmt;
v4l2_capture_get_format(cap, &fmt);
std::vector<uint8_t> frame_buffer(fmt.image_size);
std::vector<uint8_t> rgb_buffer(fmt.width * fmt.height * 3);

// 启动采集
v4l2_capture_start(cap);

while (running) {
    if (v4l2_capture_frame(cap, frame_buffer.data(), 
                           frame_buffer.size(), 1000) == 0) {
        // 转换为RGB并处理
        v4l2_frame_to_rgb(cap, frame_buffer.data(),
                         rgb_buffer.data(), rgb_buffer.size());
        // 使用SIMD处理...
    }
}

v4l2_capture_stop(cap);
v4l2_capture_close(cap);
```

---

## 四、3D 体绘制 (`volume_renderer.h`)

### 4.1 头文件

```c
#include "volume_renderer.h"
```

### 4.2 类型定义

```c
// 渲染模式
typedef enum {
    VOLUME_MODE_RAY_CASTING = 0,
    VOLUME_MODE_MIP,
    VOLUME_MODE_COMPOSITE,
    VOLUME_MODE_SURFACE
} VolumeRenderMode;

// 传输函数类型
typedef enum {
    TRANSFER_CT_BONE,
    TRANSFER_CT_SOFT_TISSUE,
    TRANSFER_CT_LUNG,
    TRANSFER_PET_METABOLIC,
    TRANSFER_FUSED_CT_PET
} TransferFunctionType;

// 渲染配置
typedef struct {
    VolumeRenderMode mode;
    TransferFunctionType transfer_type;
    float window_center;
    float window_width;
    float step_size;
    bool enable_shading;
} VolumeRenderConfig;

// 体数据
typedef struct {
    const void* data;
    int width, height, depth;
    float spacing_x, spacing_y, spacing_z;
    float min_value, max_value;
    int bytes_per_voxel;
    bool is_float;
} VolumeData;
```

### 4.3 生命周期

```c
// 创建渲染器 (backend: "cpu", "vulkan", "metal")
VolumeRenderer* volume_renderer_create(const char* backend);
void volume_renderer_destroy(VolumeRenderer* renderer);

// 设置数据
int volume_renderer_set_data(VolumeRenderer* renderer, const VolumeData* data);
int volume_renderer_set_config(VolumeRenderer* renderer, 
                              const VolumeRenderConfig* config);

// 传输函数
int volume_renderer_set_transfer_function(VolumeRenderer* renderer, 
                                        TransferFunctionType type);
int volume_renderer_set_custom_transfer(VolumeRenderer* renderer,
                                       int num_points, const float* points);
```

### 4.4 渲染

```c
int volume_renderer_render(VolumeRenderer* renderer,
                        uint8_t* output,
                        int output_width,
                        int output_height,
                        const float* view_matrix);

void volume_renderer_get_stats(VolumeRenderer* renderer, 
                             VolumeRenderStats* stats);
```

**示例**:
```c
VolumeRenderer* vr = volume_renderer_create("cpu");

VolumeData data = {
    .data = ct_volume_data,
    .width = 512, .height = 512, .depth = 256,
    .spacing_x = 0.5f, .spacing_y = 0.5f, .spacing_z = 1.0f,
    .bytes_per_voxel = 2,
    .is_float = false
};
volume_renderer_set_data(vr, &data);

VolumeRenderConfig config = {
    .mode = VOLUME_MODE_RAY_CASTING,
    .transfer_type = TRANSFER_CT_SOFT_TISSUE,
    .step_size = 0.5f,
    .enable_shading = true
};
volume_renderer_set_config(vr, &config);

std::vector<uint8_t> output(1920 * 1080 * 4);
float view_matrix[16] = { /* 单位矩阵 */ };
volume_renderer_render(vr, output.data(), 1920, 1080, view_matrix);

volume_renderer_destroy(vr);
```

---

## 五、内存池 (`memory_pool.h`)

### 5.1 类型定义

```c
typedef struct MemoryPoolConfig {
    size_t block_size;       // 块大小
    size_t initial_blocks;    // 初始块数
    size_t max_blocks;       // 最大块数
    bool thread_safe;        // 线程安全
} MemoryPoolConfig;
```

### 5.2 API

```c
MemoryPool* mem_pool_create(const MemoryPoolConfig* config);
void mem_pool_destroy(MemoryPool* pool);

void* mem_pool_alloc(MemoryPool* pool, size_t size);
void mem_pool_free(MemoryPool* pool, void* ptr);

void mem_pool_get_stats(MemoryPool* pool, size_t* alloc_blocks, size_t* free_blocks,
                       size_t* total_alloc, size_t* total_freed, size_t* wasted);
```

---

## 错误码

| 返回值 | 含义 |
|--------|------|
| 0 | 成功 |
| -1 | 通用错误/参数无效 |
| 1 | 超时/无数据 |

---

## 线程安全

- SIMD函数: **线程安全** (可并行调用不同缓冲区)
- GPU管线: **线程安全** (内部同步)
- V4L2采集: **非线程安全** (需外部同步)
- 内存池: **可选线程安全** (配置决定)

---

## 内存管理

- 所有输出缓冲区由调用者分配
- 建议使用内存池减少分配开销
- 大帧处理建议使用GPU纹理避免CPU拷贝
