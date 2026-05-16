#ifndef MULTI_DISPLAY_HUB_H
#define MULTI_DISPLAY_HUB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 显示器类型
// ============================================================================
typedef enum {
    DISPLAY_TYPE_DIAGNOSTIC = 0,   // 诊断级显示器
    DISPLAY_TYPE_CLINICAL,          // 临床级显示器
    DISPLAY_TYPE_SURGICAL,          // 手术显示器
    DISPLAY_TYPE_CONSULTATION,      // 会诊显示器
    DISPLAY_TYPE_COUNT
} DisplayType;

// ============================================================================
// 显示器状态
// ============================================================================
typedef enum {
    DISPLAY_STATUS_OFFLINE = 0,
    DISPLAY_STATUS_ONLINE,
    DISPLAY_STATUS_CALIBRATING,
    DISPLAY_STATUS_ERROR
} DisplayStatus;

// ============================================================================
// 单个显示器配置
// ============================================================================
typedef struct {
    int display_id;                 // 显示器ID
    DisplayType type;               // 显示器类型
    DisplayStatus status;           // 状态
    int width;                      // 分辨率宽
    int height;                     // 分辨率高
    int bit_depth;                  // 位深 (8/10/12)
    float max_luminance;            // 最大亮度 (cd/m²)
    float min_luminance;            // 最小亮度 (cd/m²)
    float current_luminance;        // 当前亮度
    float calibration_age_days;     // 校准天数
    char display_name[128];         // 显示器名称
    char edid_hash[64];            // EDID指纹
    int is_primary;                // 是否主显示器
} DisplayInfo;

// ============================================================================
// 协同显示策略
// ============================================================================
typedef enum {
    SYNC_MODE_INDEPENDENT = 0,      // 独立显示
    SYNC_MODE_MASTER_SLAVE,         // 主从同步
    SYNC_MODE_BLIND_REVIEW,         // 双盲阅片
    SYNC_MODE_CONSULTATION         // 会诊模式
} SyncMode;

// ============================================================================
// 色彩一致性配置
// ============================================================================
typedef struct {
    bool enable_color_matching;     // 启用色彩匹配
    int target_colorspace;          // 目标色彩空间 (0=sRGB, 1=DCI-P3, 2=Rec2020)
    float target_white_point_x;     // 目标白点X (典型值 0.3127)
    float target_white_point_y;     // 目标白点Y (典型值 0.3290)
    float target_luminance;         // 目标亮度 (cd/m²)
    bool apply_gsdf;               // 应用GSDF
    int ambient_light_lux;          // 环境光 (lux)
} ColorConsistencyConfig;

// ============================================================================
// 多屏协同引擎句柄
// ============================================================================
typedef struct MultiDisplayHub MultiDisplayHub;

// ============================================================================
// 引擎生命周期
// ============================================================================

/**
 * 创建多屏协同引擎
 * @param max_displays 最大支持的显示器数量
 * @return 引擎句柄，失败返回NULL
 */
MultiDisplayHub* multi_display_hub_create(int max_displays);

/**
 * 销毁多屏协同引擎
 * @param hub 引擎句柄
 */
void multi_display_hub_destroy(MultiDisplayHub* hub);

/**
 * 扫描并检测所有连接的显示器
 * @param hub 引擎句柄
 * @param displays 输出显示器数组
 * @param max_count 最大数量
 * @return 检测到的显示器数量，-1失败
 */
int multi_display_hub_scan_displays(MultiDisplayHub* hub,
                                    DisplayInfo* displays,
                                    int max_count);

/**
 * 获取显示器信息
 * @param hub 引擎句柄
 * @param display_id 显示器ID
 * @param info 输出信息
 * @return 0成功，-1失败
 */
int multi_display_hub_get_display_info(MultiDisplayHub* hub,
                                       int display_id,
                                       DisplayInfo* info);

/**
 * 设置主显示器
 * @param hub 引擎句柄
 * @param display_id 显示器ID
 * @return 0成功，-1失败
 */
int multi_display_hub_set_primary(MultiDisplayHub* hub, int display_id);

