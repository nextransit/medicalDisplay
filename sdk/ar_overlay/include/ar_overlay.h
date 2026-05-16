#ifndef AR_OVERLAY_H
#define AR_OVERLAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 标注类型
// ============================================================================
typedef enum {
    ANNOTATION_POINT = 0,     // 关键点标注
    ANNOTATION_LINE,           // 线条标注
    ANNOTATION_RECT,           // 矩形区域
    ANNOTATION_ELLIPSE,        // 椭圆区域
    ANNOTATION_POLYGON,        // 多边形
    ANNOTATION_TEXT,           // 文字标注
    ANNOTATION_MEASUREMENT,    // 测量标注
    ANNOTATION_ARROW,          // 箭头指向
    ANNOTATION_HIGHLIGHT,      // 高亮区域
    ANNOTATION_COUNT
} AnnotationType;

// ============================================================================
// 标注样式
// ============================================================================
typedef struct {
    uint8_t color_r;           // 红色 (0-255)
    uint8_t color_g;           // 绿色
    uint8_t color_b;          // 蓝色
    uint8_t color_a;           // 透明度
    float line_width;           // 线宽 (像素)
    float font_size;           // 字体大小
    bool fill;                 // 是否填充
    bool dashed;               // 是否虚线
    bool visible;              // 是否可见
} AnnotationStyle;

// ============================================================================
// 标注数据
// ============================================================================
typedef struct {
    uint32_t id;               // 标注ID
    AnnotationType type;       // 类型
    float x;                  // X坐标 (归一化 0-1)
    float y;                  // Y坐标
    float width;               // 宽度
    float height;              // 高度
    float rotation;            // 旋转角度 (度)
    char text[256];           // 文字内容
    float value;              // 测量值
    char unit[32];            // 单位
    AnnotationStyle style;      // 样式
    char label[128];          // 标签
    uint32_t z_order;         // Z轴顺序
    bool selected;             // 是否选中
    bool locked;               // 是否锁定
} AnnotationData;

// ============================================================================
// AR标注会话
// ============================================================================
typedef struct {
    uint32_t session_id;       // 会话ID
    char patient_id[64];       // 患者ID
    char study_id[64];        // 检查ID
    char series_id[64];       // 系列ID
    uint64_t created_time;    // 创建时间
    uint64_t modified_time;   // 修改时间
    uint32_t annotation_count; // 标注数量
} AnnotationSession;

// ============================================================================
// AR叠加引擎句柄
// ============================================================================
typedef struct AROverlayEngine AROverlayEngine;

// ============================================================================
// 引擎生命周期
// ============================================================================

/**
 * 创建AR叠加引擎
 * @param canvas_width 画布宽度
 * @param canvas_height 画布高度
 * @return 引擎句柄，失败返回NULL
 */
AROverlayEngine* ar_engine_create(int canvas_width, int canvas_height);

/**
 * 销毁AR叠加引擎
 * @param engine 引擎句柄
 */
void ar_engine_destroy(AROverlayEngine* engine);

// ============================================================================
// 标注管理
// ============================================================================

/**
 * 添加标注
 * @param engine 引擎句柄
 * @param annotation 标注数据
 * @return 标注ID，-1失败
 */
int ar_engine_add_annotation(AROverlayEngine* engine, const AnnotationData* annotation);

/**
 * 更新标注
 * @param engine 引擎句柄
 * @param annotation_id 标注ID
 * @param annotation 标注数据
 * @return 0成功，-1失败
 */
int ar_engine_update_annotation(AROverlayEngine* engine, uint32_t annotation_id,
                              const AnnotationData* annotation);

/**
 * 删除标注
 * @param engine 引擎句柄
 * @param annotation_id 标注ID
 * @return 0成功，-1失败
 */
int ar_engine_remove_annotation(AROverlayEngine* engine, uint32_t annotation_id);

/**
 * 获取标注
 * @param engine 引擎句柄
 * @param annotation_id 标注ID
 * @param annotation 输出数据
 * @return 0成功，-1失败
 */
int ar_engine_get_annotation(AROverlayEngine* engine, uint32_t annotation_id,
                            AnnotationData* annotation);

