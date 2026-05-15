#ifndef LINUX_DISPLAY_H
#define LINUX_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DRM文件描述符
// ============================================================================
typedef int drm_fd_t;
#define DRM_FD_INVALID -1

// ============================================================================
// 显示设备信息
// ============================================================================
typedef struct {
    int         connector_id;      // DRM连接器ID
    char        name[64];         // 显示器名称
    int         width_mm;         // 物理宽度 (mm)
    int         height_mm;        // 物理高度 (mm)
    int         subpixel;         // 子像素排列
    int         connection;       // 连接状态
    uint32_t    encoder_id;        // 编码器ID
    uint32_t    crtc_id;          // CRTC ID
    int         possible_crtcs;   // 可能的CRTC掩码
} DRMConnectorInfo;

typedef struct {
    uint32_t    crtc_id;          // CRTC ID
    int         x, y;             // 位置
    int         width, height;     // 分辨率
    int         refresh;           // 刷新率 (mHz)
    uint32_t    fb_id;            // Framebuffer ID
    uint32_t    gamma_size;       // Gamma LUT大小
} DRMCrtcInfo;

typedef struct {
    int                 fd;                 // DRM文件描述符
    int                 num_connectors;      // 连接器数量
    DRMConnectorInfo*   connectors;         // 连接器列表
    DRMCrtcInfo*        crtcs;              // CRTC列表
    uint32_t            capabilities;        // DRM能力
    int                 min_width, max_width;
    int                 min_height, max_height;
} DRMDeviceInfo;

// ============================================================================
// 色彩管理
// ============================================================================
typedef struct {
    int     crtc_id;              // 目标CRTC
    int     lut_size;             // LUT大小
    float*  red;                  // 红色通道LUT
    float*  green;                // 绿色通道LUT
    float*  blue;                 // 蓝色通道LUT
} DRMGammaLUT;

typedef enum {
    DRM_COLORSPACE_DEFAULT = 0,
    DRM_COLORSPACE_sRGB,
    DRM_COLORSPACE_BT2020,
    DRM_COLORSPACE_DCI_P3,
    DRM_COLORSPACE_SCRGB,
    DRM_COLORSPACE_AdobeRGB,
    DRM_COLORSPACE_DICOM_GSDF
} DRMColorspace;

// ============================================================================
// 平面 (Plane) - 用于Overlay
// ============================================================================
typedef struct {
    uint32_t    plane_id;
    int         possible_crtcs;
    uint64_t    formats;          // 支持的格式掩码
    int         zpos_min, zpos_max;
    int         width, height;
    int         type;              // 0=Overlay, 1=Primary, 2=Cursor
} DRMPlaneInfo;

// ============================================================================
// DRM设备操作
// ============================================================================

/**
 * 打开DRM设备
 * @param device_path 设备路径 (如"/dev/dri/card0")，NULL则自动查找
 * @return 设备信息，失败返回NULL
 */
DRMDeviceInfo* drm_device_open(const char* device_path);

/**
 * 关闭DRM设备
 * @param device 设备信息
 */
void drm_device_close(DRMDeviceInfo* device);

/**
 * 获取设备能力
 * @param device 设备信息
 * @param capability 能力类型
 * @return 能力值
 */
uint64_t drm_get_capability(DRMDeviceInfo* device, uint64_t capability);

/**
 * 获取连接器列表
 * @param device 设备信息
 * @param connectors 输出数组
 * @param max_count 最大数量
 * @return 实际数量
 */
int drm_get_connectors(DRMDeviceInfo* device, DRMConnectorInfo* connectors, int max_count);

/**
 * 获取CRTC列表
 * @param device 设备信息
 * @param crtcs 输出数组
 * @param max_count 最大数量
 * @return 实际数量
 */
int drm_get_crtcs(DRMDeviceInfo* device, DRMCrtcInfo* crtcs, int max_count);

/**
 * 获取Plane列表
 * @param device 设备信息
 * @param planes 输出数组
 * @param max_count 最大数量
 * @return 实际数量
 */
int drm_get_planes(DRMDeviceInfo* device, DRMPlaneInfo* planes, int max_count);

// ============================================================================
// 显示模式
// ============================================================================

/**
 * 设置显示模式
 * @param device 设备信息
 * @param connector_id 连接器ID
 * @param width 分辨率宽
 * @param height 分辨率高
 * @param refresh 刷新率 (Hz)
 * @return 0成功
 */
int drm_set_mode(DRMDeviceInfo* device, int connector_id, int width, int height, int refresh);

