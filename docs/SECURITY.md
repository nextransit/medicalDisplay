# 安全设计 / Security Design

## 概述

本文档介绍AI Adaptive Medical Display System的安全设计。

## 安全架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                        安全架构                                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐      │
│  │                      网络安全                               │      │
│  │  • TLS 1.3 加密通信                                       │      │
│  │  • MQTT over TLS                                         │      │
│  │  • API 密钥认证                                           │      │
│  └─────────────────────────────────────────────────────────────┘      │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐      │
│  │                      OTA安全                               │      │
│  │  • 包签名验证 (RSA-4096)                                 │      │
│  │  • SHA-256 完整性校验                                     │      │
│  │  • 版本回滚保护                                           │      │
│  └─────────────────────────────────────────────────────────────┘      │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐      │
│  │                      数据安全                               │      │
│  │  • 医疗数据加密存储                                       │      │
│  │  • DICOM脱敏处理                                         │      │
│  │  • 审计日志                                               │      │
│  └─────────────────────────────────────────────────────────────┘      │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## OTA安全

### 包签名验证

```c
// OTA包签名验证
typedef struct {
    char package_id[64];
    char version[32];
    uint32_t signature_size;
    uint8_t signature[512];  // RSA-4096签名
    uint8_t checksum[32];    // SHA-256
} OTA_PackageHeader;

// 验证流程
int verify_ota_package(const OTA_PackageHeader* header, const uint8_t* package_data) {
    // 1. 验证校验和
    uint8_t checksum[32];
    sha256(package_data, &checksum);
    if (memcmp(checksum, header->checksum, 32) != 0) {
        return ERROR_CHECKSUM_MISMATCH;
    }
    
    // 2. 验证签名
    if (!rsa_verify(header->signature, sizeof(header->signature),
                    package_data, header->checksum)) {
        return ERROR_SIGNATURE_INVALID;
    }
    
    return 0;
}
```

### 版本回滚保护

```c
// 禁止降级到不安全版本
int check_version_rollback(const char* current_version, const char* new_version) {
    // 版本格式: major.minor.patch
    // 安全版本检查
    if (is_critical_security_update(current_version)) {
        // 关键安全更新后禁止回滚
        return ERROR_SECURITY_ROLLBACK_BLOCKED;
    }
    return 0;
}
```

## API安全

### 认证机制

```bash
# 使用API密钥
curl -H "X-API-Key: your-api-key" \
     https://api.example.com/v1/devices

# JWT Token (可选)
curl -H "Authorization: Bearer jwt-token" \
     https://api.example.com/v1/devices
```

### 速率限制

| 端点 | 限制 | 窗口 |
|------|------|------|
| /devices/register | 10/分钟 | 滑动 |
| /telemetry | 100/分钟 | 滑动 |
| /models/check | 60/分钟 | 滑动 |

## 医疗数据安全

### 数据分类

| 级别 | 数据类型 | 保护要求 |
|------|----------|----------|
| PII | 设备ID、位置 | 加密存储 |
| PHI | 患者信息 | HIPAA合规 |
| 操作日志 | 时间戳、操作 | 完整保留 |

### 数据加密

```python
# 数据加密存储
from cryptography.fernet import Fernet

class SecureStorage:
    def __init__(self, key):
        self.cipher = Fernet(key)
    
    def store_calibration_report(self, report: dict) -> bytes:
        """加密存储校准报告"""
        json_data = json.dumps(report).encode()
        return self.cipher.encrypt(json_data)
    
    def load_calibration_report(self, encrypted: bytes) -> dict:
        """解密读取校准报告"""
        json_data = self.cipher.decrypt(encrypted)
        return json.loads(json_data)
```

## 审计日志

### 日志内容

```json
{
    "timestamp": "2024-01-15T10:30:00Z",
    "event_type": "CALIBRATION_UPLOAD",
    "device_id": "DISPLAY-001",
    "user_id": "admin@example.com",
    "result": "SUCCESS",
    "details": {
        "delta_e_avg": 1.2,
        "calibration_id": "cal-12345"
    },
    "ip_address": "192.168.1.100"
}
```

### 日志保留

- **操作日志**: 2年
- **安全日志**: 5年
- **校准报告**: 设备生命周期 + 1年
