# 示例程序使用指南

本文档介绍 SDK 附带的示例程序的用法。

---

## 一、医学影像显示演示

### 1.1 基本用法

```bash
./examples/linux/medical_display_demo
```

**参数**:
```
--modality CT|MR|DX|US|PT  # 影像模态
--frames N                   # 处理的帧数
--width W                    # 宽度 (默认512)
--height H                   # 高度 (默认512)
--output-dir DIR             # 输出目录
```

**示例**:
```bash
# CT影像处理
./examples/linux/medical_display_demo --modality CT --frames 100

# MRI影像处理
./examples/linux/medical_display_demo --modality MR --frames 50 --width 1024 --height 1024
```

### 1.2 输出

程序会生成:
- 处理后的图像帧 (`frame_*.raw`)
- 性能统计 (`summary.json`)

**示例输出**:
```
frames=100
modality=CT
bytes_written=825600
inference_avg_ms=0.001
render_avg_ms=0.4085
fps=1441.96
```

---

## 二、GSDF 校准演示

### 2.1 基本用法

```bash
./examples/linux/gsdf_calibration_demo
```

**功能**:
- 演示GSDF (Grayscale Standard Display Function)
- DICOM Part 14合规性验证
- 生成校准报告

### 2.2 输出

生成校准报告包含:
- JND (Just Noticeable Difference) 曲线
- 亮度响应曲线
- 合规性评分

---

## 三、术野视频增强演示

### 3.1 基本用法

```bash
./examples/linux/surgical_video_demo
```

**参数**:
```
--preset LAPAROSCOPIC|ENDOSCOPIC|MICROSCOPIC  # 手术类型
--width N                                    # 帧宽度
--height N                                   # 帧高度
--frames N                                   # 帧数
--brightness F                               # 亮度 (-1.0 to 1.0)
--contrast F                                 # 对比度 (0.5 to 2.0)
--saturation F                               # 饱和度 (0.0 to 2.0)
--enable-bloodless                            # 启用无血术野
--suppress-level F                           # 血色抑制 (0.0 to 1.0)
```

**示例**:
```bash
# 腹腔镜手术增强
./examples/linux/surgical_video_demo --preset LAPAROSCOPIC \
    --width 1920 --height 1080 --frames 300 \
    --enable-bloodless --suppress-level 0.5

# 内窥镜检查
./examples/linux/surgical_video_demo --preset ENDOSCOPIC \
    --brightness 0.1 --contrast 1.1 --saturation 1.2
```

### 3.2 算法说明

**无血术野增强**:
1. 血色检测: 基于RGB色彩空间模型
2. 血色抑制: 降低红色通道，增强绿/蓝
3. 组织对比度增强: 提升组织边缘可见性
4. 边缘保留: 防止过度平滑

---

## 四、多显示同步演示

### 4.1 基本用法

```bash
./examples/linux/multi_display_demo
```

**功能**:
- 多屏协同显示
- GSDF一致性校准
- 色彩同步验证

---

## 五、云端演示

### 5.1 基本用法

```bash
./examples/linux/cloud_demo --server http://localhost:8080
```

**功能**:
- OTA更新模拟
- 设备注册
- 遥测数据上报

---

## 六、V4L2 采集演示

> 仅Linux支持

### 6.1 列出设备

```bash
./examples/linux/v4l2_capture_demo --list
```

**输出示例**:
```
Available V4L2 devices:

  Device: /dev/video0
    Name: USB Camera
    Capabilities: 0x5000001
    Supported formats:
      - YUYV (0x56595559)
      - MJPG (0x47504A4D)
    Resolution: 640x480 to 1920x1080
```

### 6.2 采集视频

```bash
# 基本采集
./examples/linux/v4l2_capture_demo -d /dev/video0

# 高分辨率高帧率
./examples/linux/v4l2_capture_demo -d /dev/video0 \
    -w 1920 -H 1080 -f 60

# 启用术野增强
./examples/linux/v4l2_capture_demo -d /dev/video0 \
    -w 1280 -H 720 -f 30 \
    -e --suppress-level 0.5

# 保存到文件
./examples/linux/v4l2_capture_demo -d /dev/video0 \
    -w 1920 -H 1080 -c 300 -o /tmp/frames/
```