/**
 * 获取当前模式
 * @param device 设备信息
 * @param crtc_id CRTC ID
 * @param mode 输出模式
 * @return 0成功
 */
int drm_get_mode(DRMDeviceInfo* device, uint32_t crtc_id, DRMConnectorInfo* mode);

// ============================================================================
// Gamma/色彩管理
// ============================================================================

/**
 * 设置Gamma LUT
 * @param device 设备信息
 * @param crtc_id CRTC ID
 * @param gamma Gamma配置
 * @return 0成功
 */
int drm_set_gamma(DRMDeviceInfo* device, uint32_t crtc_id, const DRMGammaLUT* gamma);

/**
 * 获取Gamma LUT大小
 * @param device 设备信息
 * @param crtc_id CRTC ID
 * @return LUT大小，-1失败
 */
int drm_get_gamma_size(DRMDeviceInfo* device, uint32_t crtc_id);

/**
 * 设置色彩空间
 * @param device 设备信息
 * @param crtc_id CRTC ID
 * @param colorspace 色彩空间
 * @return 0成功
 */
int drm_set_colorspace(DRMDeviceInfo* device, uint32_t crtc_id, DRMColorspace colorspace);

// ============================================================================
// Framebuffer管理
// ============================================================================

/**
 * 创建Framebuffer
 * @param device 设备信息
 * @param width 宽度
 * @param height 高度
 * @param format 格式 (DRM_FORMAT_*)
 * @param handles 句柄数组
 * @param strides 步长数组
 * @param offsets 偏移数组
 * @param fb_id 输出FB ID
 * @return 0成功
 */
int drm_create_fb(DRMDeviceInfo* device, int width, int height, uint32_t format,
                  const uint32_t* handles, const uint32_t* strides,
                  const uint32_t* offsets, uint32_t* fb_id);

/**
 * 销毁Framebuffer
 * @param device 设备信息
 * @param fb_id FB ID
 * @return 0成功
 */
int drm_destroy_fb(DRMDeviceInfo* device, uint32_t fb_id);

/**
 * 创建DMA-BUF FB
 * @param device 设备信息
 * @param width 宽度
 * @param height 高度
 * @param format 格式
 * @param fd DMA-BUF文件描述符
 * @param fb_id 输出FB ID
 * @return 0成功
 */
int drm_create_fb_from_dmabuf(DRMDeviceInfo* device, int width, int height,
                              uint32_t format, int dmabuf_fd, uint32_t* fb_id);

// ============================================================================
// 原子操作
// ============================================================================

/**
 * 创建原子请求
 * @param device 设备信息
 * @return 请求句柄，失败返回0
 */
uint32_t drm_atomic_begin(DRMDeviceInfo* device);

/**
 * 添加属性
 * @param device 设备信息
 * @param req 原子请求
 * @param obj_id 对象ID
 * @param obj_type 对象类型
 * @param prop_id 属性ID
 * @param value 属性值
 * @return 0成功
 */
int drm_atomic_add_property(uint32_t req, uint32_t obj_id, uint32_t obj_type,
                            uint32_t prop_id, uint64_t value);

/**
 * 提交原子请求
 * @param device 设备信息
 * @param req 原子请求
 * @param flags 标志
 * @return 0成功
 */
int drm_atomic_commit(DRMDeviceInfo* device, uint32_t req, uint32_t flags);

/**
 * 销毁原子请求
 * @param device 设备信息
 * @param req 原子请求
 */
void drm_atomic_destroy(uint32_t req);

// ============================================================================
// VSYNC同步
// ============================================================================

/**
 * 等待VSYNC
 * @param device 设备信息
 * @param crtc_id CRTC ID
 * @return 0成功
 */
int drm_wait_vsync(DRMDeviceInfo* device, uint32_t crtc_id);

/**
 * 订阅页面翻转事件
 * @param device 设备信息
 * @param crtc_id CRTC ID
 * @return 0成功
 */
int drm_page_flipubscribe(DRMDeviceInfo* device, uint32_t crtc_id);

/**
 * 获取翻转事件FD
 * @param device 设备信息
 * @return FD，-1失败
 */
int drm_get_event_fd(DRMDeviceInfo* device);

/**
 * 处理翻转事件
 * @param device 设备信息
 * @param event_data 事件数据
 * @return 0成功
 */
int drm_handle flip_event(DRMDeviceInfo* device, void* event_data);

#ifdef __cplusplus
}
#endif

#endif // LINUX_DISPLAY_H
