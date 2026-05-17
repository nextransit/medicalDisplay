/**
 * @file v4l2_capture.h
 * @brief V4L2 实时视频采集接口
 * 
 * 支持: USB摄像头、HDMI采集卡、医疗内窥镜
 * 格式: YUV422/YUV420/RGB24
 */

#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 像素格式
// ============================================================================
typedef enum {
    V4L2_PIX_FMT_YUYV = 0x56595559,   // YUV 4:2:2
    V4L2_PIX_FMT_MJPEG = 0x47504A4D,  // Motion JPEG
    V4L2_PIX_FMT_H264 = 0x34363248,   // H.264
    V4L2_PIX_FMT_RGB24 = 0x33424752,  // RGB24
    V4L2_PIX_FMT_BGR24 = 0x33524742,  // BGR24
    V4L2_PIX_FMT_NV12 = 0x3231564E,   // NV12 YUV420
    V4L2_PIX_FMT_YUV420 = 0x32315559, // YUV420 planar
} V4L2PixelFormat;

// ============================================================================
// 帧格式描述
// ============================================================================
typedef struct {
    uint32_t width;
    uint32_t height;
    V4L2PixelFormat pixel_format;
    uint32_t bytes_per_line;
    uint32_t image_size;
    uint32_t framerate_numer;
    uint32_t framerate_denom;
} V4L2FrameFormat;

// ============================================================================
// 采集设备句柄
// ============================================================================
typedef struct V4L2Capture V4L2Capture;

// ============================================================================
// 设备能力查询
// ============================================================================

/**
 * 获取可用设备列表
 * @param devices 输出设备路径数组
 * @param max_devices 最大设备数
 * @return 实际设备数
 */
int v4l2_list_devices(char** devices, int max_devices);

/**
 * 获取设备信息
 */
typedef struct {
    char device_path[256];
    char device_name[256];
    uint32_t capabilities;  // V4L2_CAP_*
    uint32_t pixel_formats[16];
    int num_formats;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
} V4L2DeviceInfo;

int v4l2_get_device_info(const char* device_path, V4L2DeviceInfo* info);

// ============================================================================
// 设备生命周期
// ============================================================================

/**
 * 打开并初始化采集设备
 * @param device_path 设备路径 (如 /dev/video0)
 * @param width 期望宽度
 * @param height 期望高度
 * @param pixel_format 期望像素格式
 * @param framerate 期望帧率
 * @return 句柄，失败返回NULL
 */
V4L2Capture* v4l2_capture_open(const char* device_path,
                               uint32_t width,
                               uint32_t height,
                               V4L2PixelFormat pixel_format,
                               uint32_t framerate);

/**
 * 关闭设备
 */
void v4l2_capture_close(V4L2Capture* capture);

/**
 * 获取当前帧格式
 */
int v4l2_capture_get_format(V4L2Capture* capture, V4L2FrameFormat* format);

/**
 * 设置帧格式
 */
int v4l2_capture_set_format(V4L2Capture* capture,
                           uint32_t width,
                           uint32_t height,
                           V4L2PixelFormat pixel_format,
                           uint32_t framerate);

// ============================================================================
// 帧采集
// ============================================================================

/**
 * 采集单帧
 * @param capture 设备句柄
 * @param buffer 输出缓冲区 (大小 >= image_size)
 * @param timeout_ms 超时时间 (毫秒)
 * @return 0成功，-1失败，1超时
 */
int v4l2_capture_frame(V4L2Capture* capture,
                      uint8_t* buffer,
                      size_t buffer_size,
                      int timeout_ms);

/**
 * 启动连续采集模式
 */
int v4l2_capture_start(V4L2Capture* capture);

/**
 * 停止连续采集模式
 */
int v4l2_capture_stop(V4L2Capture* capture);

/**
 * 轮询新帧 (连续采集模式下使用)
 * @return 0有新帧，-1错误，1无帧
 */
int v4l2_capture_poll(V4L2Capture* capture, int timeout_ms);

/**
 * 获取最新帧 (连续采集模式下使用)
 */
int v4l2_capture_get_frame(V4L2Capture* capture,
                          uint8_t* buffer,
                          size_t buffer_size);

// ============================================================================
// 图像处理
// ============================================================================

/**
 * 转换帧格式
 */
int v4l2_convert_frame(const uint8_t* src,
                      uint8_t* dst,
                      const V4L2FrameFormat* src_format,
                      const V4L2FrameFormat* dst_format);

/**
 * 获取帧的RGB数据
 */
int v4l2_frame_to_rgb(V4L2Capture* capture,
                     const uint8_t* frame_data,
                     uint8_t* rgb_data,
                     size_t rgb_buffer_size);

// ============================================================================
// 设备控制
// ============================================================================

/**
 * 设置曝光
 * @param value 曝光值
 */
int v4l2_set_exposure(V4L2Capture* capture, int value);

/**
 * 设置增益
 * @param value 增益值
 */
int v4l2_set_gain(V4L2Capture* capture, int value);

/**
 * 设置亮度
 * @param value 亮度值 (0-100)
 */
int v4l2_set_brightness(V4L2Capture* capture, int value);

/**
 * 设置对比度
 */
int v4l2_set_contrast(V4L2Capture* capture, int value);

/**
 * 设置饱和度
 */
int v4l2_set_saturation(V4L2Capture* capture, int value);

/**
 * 设置白平衡模式
 * @param auto_mode 0=手动，1=自动
 */
int v4l2_set_white_balance(V4L2Capture* capture, int auto_mode, int value);

/**
 * 设置焦点
 * @param auto_focus 0=手动，1=自动
 */
int v4l2_set_focus(V4L2Capture* capture, int auto_focus, int value);

// ============================================================================
// 性能统计
// ============================================================================

typedef struct {
    uint64_t total_frames;
    uint64_t dropped_frames;
    uint64_t error_frames;
    double avg_latency_ms;
    double min_latency_ms;
    double max_latency_ms;
    uint32_t current_fps;
} V4L2Stats;

void v4l2_get_stats(V4L2Capture* capture, V4L2Stats* stats);
void v4l2_reset_stats(V4L2Capture* capture);

#ifdef __cplusplus
}
#endif

#endif // V4L2_CAPTURE_H
