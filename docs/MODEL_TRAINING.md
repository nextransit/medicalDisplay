# AI模型训练指南

## 概述

本文档介绍如何训练和优化医疗影像识别AI模型。

## 支持的模态

| 模态 | 说明 | 训练数据量建议 |
|------|------|---------------|
| CT | 计算机断层扫描 | 10,000+ |
| MR | 磁共振成像 | 10,000+ |
| DR/CR | 数字X光 | 20,000+ |
| US | 超声 | 50,000+ |
| ES | 内窥镜 | 30,000+ |
| SM | 病理切片 | 50,000+ |

## 模型架构

### 推荐架构

1. **轻量级 (边缘部署)**
   - MobileNetV3-Large
   - EfficientNet-B0
   - RepVGG-A0

2. **标准精度**
   - EfficientNet-B3
   - DeiT-Tiny
   - ResNet-50

3. **高精度**
   - EfficientNet-B5
   - ViT-Small
   - Swin-T

## 训练流程

### 1. 数据准备

```python
import os
from pathlib import Path

# 目录结构
data_dir = Path("medical_imaging_data/")
train_dir = data_dir / "train"
val_dir = data_dir / "val"

classes = ["CT", "MR", "DR", "CR", "US", "PT", "ES", "SM", "SV"]
```

### 2. 数据增强

```python
import albumentations as A

train_transform = A.Compose([
    A.RandomResizedCrop(224, 224, scale=(0.8, 1.0)),
    A.HorizontalFlip(p=0.5),
    A.ShiftScaleRotate(shift_limit=0.1, scale_limit=0.1, rotate_limit=15),
    A.RandomBrightnessContrast(p=0.5),
    A.Normalize(mean=[0.485], std=[0.229])  # 单通道灰度
])
```

### 3. 模型训练

```python
import torch
import timm

# 创建模型
model = timm.create_model(
    'efficientnet_b0',
    pretrained=True,
    num_classes=len(classes),
    in_chans=1  # 灰度图像
)

# 训练配置
config = {
    'epochs': 100,
    'batch_size': 32,
    'learning_rate': 1e-4,
    'weight_decay': 1e-5,
    'warmup_epochs': 5
}
```

### 4. 量化训练 (QAT)

```python
import torch.quantization as tq

# 感知量化训练
model.qconfig = tq.get_default_qconfig('fbgemm')
tq.prepare_qat(model, inplace=True)

# 微调几个epoch恢复精度
train_model(model, epochs=5)
```

## 模型优化

### 剪枝

```python
def structured_prune(model, prune_ratio=0.3):
    """结构化剪枝"""
    for name, module in model.named_modules():
        if isinstance(module, torch.nn.Conv2d):
            # 按通道剪枝
            prune.l1_unstructured(module, name='weight', amount=prune_ratio)
            prune.remove(module, 'weight')
```

### RepVGG结构重参数化

```python
def repvgg_convert(model):
    """转换为RepVGG部署结构"""
    for name, module in model.named_modules():
        if hasattr(module, 'switch_to_deploy'):
            module.switch_to_deploy()
    return model
```

## RKNN模型转换

```bash
# 使用RKNN Toolkit转换
python3 rknn_convert.py \
    --input_model efficientnet_b0.onnx \
    --output_model efficientnet_b0.rknn \
    --platform RK3588 \
    --quantize Dataset/calibration.txt \
    --batch_size 1
```

## 模型验证

### 精度测试

```python
def evaluate_model(model, test_loader):
    model.eval()
    correct = 0
    total = 0
    
    with torch.no_grad():
        for images, labels in test_loader:
            outputs = model(images)
            _, predicted = outputs.max(1)
            total += labels.size(0)
            correct += predicted.eq(labels).sum().item()
    
    accuracy = 100. * correct / total
    return accuracy
```

### 延迟测试

```python
import time

def benchmark_latency(model, input_size=(1, 1, 224, 224)):
    model.eval()
    warmup_runs = 10
    test_runs = 100
    
    # Warmup
    x = torch.randn(input_size)
    for _ in range(warmup_runs):
        _ = model(x)
    
    # Benchmark
    latencies = []
    for _ in range(test_runs):
        start = time.perf_counter()
        _ = model(x)
        latencies.append(time.perf_counter() - start)
    
    return {
        'mean': np.mean(latencies) * 1000,  # ms
        'p50': np.percentile(latencies, 50) * 1000,
        'p95': np.percentile(latencies, 95) * 1000,
        'p99': np.percentile(latencies, 99) * 1000
    }
```

## 部署检查清单

- [ ] 精度满足要求 (>90% 分类准确率)
- [ ] 延迟满足要求 (<50ms 边缘设备)
- [ ] 模型大小符合限制 (<100MB)
- [ ] INT8量化精度损失 <2%
- [ ] RKNN/TensorRT转换成功
- [ ] 边缘设备验证通过
