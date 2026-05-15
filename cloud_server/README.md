# 云端后端模拟服务

这是 `medicalDisplay` 的云端服务端参考实现，覆盖设备注册、遥测采集、模型分发、OTA 升级、校准报告、远程命令和 MQTT 事件联动。

## 目录结构

```text
cloud_server/
├── Dockerfile
├── README.md
├── requirements.txt
├── config/
│   └── settings.json
├── models/
│   ├── README.md
│   ├── ct_modality_classifier_v1_2_0.manifest.json
│   ├── ct_modality_classifier_v1_2_0.onnx
│   ├── mri_sequence_classifier_v3_0_0.manifest.json
│   ├── mri_sequence_classifier_v3_0_0.onnx
│   ├── federated_display_optimizer_v2_0_0.manifest.json
│   ├── federated_display_optimizer_v2_0_0.bin
│   ├── medical_display_firmware_2.1.0.bin
│   └── medical_display_firmware_2.1.0.delta.bin
└── src/
    ├── __init__.py
    ├── calibration_service.py
    ├── device_manager.py
    ├── main.py
    ├── model_store.py
    ├── ota_service.py
    └── telemetry_collector.py
```

## 本地启动

```bash
cd cloud_server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn src.main:app --host 0.0.0.0 --port 8080 --reload
```

默认配置文件为 `cloud_server/config/settings.json`，也可以通过环境变量 `CLOUD_SERVER_SETTINGS` 指定。

## Docker 启动

```bash
cd cloud_server
docker build -t medical-display-cloud .
docker run --rm -p 8080:8080 medical-display-cloud
```

## 关键接口

- `GET /health`
- `POST /api/v1/devices/register`
- `POST /api/v1/devices/{device_id}/telemetry`
- `POST /api/v1/models/check-update`
- `POST /api/v1/ota/check`
- `POST /api/v1/calibration/reports`
- `POST /api/v1/commands`

## 说明

- 默认使用 SQLite，数据库文件位于 `cloud_server/data/cloud_server.db`
- MQTT 为可选集成，关闭时不影响 REST API
- `cloud_server/models/` 中的模型与固件文件均为占位样例，可替换成真实工件