/**
 * 获取所有标注
 * @param engine 引擎句柄
 * @param annotations 输出数组
 * @param max_count 最大数量
 * @return 实际数量
 */
int ar_engine_get_all_annotations(AROverlayEngine* engine,
                                  AnnotationData* annotations,
                                  int max_count);

/**
 * 清除所有标注
 * @param engine 引擎句柄
 */
void ar_engine_clear_annotations(AROverlayEngine* engine);

// ============================================================================
// 标注会话
// ============================================================================

/**
 * 创建标注会话
 * @param engine 引擎句柄
 * @param session 输出会话信息
 * @return 0成功，-1失败
 */
int ar_engine_create_session(AROverlayEngine* engine, AnnotationSession* session);

/**
 * 加载标注会话
 * @param engine 引擎句柄
 * @param session_id 会话ID
 * @return 0成功，-1失败
 */
int ar_engine_load_session(AROverlayEngine* engine, uint32_t session_id);

/**
 * 保存标注会话
 * @param engine 引擎句柄
 * @param path 保存路径
 * @return 0成功，-1失败
 */
int ar_engine_save_session(AROverlayEngine* engine, const char* path);

/**
 * 导出标注为JSON
 * @param engine 引擎句柄
 * @param json_output 输出JSON
 * @param buffer_size 缓冲区大小
 * @return 0成功，-1失败
 */
int ar_engine_export_json(AROverlayEngine* engine, char* json_output, size_t buffer_size);

/**
 * 从JSON导入标注
 * @param engine 引擎句柄
 * @param json_input JSON字符串
 * @return 导入数量，-1失败
 */
int ar_engine_import_json(AROverlayEngine* engine, const char* json_input);

// ============================================================================
// 渲染
// ============================================================================

/**
 * 渲染AR叠加层
 * @param engine 引擎句柄
 * @param base_image 基础图像
 * @param output 输出图像
 * @return 0成功，-1失败
 */
int ar_engine_render(AROverlayEngine* engine,
                    const uint8_t* base_image,
                    uint8_t* output);

/**
 * 渲染到纹理 (GPU加速)
 * @param engine 引擎句柄
 * @param texture_id 纹理ID
 * @return 0成功，-1失败
 */
int ar_engine_render_to_texture(AROverlayEngine* engine, uint32_t texture_id);

// ============================================================================
// 交互
// ============================================================================

/**
 * 点击测试
 * @param engine 引擎句柄
 * @param x 点击X坐标
 * @param y 点击Y坐标
 * @return 命中的标注ID，0表示无命中
 */
uint32_t ar_engine_hit_test(AROverlayEngine* engine, float x, float y);

/**
 * 选择标注
 * @param engine 引擎句柄
 * @param annotation_id 标注ID
 * @param selected 是否选中
 * @return 0成功
 */
int ar_engine_select(AROverlayEngine* engine, uint32_t annotation_id, bool selected);

/**
 * 移动标注
 * @param engine 引擎句柄
 * @param annotation_id 标注ID
 * @param dx X偏移
 * @param dy Y偏移
 * @return 0成功
 */
int ar_engine_move(AROverlayEngine* engine, uint32_t annotation_id, float dx, float dy);

/**
 * 缩放标注
 * @param engine 引擎句柄
 * @param annotation_id 标注ID
 * @param scale 缩放因子
 * @param center_x 中心X
 * @param center_y 中心Y
 * @return 0成功
 */
int ar_engine_scale(AROverlayEngine* engine, uint32_t annotation_id,
                    float scale, float center_x, float center_y);

// ============================================================================
// 预设样式
// ============================================================================

/**
 * 获取关键解剖标注样式
 * @param style 输出样式
 */
void ar_get_anatomy_style(AnnotationStyle* style);

/**
 * 获取手术标注样式
 * @param style 输出样式
 */
void ar_get_surgical_style(AnnotationStyle* style);

/**
 * 获取测量标注样式
 * @param style 输出样式
 */
void ar_get_measurement_style(AnnotationStyle* style);

/**
 * 获取警告标注样式
 * @param style 输出样式
 */
void ar_get_warning_style(AnnotationStyle* style);

#ifdef __cplusplus
}
#endif

#endif // AR_OVERLAY_H
