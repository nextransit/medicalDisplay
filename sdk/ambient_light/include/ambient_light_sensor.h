/**
 * @file ambient_light_sensor.h
 * @brief 环境光自适应传感器接口
 *
 * 根据环境光照度自动调整显示器亮度、GSDF曲线和色彩空间。
 * 支持 I2C 光传感器 (VEML7700, TCS34725 等) 和软件估算。
 */

#ifndef AMBIENT_LIGHT_SENSOR_H
#define AMBIENT_LIGHT_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 传感器类型
// ============================================================================
typedef enum {
    ALS_VEML7700 = 0,        // Vishay VEML7700 (I2C, 0.004-120k lux)
    ALS_TCS34725,            // AMS TCS34725 (I2C, RGB+Clear)
    ALS_OPT3001,             // TI OPT3001 (I2C, 0.01-83k lux)
    ALS_BH1750,              // ROHM BH1750 (I2C, 1-65535 lux)
    ALS_SOFTWARE_ESTIMATE,   // 软件估算 (基于摄像头/时间)
    ALS_COUNT
} ALSSensorType;

// ============================================================================
// 传感器配置
// ============================================================================
typedef struct {
    ALSSensorType sensor_type;    // 传感器类型
    char i2c_device[64];          // I2C 设备路径 (/dev/i2c-1)
    uint8_t i2c_address;          // I2C 地址
    uint32_t sample_interval_ms;  // 采样间隔 (默认 1000ms)
    bool enable_averaging;        // 启用滑动平均
    uint8_t average_window;       // 平均窗口大小 (默认 8)
} ALSConfig;

// ============================================================================
// 光照读数
// ============================================================================
typedef struct {
    float lux;                    // 照度 (lux)
    float color_temp_k;           // 色温 (K, 如有色度传感器)
    uint32_t raw_clear;           // Clear通道原始值
    uint32_t raw_red;             // Red通道原始值
    uint32_t raw_green;           // Green通道原始值
    uint32_t raw_blue;            // Blue通道原始值
    uint64_t timestamp_us;        // 时间戳 (微秒)
} ALSReading;

// ============================================================================
// 自适应策略
// ============================================================================
typedef enum {
    ALS_STRATEGY_LINEAR = 0,      // 线性映射
    ALS_STRATEGY_STEPPED,         // 阶梯映射 (DICOM 推荐)
    ALS_STRATEGY_SIGMOID,         // S型曲线映射
    ALS_STRATEGY_CUSTOM           // 自定义曲线
} ALSStrategy;

// ============================================================================
// 自适应配置
// ============================================================================
typedef struct {
    ALSStrategy strategy;         // 映射策略
    float min_luminance;          // 最小亮度 (cd/m²)
    float max_luminance;          // 最大亮度
    float min_lux;                // 最小照度阈值
    float max_lux;                // 最大照度阈值
    float target_luminance_ratio; // 目标亮度比 (0.5=50%)
    bool adjust_gsdf;             // 是否调整GSDF
    bool adjust_color_temp;       // 是否调整色温
    float color_temp_target;      // 目标色温 (K)
} ALSAdaptiveConfig;

// ============================================================================
// 输出: 显示调整建议
// ============================================================================
typedef struct {
    float recommended_luminance;  // 推荐亮度 (cd/m²)
    float luminance_ratio;        // 亮度比 (0-1)
    float recommended_color_temp; // 推荐色温 (K)
    float delta_e_estimate;       // 预估 ΔE
    bool needs_calibration;       // 是否需要校准
    char description[256];        // 调整描述
} ALSDisplayAdjustment;

// ============================================================================
// 传感器句柄 (opaque)
// ============================================================================
typedef struct ALSSensor ALSSensor;

// ============================================================================
// 生命周期
// ============================================================================

ALSSensor* als_sensor_create(const ALSConfig* config);
void als_sensor_destroy(ALSSensor* sensor);

// ============================================================================
// 读数
// ============================================================================

/**
 * 读取当前光照度
 * @return 0 成功
 */
int als_sensor_read(ALSSensor* sensor, ALSReading* reading);

/**
 * 获取滑动平均光照度
 * @return 平均 lux 值
 */
float als_sensor_get_average_lux(ALSSensor* sensor);

// ============================================================================
// 自适应计算
// ============================================================================

/**
 * 根据光照度计算显示调整建议
 * @param lux 当前光照度
 * @param config 自适应配置
 * @param adjustment 输出调整建议
 * @return 0 成功
 */
int als_compute_adjustment(float lux,
                           const ALSAdaptiveConfig* config,
                           ALSDisplayAdjustment* adjustment);

/**
 * 预设自适应配置 (诊断室)
 */
ALSAdaptiveConfig als_preset_diagnostic(void);

/**
 * 预设自适应配置 (手术室)
 */
ALSAdaptiveConfig als_preset_surgical(void);

/**
 * 预设自适应配置 (会诊室)
 */
ALSAdaptiveConfig als_preset_consultation(void);

// ============================================================================
// 软件估算 (无传感器时)
// ============================================================================

/**
 * 基于时间估算环境光照度
 * @param hour 当前小时 (0-23)
 * @param latitude 纬度
 * @return 估算 lux 值
 */
float als_estimate_from_time(int hour, float latitude);

#ifdef __cplusplus
}
#endif

#endif // AMBIENT_LIGHT_SENSOR_H
