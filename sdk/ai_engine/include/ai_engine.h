#ifndef AI_ENGINE_H
#define AI_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 影像模态枚举
// ============================================================================
typedef enum {
    MODALITY_UNKNOWN = 0,
    MODALITY_CT,           // 计算机断层扫描
    MODALITY_MR,           // 磁共振成像
    MODALITY_DX,           // 数字X射线
    MODALITY_CR,           // 计算机X射线
    MODALITY_US,           // 超声
    MODALITY_ES,           // 内窥镜
    MODALITY_SM,           // 数字病理 (Slidemicroscopy)
    MODALITY_PT,           // PET
    MODALITY_XA,           // X射线血管造影
    MODALITY_RF,           // 放射透视
    MODALITY_OP,           // 眼科摄影
    MODALITY_SURGICAL,     // 术野视频
    MODALITY_COUNT
} ModalityType;

// ============================================================================
// 显示策略配置
// ============================================================================
typedef struct {
    float gamma;                    // Gamma值 (1.8 - 2.6)
    int   color_space;             // 色彩空间 (0=sRGB, 1=DCI-P3, 2=Rec2020, 3=Native)
    int   gsdf_mode;               // GSDF模式 (0=关闭, 1=DICOM, 2=Custom)
    float window_center;           // 窗口中心 (CT HU值)
    float window_width;            // 窗口宽度
    int   local_enhance;          // 局部增强类型 (0=关闭, 1=bone, 2=lung, 3=vascular, 4=cell)
    float sharpness;               // 锐化强度 (0.0 - 2.0)
    float contrast;                // 对比度 (0.0 - 2.0)
    bool  hdr_enabled;             // HDR启用
    int   hdr_mode;               // HDR模式 (0=HDR10, 1=HLG, 2=Local)
} DisplayStrategy;

// ============================================================================
// AI识别结果
// ============================================================================
typedef struct {
    ModalityType    modality;          // 识别到的模态
    float           confidence;         // 置信度 (0.0 - 1.0)
    DisplayStrategy strategy;           // 推荐显示策略
    float           inference_time_ms; // 推理耗时 (毫秒)
    int             body_part;         // 检查部位 (0=Unknown, 1=Head, 2=Chest, etc.)
} AIRecognitionResult;

// ============================================================================
// AI引擎配置
// ============================================================================
typedef struct {
    // 模型配置
    char        model_path[512];        // 模型文件路径
    int         num_threads;           // CPU线程数 (0=自动)
    bool        use_npu;               // 是否使用NPU加速
    bool        use_gpu;               // 是否使用GPU加速
    
    // 推理配置
    int         max_batch_size;         // 最大批处理大小
    int         input_width;           // 输入图像宽度
    int         input_height;          // 输入图像高度
    float       score_threshold;       // 置信度阈值
    
    // 平台特定配置
    union {
        struct {
            int device_id;             // NPU设备ID
            int dsp_id;               // DSP ID (可选)
        } rk3588;
        struct {
            int gpu_id;               // NVIDIA GPU ID
            int tensorrt_precision;    // 0=FP32, 1=FP16, 2=INT8
        } nvidia;
        struct {
            int device_id;            // Intel GPU device
        } intel;
    } platform;
} AIEngineConfig;

// ============================================================================
// AI引擎句柄
// ============================================================================
typedef struct AIEngine AIEngine;

// ============================================================================
// 引擎生命周期
// ============================================================================

/**
 * 创建AI引擎实例
 * @param config 引擎配置
 * @return 引擎句柄，失败返回NULL
 */
AIEngine* ai_engine_create(const AIEngineConfig* config);

/**
 * 销毁AI引擎实例
 * @param engine 引擎句柄
 */
void ai_engine_destroy(AIEngine* engine);

/**
 * 重新加载模型 (用于OTA更新)
 * @param engine 引擎句柄
 * @param model_path 新模型路径
 * @return 0成功，-1失败
 */
int ai_engine_reload_model(AIEngine* engine, const char* model_path);

// ============================================================================
// 推理接口
// ============================================================================

/**
 * 从DICOM数据识别影像模态
 * @param engine 引擎句柄
 * @param dicom_data DICOM像素数据
 * @param width 图像宽度
 * @param height 图像高度
 * @param bits_allocated 位深
 * @param result 输出结果
 * @return 0成功，-1失败
 */
int ai_engine_recognize_from_dicom(AIEngine* engine,
                                    const uint16_t* dicom_data,
                                    int width, int height, int bits_allocated,
                                    AIRecognitionResult* result);

/**
 * 从原始图像识别影像模态
 * @param engine 引擎句柄
 * @param image_data 图像数据
 * @param width 图像宽度
 * @param height 图像高度
 * @param channels 通道数 (1=灰度, 3=RGB)
 * @param result 输出结果
 * @return 0成功，-1失败
 */
int ai_engine_recognize_from_image(AIEngine* engine,
                                   const uint8_t* image_data,
                                   int width, int height, int channels,
                                   AIRecognitionResult* result);

/**
 * 从元数据快速识别 (不执行AI推理)
 * @param engine 引擎句柄
 * @param modality_tag DICOM Modality标签
 * @param series_desc 系列描述
 * @param body_part 部位
 * @param result 输出结果
 * @return 0成功，-1失败
 */
int ai_engine_recognize_from_metadata(AIEngine* engine,
                                       const char* modality_tag,
                                       const char* series_desc,
                                       int body_part,
                                       AIRecognitionResult* result);

// ============================================================================
// 批量处理
// ============================================================================

/**
 * 批量识别 (用于视频流处理)
 * @param engine 引擎句柄
 * @param frames 帧数据数组
 * @param frame_count 帧数量
 * @param results 结果数组 (需预分配)
 * @return 成功处理的帧数，-1失败
 */
int ai_engine_recognize_batch(AIEngine* engine,
                               const uint8_t** frames,
                               int frame_count,
                               AIRecognitionResult* results);

// ============================================================================
// 状态查询
// ============================================================================

/**
 * 获取引擎统计信息
 * @param engine 引擎句柄
 * @param total_inferences 总推理次数
 * @param avg_latency_ms 平均延迟
 */
void ai_engine_get_stats(AIEngine* engine, uint64_t* total_inferences, float* avg_latency_ms);

/**
 * 重置统计信息
 * @param engine 引擎句柄
 */
void ai_engine_reset_stats(AIEngine* engine);

#ifdef __cplusplus
}
#endif

#endif // AI_ENGINE_H