**参数**:
```
-d, --device PATH     # V4L2设备路径
-w, --width W        # 帧宽度
-H, --height H       # 帧高度
-f, --fps FPS        # 帧率
-c, --count N        # 采集帧数 (0=无限)
-o, --output DIR     # 输出目录
-b, --brightness F    # 亮度
-s, --saturation F   # 饱和度
-e, --enhance        # 启用术野增强
--list               # 列出设备
-v, --verbose        # 详细输出
```

---

## 七、SIMD 性能基准测试

### 7.1 运行所有测试

```bash
./tests/test_simd_benchmark
```

### 7.2 运行特定测试

```bash
# 亮度/对比度测试
./tests/test_simd_benchmark --gtest_filter="*Brightness*"

# 全流水线测试
./tests/test_simd_benchmark --gtest_filter="*Pipeline*"

# 特定分辨率
./tests/test_simd_benchmark --gtest_filter="*1920x1080*"
```

### 7.3 输出示例

```
================================================
  SIMD Performance Benchmark
  Backend: AVX2
  CPU: x86_64
================================================

  === Full HD (1920x1080) Performance ===

  Brightness/Contrast: 0.523 ms (1912 fps)
  Saturation:          0.487 ms (2053 fps)
  RGB to Grayscale:    0.198 ms (5050 fps)
  GSDF LUT Apply:      0.612 ms (1634 fps)
  Sobel Edge Detect:   0.489 ms (2045 fps)
  Bloodless Enhance:   0.756 ms (1323 fps)

  FULL PIPELINE:       2.847 ms (351 fps)
```

---

## 八、集成示例

### 8.1 完整处理流水线

```c
#include "v4l2_capture.h"
#include "simd_processing.h"
#include "gpu_pipeline.h"

int main() {
    // 1. 打开摄像头
    V4L2Capture* cap = v4l2_capture_open("/dev/video0", 1920, 1080,
                                          V4L2_PIX_FMT_YUYV, 60);
    v4l2_capture_start(cap);
    
    // 2. 分配缓冲区
    std::vector<uint8_t> yuyv_buffer(1920*1080*2);
    std::vector<uint8_t> rgb_buffer(1920*1080*3);
    
    // 3. SIMD流水线配置
    uint8_t gsdf_lut[256];
    gsdf_generate_lut(gsdf_lut, GSDF_DICOM_PART14);
    
    SimdPipelineConfig simd_config = {
        .brightness = 0.1f,
        .contrast = 1.1f,
        .saturation = 1.2f,
        .gsdf_lut = gsdf_lut,
        .enable_bloodless = true,
        .blood_suppress_level = 0.5f,
        .tissue_enhance = 0.3f,
    };
    
    // 4. 采集循环
    while (running) {
        if (v4l2_capture_frame(cap, yuyv_buffer.data(), 
                               yuyv_buffer.size(), 33) == 0) {
            // YUYV -> RGB
            yuyv_to_rgb(yuyv_buffer.data(), rgb_buffer.data(), 1920, 1080);
            
            // SIMD处理
            std::vector<uint8_t> output(1920*1080*3);
            simd_pipeline_process(rgb_buffer.data(), output.data(),
                               1920, 1080, &simd_config);
            
            // 显示或保存
            display_frame(output.data(), 1920, 1080);
        }
    }
    
    v4l2_capture_stop(cap);
    v4l2_capture_close(cap);
    
    return 0;
}
```

### 8.2 编译

```bash
g++ -o process_video process_video.cpp \
    -I./sdk/performance/include \
    -I./sdk/platform/linux/include \
    -L./build/sdk/performance -lmedicaldisplay_performance \
    -L./build/sdk/platform -lmedicaldisplay_linux \
    -lpthread
```

---

## 九、故障排除

### 9.1 V4L2设备无法打开

```bash
# 检查权限
ls -la /dev/video*

# 添加用户到video组
sudo usermod -a -G video $USER

# 或使用sudo运行
sudo ./v4l2_capture_demo
```

### 9.2 设备不支持请求的格式

```bash
# 查看设备支持格式
./v4l2_capture_demo --list

# 使用设备支持的格式
./v4l2_capture_demo -d /dev/video0 -w 1280 -H 720 -f 30
```

### 9.3 SIMD后端检测失败

```bash
# 查看检测到的后端
./tests/test_simd_benchmark --gtest_filter="*Backend*"

# 如果显示"Scalar"，可能原因:
# - CPU不支持SSE4.2/AVX2
# - 编译器未启用相应选项
```
