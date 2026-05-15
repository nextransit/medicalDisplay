# 故障排除 / Troubleshooting

## 常见问题

### 1. AI识别不准确

**症状:** 影像类型识别错误

**排查步骤:**
```bash
# 1. 检查日志
journalctl -u medical-display -f

# 2. 检查模型文件
ls -la /data/models/*.rknn

# 3. 验证模型加载
./diagnostic_tool --check-model

# 4. 查看NPU状态
cat /sys/class/npu/npu0/state
```

**解决方案:**
- 更新AI模型到最新版本
- 清理模型缓存
- 检查输入影像格式

### 2. 显示色彩异常

**症状:** 色彩偏差大、GSDF不准确

**排查步骤:**
```bash
# 1. 检查GSDF LUT状态
cat /sys/class/drm/card0/degamma_lut_size

# 2. 验证LUT加载
drm_tool --get-gamma

# 3. 检查色彩空间
cat /sys/class/drm/card0/colorspace
```

**解决方案:**
- 重新校准显示器
- 恢复默认GSDF配置
- 检查色彩管理驱动

### 3. OTA更新失败

**症状:** 更新下载或验证失败

**排查步骤:**
```bash
# 1. 检查网络连接
ping api.medical-display.ai

# 2. 验证SSL证书
openssl s_client -connect api.medical-display.ai:443

# 3. 查看更新日志
journalctl -u ota-agent -f

# 4. 检查磁盘空间
df -h /data
```

**解决方案:**
- 清理磁盘空间 (需要 >500MB)
- 重新下载证书
- 手动触发更新

### 4. 云端连接失败

**症状:** MQTT连接断开、HTTPS请求失败

**排查步骤:**
```bash
# 1. 测试连接
curl -v https://api.medical-display.ai/health

# 2. 检查MQTT
mosquitto_sub -t "medical-display/#" -v

# 3. 查看Agent日志
journalctl -u cloud-agent -f

# 4. 检查防火墙
iptables -L | grep 1883
```

**解决方案:**
- 检查网络代理设置
- 确认MQTT端口开放
- 更新Agent到最新版本

### 5. 性能下降

**症状:** 帧率降低、延迟增加

**排查步骤:**
```bash
# 1. 检查CPU/GPU使用
top -b -n 1 | head -20

# 2. 检查NPU使用
cat /sys/class/npu/npu0/utilization

# 3. 查看内存使用
free -h

# 4. 温度检查
cat /sys/class/thermal/thermal_zone0/temp
```

**解决方案:**
- 降低分辨率或帧率
- 关闭不必要的增强功能
- 检查散热系统

## 错误码

### SDK错误码

| 错误码 | 说明 | 解决方案 |
|--------|------|----------|
| -1 | 无效参数 | 检查API调用参数 |
| -2 | 内存不足 | 增加可用内存 |
| -3 | 资源未找到 | 检查文件路径 |
| -4 | 超时 | 增加超时时间 |
| -5 | 权限不足 | 以root运行 |
| -6 | 不支持 | 检查平台兼容性 |
| -7 | 设备忙 | 等待或重启 |
| -8 | 连接失败 | 检查网络 |

### OTA错误码

| 错误码 | 说明 | 解决方案 |
|--------|------|----------|
| 1001 | 下载失败 | 检查网络 |
| 1002 | 签名验证失败 | 重新下载 |
| 1003 | 校验和不匹配 | 重新下载 |
| 1004 | 版本不支持 | 检查兼容性 |
| 1005 | 磁盘空间不足 | 清理空间 |
| 1006 | 安装失败 | 查看日志 |

## 诊断工具

### 完整诊断

```bash
# 运行完整诊断
./diagnostic_tool --full-report --output report.json

# 检查所有组件
./diagnostic_tool --check-all
```

### 报告示例

```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "system": {
    "os": "Ubuntu 22.04",
    "kernel": "5.15.0-generic",
    "uptime": "7 days"
  },
  "ai_engine": {
    "status": "OK",
    "model_loaded": "medical_v1.0.rknn",
    "last_inference_ms": 12.5
  },
  "display": {
    "status": "OK",
    "gsdf_profile": "CT",
    "current_lut": "gsdf_ct.bin"
  },
  "cloud": {
    "status": "CONNECTED",
    "last_sync": "2024-01-15T10:29:00Z"
  }
}
```
