# 平台支持 / Platform Support

## 支持的平台

### Linux 发行版

| 发行版 | 支持版本 | 最低内核 | 备注 |
|--------|----------|----------|------|
| Ubuntu | 20.04, 22.04, 24.04 | 5.4 | 推荐 |
| Debian | 11, 12 | 5.10 | |
| RHEL/CentOS | 8, 9 | 4.18 | |
| Rocky Linux | 8, 9 | 4.18 | |
| 麒麟V10 | SP1, SP2, SP3 | 4.19 | 国产OS |
| 统信UOS | 20, 21 | 4.19 | 国产OS |

### Android 版本

| 版本 | API级别 | 支持 |
|------|---------|------|
| Android 10 | 29 | 是 |
| Android 11 | 30 | 是 |
| Android 12 | 31 | 是 |
| Android 13 | 33 | 是 |
| Android 14 | 34 | 是 |

## RK3588 平台

### 硬件规格

| 参数 | 规格 |
|------|------|
| CPU | Cortex-A76x4 + Cortex-A55x4 |
| GPU | Mali-G610 MP4 |
| NPU | 6 TOPS |
| 内存 | 8GB/16GB LPDDR4X |
| 存储 | eMMC 5.1 / NVMe |

### AI加速配置

```bash
# 检查NPU状态
cat /sys/class/npu/npu0/device/id

# 查看NPU利用率
cat /sys/class/npu/npu0/utilization

# RKNN模型推理
./rknn_model_zoo/examples/rknn_ssd_model_zoo \
    --model_path /data/models/medical.rknn \
    --input_path /data/test/image.jpg
```

### 显示配置

```bash
# DRM设备
ls -la /dev/dri/

# 查看显示模式
cat /sys/class/drm/card0-DSI-1/modes

# 设置分辨率
modetest -M rk3588 -s 39:1920x1080@60
```

### 性能基准

| 测试项 | 性能 |
|--------|------|
| AI推理延迟 (6MP CT) | <15ms |
| 帧率 (4K@60) | 60 fps |
| 色彩深度 | 12-bit |
| GSDF LUT切换 | <5ms |

## Jetson Orin 平台

### 硬件规格

| 参数 | Orin Nano | Orin NX | Orin AGX |
|------|-----------|----------|-----------|
| GPU | Ampere 512-core | Ampere 1024-core | Ampere 2048-core |
| NPU | - | - | - |
| GPU算力 | 40 TOPS | 100 TOPS | 275 TOPS |
| 内存 | 4-8GB | 8-16GB | 32-64GB |

### TensorRT优化

```python
import tensorrt as trt

# 加载ONNX模型
with trt.Builder(TRT_LOGGER) as builder:
    with builder.create_network() as network:
        parser = trt.OnnxParser(network, TRT_LOGGER)
        with open('model.onnx', 'rb') as f:
            parser.parse(f.read())
        
        config = builder.create_builder_config()
        config.set_memory_pool_limit(trt.MemoryPoolType.WORKSPACE, 1 << 30)
        
        # INT8量化
        config.int8_mode = True
        config.int8_calibrator = Calibrator()
        
        engine = builder.build_serialized_network(network, config)
```

### 性能基准

| 测试项 | Orin Nano | Orin AGX |
|--------|-----------|-----------|
| AI推理延迟 | <10ms | <5ms |
| 帧率 (4K@60) | 60 fps | 60 fps |
| GPU利用率 | 85% | 90% |

## Intel NUC 平台

### 硬件规格

| 参数 | NUC 12 Pro | NUC 13 Pro |
|------|------------|-------------|
| CPU | i7-1265U | i7-1365U |
| GPU | Iris Xe | Iris Xe |
| 内存 | 16-64GB | 16-64GB |

### OpenVINO优化

```python
from openvino.runtime import Core

# 加载并优化模型
core = Core()
model = core.read_model('model.xml')

# 编译为GPU
compiled_model = core.compile_model(model, 'GPU')

# 推理
infer_request = compiled_model.create_infer_request()
results = infer_request.infer({input_tensor: image_data})
```

## 性能对比

| 平台 | NPU/GPU | 推理延迟 | 4K@60fps | 12-bit |
|------|---------|----------|----------|--------|
| RK3588 | Mali-G610 | 15ms | ✓ | ✓ |
| Jetson Orin | NVIDIA GPU | 5ms | ✓ | ✓ |
| Intel NUC | Iris Xe | 12ms | ✓ | ✓ |
| RK3576 | Mali-G52 | 25ms | ✓ | ✓ |

## 已知限制

### RK3588

- ⚠️ Vulkan 1.3 需要内核 ≥ 5.10
- ⚠️ DRM color management 需要内核 ≥ 6.1
- ⚠️ 多屏同步有时延抖动

### Jetson Orin

- ⚠️ TensorRT 8.6+ 需要 JetPack 5.1+
- ⚠️ NPU仅支持特定模型格式
- ⚠️ 热功耗限制需调整

### Intel NUC

- ⚠️ Iris Xe OpenCL 驱动稳定性问题
- ⚠️ 多屏同步支持有限
