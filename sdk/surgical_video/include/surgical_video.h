#ifndef SURGICAL_VIDEO_H
#define SURGICAL_VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 术野类型
// ============================================================================
typedef enum {
    SURGICAL_LAPAROSCOPIC = 0,   // 腹腔镜
    SURGICAL_ENDOSCOPIC,           // 内窥镜
    SURGICAL_MICROSCOPIC,         // 手术显微镜
    SURGICAL_ARTHROSCOPIC,       // 关节镜
    SURGICAL_BRACHYTHERAPY,       // 近距离放疗
    SURGICAL_GENERAL              // 普通手术
} SurgicalType;

// ============================================================================
// 增强模式
// ============================================================================
typedef enum {
    ENHANCE_NONE = 0,
    ENHANCE_TISSUE_BOUNDRY,       // 组织边界增强
    ENHANCE_VASCULAR,             // 血管增强
    ENHANCE_NERVE,               // 神经增强
    ENHANCE_TUMOR,               // 肿瘤标记
    ENHANCE_BLOOD_LESS,          // 无血术野
    ENHANCE_STRUCTURE,           // 结构增强
    ENHANCE_COUNT
} EnhancementMode;

// ============================================================================
// 视频帧信息
// ============================================================================
typedef struct {
    uint32_t width;               // 帧宽度
    uint32_t height;              // 帧高度
    uint32_t format;             // 格式 (0=YUV, 1=RGB, 2=YUV422)
    uint32_t timestamp_us;        // 时间戳 (微秒)
    float frame_rate;            // 帧率
    float exposure_ms;           // 曝光时间 (毫秒)
    float gain_db;               // 增益 (dB)
} VideoFrameInfo;

// ============================================================================
// 增强参数
// ============================================================================
typedef struct {
    // 基础增强
    float brightness;             // 亮度调整 (-1.0 to 1.0)
    float contrast;              // 对比度调整
    float saturation;            // 饱和度调整
    float sharpness;             // 锐化强度
    
    // 血流抑制
    float bloodless_strength;    // 无血术野强度 (0.0-1.0)
    float bloodless_threshold;   // 血流检测阈值
    
    // 边缘增强
    EnhancementMode mode;        // 增强模式
    float edge_strength;         // 边缘增强强度
    float edge_threshold;        // 边缘检测阈值
    float edge_thickness;        // 边缘线条粗细
    
    // 色彩映射
    bool pseudo_color_enable;    // 伪彩色启用
    float pseudo_color_intensity; // 伪彩色强度
    
    // HDR
    bool hdr_enable;             // HDR启用
    float hdr_exposure_comp;    // 曝光补偿
    
    // AR叠加 (预留)
    bool ar_overlay_enable;      // AR叠加启用
    float ar_opacity;           // AR层不透明度
} EnhancementParams;

// ============================================================================
// 性能统计
// ============================================================================
typedef struct {
    uint32_t frames_processed;   // 已处理帧数
    float total_latency_ms;      // 总延迟累计
    float avg_latency_ms;        // 平均延迟
    float p95_latency_ms;        // P95延迟
    float p99_latency_ms;        // P99延迟
    float max_latency_ms;        // 最大延迟
    uint32_t dropped_frames;     // 丢帧数
    float current_fps;           // 当前帧率
    float gpu_utilization;       // GPU利用率
    float memory_used_mb;        // 内存使用 (MB)
} PerformanceStats;

// ============================================================================
// 术野视频引擎句柄
// ============================================================================
typedef struct SurgicalVideoEngine SurgicalVideoEngine;

// ============================================================================
// 引擎生命周期
// ============================================================================

/**
 * 创建术野视频增强引擎
 * @param surgical_type 手术类型
 * @param use_gpu 是否使用GPU加速
 * @return 引擎句柄，失败返回NULL
 */
SurgicalVideoEngine* surgical_engine_create(SurgicalType surgical_type, bool use_gpu);

/**
 * 销毁术野视频增强引擎
 * @param engine 引擎句柄
 */
void surgical_engine_destroy(SurgicalVideoEngine* engine);

/**
 * 配置引擎参数
 * @param engine 引擎句柄
 * @param params 增强参数
 * @return 0成功，-1失败
 */
int surgical_engine_config(SurgicalVideoEngine* engine, const EnhancementParams* params);