// ============================================================================
// 色彩一致性管理
// ============================================================================

/**
 * 配置色彩一致性参数
 * @param hub 引擎句柄
 * @param config 色彩配置
 * @return 0成功，-1失败
 */
int multi_display_hub_set_color_config(MultiDisplayHub* hub,
                                       const ColorConsistencyConfig* config);

/**
 * 获取当前色彩一致性配置
 * @param hub 引擎句柄
 * @param config 输出配置
 * @return 0成功，-1失败
 */
int multi_display_hub_get_color_config(MultiDisplayHub* hub,
                                       ColorConsistencyConfig* config);

/**
 * 应用GSDF到指定显示器
 * @param hub 引擎句柄
 * @param display_id 显示器ID
 * @param ambient_lux 环境光 (lux)
 * @return 0成功，-1失败
 */
int multi_display_hub_apply_gsdf(MultiDisplayHub* hub,
                                  int display_id,
                                  int ambient_lux);

/**
 * 生成并应用统一LUT
 * @param hub 引擎句柄
 * @param display_ids 目标显示器ID数组
 * @param count 显示器数量
 * @return 0成功，-1失败
 */
int multi_display_hub_apply_unified_lut(MultiDisplayHub* hub,
                                        const int* display_ids,
                                        int count);

// ============================================================================
// 协同显示模式
// ============================================================================

/**
 * 设置协同显示模式
 * @param hub 引擎句柄
 * @param mode 同步模式
 * @return 0成功，-1失败
 */
int multi_display_hub_set_sync_mode(MultiDisplayHub* hub, SyncMode mode);

/**
 * 获取当前同步模式
 * @param hub 引擎句柄
 * @return 当前模式
 */
SyncMode multi_display_hub_get_sync_mode(MultiDisplayHub* hub);

/**
 * 同步切换显示器内容
 * @param hub 引擎句柄
 * @param display_ids 目标显示器数组
 * @param count 数量
 * @param source_data 源图像数据
 * @param width 宽度
 * @param height 高度
 * @return 0成功，-1失败
 */
int multi_display_hub_sync_render(MultiDisplayHub* hub,
                                  const int* display_ids,
                                  int count,
                                  const uint8_t* source_data,
                                  int width, int height);

// ============================================================================
// 校准管理
// ============================================================================

/**
 * 开始显示器校准流程
 * @param hub 引擎句柄
 * @param display_id 显示器ID
 * @return 0成功，-1失败
 */
int multi_display_hub_start_calibration(MultiDisplayHub* hub, int display_id);

/**
 * 获取校准状态
 * @param hub 引擎句柄
 * @param display_id 显示器ID
 * @param progress 输出进度 (0.0-1.0)
 * @param status 输出状态字符串
 * @param status_len 状态字符串缓冲区长度
 * @return 0成功，-1失败
 */
int multi_display_hub_get_calibration_status(MultiDisplayHub* hub,
                                             int display_id,
                                             float* progress,
                                             char* status,
                                             int status_len);

/**
 * 完成校准并保存
 * @param hub 引擎句柄
 * @param display_id 显示器ID
 * @return 0成功，-1失败
 */
int multi_display_hub_complete_calibration(MultiDisplayHub* hub, int display_id);

// ============================================================================
// 诊断与日志
// ============================================================================

/**
 * 执行显示器健康检查
 * @param hub 引擎句柄
 * @param display_id 显示器ID
 * @param issues 输出问题数组 (如 "luminance_drift", "color_drift", etc.)
 * @param max_issues 最大问题数
 * @return 检测到的问题数，-1失败
 */
int multi_display_hub_health_check(MultiDisplayHub* hub,
                                    int display_id,
                                    char** issues,
                                    int max_issues);

/**
 * 导出校准报告
 * @param hub 引擎句柄
 * @param display_id 显示器ID
 * @param report_path 输出报告路径
 * @return 0成功，-1失败
 */
int multi_display_hub_export_calibration_report(MultiDisplayHub* hub,
                                                 int display_id,
                                                 const char* report_path);

#ifdef __cplusplus
}
#endif

#endif // MULTI_DISPLAY_HUB_H
