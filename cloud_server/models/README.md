# 模型与固件占位工件

该目录用于模拟云端模型仓库与 OTA 工件仓库：

- `*.manifest.json`：模型元数据
- `*.onnx` / `*.bin`：占位模型文件
- `medical_display_firmware_*.bin`：占位固件包

接入真实后端时，可直接替换这些工件并保持相同文件名或更新清单元数据。