/**
 * 获取当前参数
 * @param engine 引擎句柄
 * @param params 输出参数
 * @return 0成功，-1失败
 */
int surgical_engine_get_config(SurgicalVideoEngine* engine, EnhancementParams* params);

// ============================================================================
// 帧处理
// ============================================================================

/**
 * 处理视频帧
 * @param engine 引擎句柄
 * @param input 输入帧数据
 * @param input_info 输入帧信息
 * @param output 输出帧数据
 * @param output_info 输出帧信息
 * @return 0成功，-1失败
 */
int surgical_engine_process_frame(SurgicalVideoEngine* engine,
                                 const uint8_t* input,
                                 const VideoFrameInfo* input_info,
                                 uint8_t* output,
                                 VideoFrameInfo* output_info);

/**
 * 批量处理帧
 * @param engine 引擎句柄
 * @param frames 输入帧数组
 * @param count 帧数量
 * @param outputs 输出帧数组
 * @return 成功处理数量，-1失败
 */
int surgical_engine_process_batch(SurgicalVideoEngine* engine,
                                 const uint8_t** frames,
                                 int count,
                                 uint8_t** outputs);

/**
 * 批量处理帧（带每帧元数据）
 * @param engine 引擎句柄
 * @param frames 输入帧数组
 * @param frame_infos 输入帧信息数组
 * @param count 帧数量
 * @param outputs 输出帧数组
 * @param output_infos 输出帧信息数组，可为NULL
 * @return 成功处理数量，-1失败
 */
int surgical_engine_process_batch_ex(SurgicalVideoEngine* engine,
                                    const uint8_t** frames,
                                    const VideoFrameInfo* frame_infos,
                                    int count,
                                    uint8_t** outputs,
                                    VideoFrameInfo* output_infos);

/**
 * 设置增强模式
 * @param engine 引擎句柄
 * @param mode 增强模式
 * @return 0成功
 */
int surgical_engine_set_mode(SurgicalVideoEngine* engine, EnhancementMode mode);

/**
 * 获取当前增强模式
 * @param engine 引擎句柄
 * @return 当前模式
 */
EnhancementMode surgical_engine_get_mode(SurgicalVideoEngine* engine);

// ============================================================================
// AR叠加 (预留)
// ============================================================================

/**
 * 设置AR叠加数据
 * @param engine 引擎句柄
 * @param overlay_data AR叠加数据
 * @param data_size 数据大小
 * @return 0成功
 */
int surgical_engine_set_ar_overlay(SurgicalVideoEngine* engine,
                                  const uint8_t* overlay_data,
                                  size_t data_size);

/**
 * 更新AR标注
 * @param engine 引擎句柄
 * @param annotations JSON格式标注
 * @return 0成功
 */
int surgical_engine_update_annotations(SurgicalVideoEngine* engine,
                                      const char* annotations);

// ============================================================================
// 性能监控
// ============================================================================

/**
 * 获取性能统计
 * @param engine 引擎句柄
 * @param stats 输出统计
 * @return 0成功
 */
int surgical_engine_get_stats(SurgicalVideoEngine* engine, PerformanceStats* stats);

/**
 * 重置统计
 * @param engine 引擎句柄
 */
void surgical_engine_reset_stats(SurgicalVideoEngine* engine);

/**
 * 检查是否满足延迟要求
 * @param engine 引擎句柄
 * @param max_latency_ms 最大允许延迟
 * @return true满足要求
 */
bool surgical_engine_check_latency(SurgicalVideoEngine* engine, float max_latency_ms);

// ============================================================================
// 预设配置
// ============================================================================

/**
 * 应用腹腔镜预设
 * @param params 输出参数
 */
void surgical_preset_laparoscopic(EnhancementParams* params);

/**
 * 应用内窥镜预设
 * @param params 输出参数
 */
void surgical_preset_endoscopic(EnhancementParams* params);

/**
 * 应用显微镜预设
 * @param params 输出参数
 */
void surgical_preset_microscopic(EnhancementParams* params);

/**
 * 应用无血术野预设
 * @param params 输出参数
 */
void surgical_preset_bloodless(EnhancementParams* params);

#ifdef __cplusplus
}
#endif

#endif // SURGICAL_VIDEO_H
