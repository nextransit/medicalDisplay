# Linux 示例

## 构建

```bash
cmake -S examples/linux -B build/examples/linux
cmake --build build/examples/linux -j
```

## 运行

```bash
./build/examples/linux/medical_display_demo
./build/examples/linux/dicom_receiver --self-test
./build/examples/linux/multi_display_demo
./build/examples/linux/gsdf_calibration_demo
./build/examples/linux/cloud_demo
```

## 说明

- 所有示例默认可在无外部数据文件时直接运行。
- `medical_display_demo` 会在输出目录生成 `.dcm` 与 `.pgm` 预览文件。
- `dicom_receiver --self-test` 会在本机回环接口上完成一次最小 C-STORE 往返。
- `multi_display_demo` 会为每个逻辑显示器输出同步后的帧文件。
- `gsdf_calibration_demo` 使用模拟色度计生成校准报告。
- `cloud_demo` 会自动生成 OTA 清单与升级包，演示连接、遥测、校验与应用更新。
