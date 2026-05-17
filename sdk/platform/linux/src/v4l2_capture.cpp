/**
 * @file v4l2_capture.cpp
 * @brief V4L2 实时视频采集实现
 */

#include "v4l2_capture.h"
#include "simd_processing.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <poll.h>
#include <algorithm>

// ============================================================================
// 内部结构
// ============================================================================

struct V4L2Capture {
    int fd;
    char device_path[256];
    
    // 帧格式
    V4L2FrameFormat format;
    
    // MMAP缓冲
    void* buffers[8];
    size_t buffer_sizes[8];
    int num_buffers;
    int current_buffer;
    
    // 连续采集
    bool streaming;
    int poll_fd;
    
    // 统计
    V4L2Stats stats;
    
    // 临时缓冲
    uint8_t* conversion_buffer;
    size_t conversion_buffer_size;
};

static constexpr uint32_t MAX_DEVICES = 16;

// ============================================================================
// 工具函数
// ============================================================================

static uint32_t v4l2_fourcc(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
}

static const char* fourcc_to_string(uint32_t fourcc) {
    static char buf[5];
    buf[0] = fourcc & 0xFF;
    buf[1] = (fourcc >> 8) & 0xFF;
    buf[2] = (fourcc >> 16) & 0xFF;
    buf[3] = (fourcc >> 24) & 0xFF;
    buf[4] = '\0';
    return buf;
}

static V4L2PixelFormat v4l2_fmt_to_pixel_fmt(uint32_t v4l2_fmt) {
    switch (v4l2_fmt) {
        case V4L2_PIX_FMT_YUYV: return V4L2_PIX_FMT_YUYV;
        case V4L2_PIX_FMT_MJPEG: return V4L2_PIX_FMT_MJPEG;
        case V4L2_PIX_FMT_RGB24: return V4L2_PIX_FMT_RGB24;
        case V4L2_PIX_FMT_BGR24: return V4L2_PIX_FMT_BGR24;
        case V4L2_PIX_FMT_NV12: return V4L2_PIX_FMT_NV12;
        case V4L2_PIX_FMT_YUV420: return V4L2_PIX_FMT_YUV420;
        default: return V4L2_PIX_FMT_YUYV;
    }
}

static uint32_t pixel_fmt_to_v4l2(V4L2PixelFormat fmt) {
    return (uint32_t)fmt;
}

// ============================================================================
// 设备查询
// ============================================================================

int v4l2_list_devices(char** devices, int max_devices) {
    int count = 0;
    char path[64];
    
    for (int i = 0; i < MAX_DEVICES && count < max_devices; i++) {
        snprintf(path, sizeof(path), "/dev/video%d", i);
        
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        
        struct v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
                devices[count] = strdup(path);
                count++;
            }
        }
        close(fd);
    }
    
    return count;
}

int v4l2_get_device_info(const char* device_path, V4L2DeviceInfo* info) {
    if (!device_path || !info) return -1;
    
    int fd = open(device_path, O_RDWR);
    if (fd < 0) return -1;
    
    memset(info, 0, sizeof(*info));
    strncpy(info->device_path, device_path, sizeof(info->device_path) - 1);
    
    // 查询能力
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        info->capabilities = cap.capabilities;
        strncpy(info->device_name, (char*)cap.card, sizeof(info->device_name) - 1);
    }
    
    // 枚举格式
    struct v4l2_fmtdesc fmtdesc = {};
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0 && info->num_formats < 16) {
        info->pixel_formats[info->num_formats] = fmtdesc.pixelformat;
        info->num_formats++;
        fmtdesc.index++;
    }
    
    // 获取尺寸范围
    struct v4l2_frmsizeenum frmsize = {};
    frmsize.pixel_format = info->pixel_formats[0];
    
    if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
        if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
            info->min_width = frmsize.stepwise.min_width;
            info->max_width = frmsize.stepwise.max_width;
            info->min_height = frmsize.stepwise.min_height;
            info->max_height = frmsize.stepwise.max_height;
        }
    }
    
    close(fd);
    return 0;
}

// ============================================================================
// 设备生命周期
// ============================================================================

V4L2Capture* v4l2_capture_open(const char* device_path,
                               uint32_t width,
                               uint32_t height,
                               V4L2PixelFormat pixel_format,
                               uint32_t framerate) {
    if (!device_path) return nullptr;
    
    int fd = open(device_path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[V4L2] Cannot open %s: %s\n", device_path, strerror(errno));
        return nullptr;
    }
    
    // 检查能力
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "[V4L2] VIDIOC_QUERYCAP failed\n");
        close(fd);
        return nullptr;
    }
    
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "[V4L2] Not a video capture device\n");
        close(fd);
        return nullptr;
    }
    
    auto* capture = new (std::nothrow) V4L2Capture();
    if (!capture) {
        close(fd);
        return nullptr;
    }
    
    memset(capture, 0, sizeof(*capture));
    capture->fd = fd;
    strncpy(capture->device_path, device_path, sizeof(capture->device_path) - 1);
    
    // 设置格式
    if (v4l2_capture_set_format(capture, width, height, pixel_format, framerate) < 0) {
        delete capture;
        return nullptr;
    }
    
    // 初始化MMAP
    struct v4l2_requestbuffers req = {};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        fprintf(stderr, "[V4L2] VIDIOC_REQBUFS failed: %s\n", strerror(errno));
        delete capture;
        return nullptr;
    }
    
    capture->num_buffers = req.count;
    
    for (int i = 0; i < capture->num_buffers; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "[V4L2] VIDIOC_QUERYBUF failed\n");
            delete capture;
            return nullptr;
        }
        
        capture->buffer_sizes[i] = buf.length;
        capture->buffers[i] = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, buf.m.offset);
        
        if (capture->buffers[i] == MAP_FAILED) {
            fprintf(stderr, "[V4L2] mmap failed\n");
            delete capture;
            return nullptr;
        }
    }
    
    // 初始化轮询
    capture->poll_fd = fd;
    
    fprintf(stderr, "[V4L2] Opened %s (%dx%d %s @ %dfps)\n",
            device_path, width, height, 
            fourcc_to_string(pixel_fmt_to_v4l2(pixel_format)),
            framerate);
    
    return capture;
}

void v4l2_capture_close(V4L2Capture* capture) {
    if (!capture) return;
    
    if (capture->streaming) {
        v4l2_capture_stop(capture);
    }
    
    // 取消MMAP
    for (int i = 0; i < capture->num_buffers; i++) {
        if (capture->buffers[i] && capture->buffers[i] != MAP_FAILED) {
            munmap(capture->buffers[i], capture->buffer_sizes[i]);
        }
    }
    
    // 关闭设备
    close(capture->fd);
    
    delete[] capture->conversion_buffer;
    delete capture;
}

int v4l2_capture_get_format(V4L2Capture* capture, V4L2FrameFormat* format) {
    if (!capture || !format) return -1;
    *format = capture->format;
    return 0;
}

int v4l2_capture_set_format(V4L2Capture* capture,
                           uint32_t width,
                           uint32_t height,
                           V4L2PixelFormat pixel_format,
                           uint32_t framerate) {
    if (!capture) return -1;
    
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixel_fmt_to_v4l2(pixel_format);
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    
    if (ioctl(capture->fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "[V4L2] VIDIOC_S_FMT failed: %s\n", strerror(errno));
        return -1;
    }
    
    capture->format.width = fmt.fmt.pix.width;
    capture->format.height = fmt.fmt.pix.height;
    capture->format.pixel_format = v4l2_fmt_to_pixel_fmt(fmt.fmt.pix.pixelformat);
    capture->format.bytes_per_line = fmt.fmt.pix.bytesperline;
    capture->format.image_size = fmt.fmt.pix.sizeimage;
    capture->format.framerate_numer = framerate;
    capture->format.framerate_denom = 1;
    
    // 设置帧率
    struct v4l2_streamparm parm = {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = framerate;
    ioctl(capture->fd, VIDIOC_S_PARM, &parm);
    
    return 0;
}

// ============================================================================
// 帧采集
// ============================================================================

int v4l2_capture_start(V4L2Capture* capture) {
    if (!capture || capture->streaming) return -1;
    
    // 入队所有缓冲
    for (int i = 0; i < capture->num_buffers; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(capture->fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "[V4L2] VIDIOC_QBUF failed\n");
            return -1;
        }
    }
    
    // 启动采集
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(capture->fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "[V4L2] VIDIOC_STREAMON failed\n");
        return -1;
    }
    
    capture->streaming = true;
    capture->current_buffer = -1;
    
    memset(&capture->stats, 0, sizeof(capture->stats));
    
    return 0;
}

int v4l2_capture_stop(V4L2Capture* capture) {
    if (!capture || !capture->streaming) return -1;
    
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(capture->fd, VIDIOC_STREAMOFF, &type) < 0) {
        return -1;
    }
    
    capture->streaming = false;
    return 0;
}

int v4l2_capture_frame(V4L2Capture* capture,
                      uint8_t* buffer,
                      size_t buffer_size,
                      int timeout_ms) {
    if (!capture || !buffer) return -1;
    
    if (!capture->streaming) {
        if (v4l2_capture_start(capture) < 0) return -1;
    }
    
    struct pollfd pfd = {};
    pfd.fd = capture->fd;
    pfd.events = POLLIN;
    
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) return -1;
    if (ret == 0) return 1;  // 超时
    
    return v4l2_capture_get_frame(capture, buffer, buffer_size);
}

int v4l2_capture_poll(V4L2Capture* capture, int timeout_ms) {
    if (!capture) return -1;
    
    struct pollfd pfd = {};
    pfd.fd = capture->fd;
    pfd.events = POLLIN;
    
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) return -1;
    if (ret == 0) return 1;
    
    return 0;
}

int v4l2_capture_get_frame(V4L2Capture* capture,
                          uint8_t* buffer,
                          size_t buffer_size) {
    if (!capture || !buffer) return -1;
    
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(capture->fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) return 1;
        return -1;
    }
    
    capture->current_buffer = buf.index;
    
    // 复制数据
    size_t copy_size = std::min((size_t)buf.bytesused, buffer_size);
    memcpy(buffer, capture->buffers[buf.index], copy_size);
    
    // 重新入队
    if (ioctl(capture->fd, VIDIOC_QBUF, &buf) < 0) {
        return -1;
    }
    
    capture->stats.total_frames++;
    
    return 0;
}

// ============================================================================
// 格式转换
// ============================================================================

int v4l2_frame_to_rgb(V4L2Capture* capture,
                     const uint8_t* frame_data,
                     uint8_t* rgb_data,
                     size_t rgb_buffer_size) {
    if (!capture || !frame_data || !rgb_data) return -1;
    
    V4L2FrameFormat dst_format = capture->format;
    dst_format.pixel_format = V4L2_PIX_FMT_RGB24;
    dst_format.bytes_per_line = capture->format.width * 3;
    dst_format.image_size = capture->format.width * capture->format.height * 3;
    
    V4L2FrameFormat src_format = capture->format;
    
    return v4l2_convert_frame(frame_data, rgb_data, &src_format, &dst_format);
}

int v4l2_convert_frame(const uint8_t* src,
                      uint8_t* dst,
                      const V4L2FrameFormat* src_format,
                      const V4L2FrameFormat* dst_format) {
    if (!src || !dst || !src_format || !dst_format) return -1;
    
    // YUYV -> RGB24
    if (src_format->pixel_format == V4L2_PIX_FMT_YUYV &&
        dst_format->pixel_format == V4L2_PIX_FMT_RGB24) {
        
        int width = std::min(src_format->width, dst_format->width);
        int height = std::min(src_format->height, dst_format->height);
        
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x += 2) {
                int src_idx = y * src_format->bytes_per_line + x * 2;
                int y0 = src[src_idx];
                int y1 = src[src_idx + 2];
                int u = src[src_idx + 1] - 128;
                int v = src[src_idx + 3] - 128;
                
                // YUV to RGB
                auto yuv2rgb = [](int y, int u, int v) -> uint8_t {
                    int r = y + (v * 1436) / 1024;
                    int g = y - (u * 352) / 1024 - (v * 731) / 1024;
                    int b = y + (u * 1814) / 1024;
                    return (r < 0 ? 0 : r > 255 ? 255 : r);
                };
                
                int dst_idx = (y * width + x) * 3;
                dst[dst_idx] = yuv2rgb(y0, u, v);
                dst[dst_idx + 1] = yuv2rgb(y0, u, v);
                dst[dst_idx + 2] = yuv2rgb(y0, u, v);
                
                if (x + 1 < width) {
                    int dst_idx2 = (y * width + x + 1) * 3;
                    dst[dst_idx2] = yuv2rgb(y1, u, v);
                    dst[dst_idx2 + 1] = yuv2rgb(y1, u, v);
                    dst[dst_idx2 + 2] = yuv2rgb(y1, u, v);
                }
            }
        }
        return 0;
    }
    
    // 直接复制 (相同格式)
    if (src_format->pixel_format == dst_format->pixel_format &&
        src_format->width == dst_format->width &&
        src_format->height == dst_format->height) {
        size_t copy_size = std::min(src_format->image_size, dst_format->image_size);
        memcpy(dst, src, copy_size);
        return 0;
    }
    
    return -1;
}

// ============================================================================
// 设备控制
// ============================================================================

static int v4l2_set_ctrl(V4L2Capture* capture, int id, int value) {
    if (!capture) return -1;
    
    struct v4l2_control ctrl = {};
    ctrl.id = id;
    ctrl.value = value;
    
    return ioctl(capture->fd, VIDIOC_S_CTRL, &ctrl);
}

int v4l2_set_exposure(V4L2Capture* capture, int value) {
    return v4l2_set_ctrl(capture, V4L2_CID_EXPOSURE, value);
}

int v4l2_set_gain(V4L2Capture* capture, int value) {
    return v4l2_set_ctrl(capture, V4L2_CID_GAIN, value);
}

int v4l2_set_brightness(V4L2Capture* capture, int value) {
    return v4l2_set_ctrl(capture, V4L2_CID_BRIGHTNESS, value);
}

int v4l2_set_contrast(V4L2Capture* capture, int value) {
    return v4l2_set_ctrl(capture, V4L2_CID_CONTRAST, value);
}

int v4l2_set_saturation(V4L2Capture* capture, int value) {
    return v4l2_set_ctrl(capture, V4L2_CID_SATURATION, value);
}

int v4l2_set_white_balance(V4L2Capture* capture, int auto_mode, int value) {
    if (!capture) return -1;
    
    if (auto_mode) {
        v4l2_set_ctrl(capture, V4L2_CID_AUTO_WHITE_BALANCE, 1);
    } else {
        v4l2_set_ctrl(capture, V4L2_CID_AUTO_WHITE_BALANCE, 0);
        v4l2_set_ctrl(capture, V4L2_CID_WHITE_BALANCE_TEMPERATURE, value);
    }
    return 0;
}

int v4l2_set_focus(V4L2Capture* capture, int auto_focus, int value) {
    if (!capture) return -1;
    
    if (auto_focus) {
        v4l2_set_ctrl(capture, V4L2_CID_AUTOFOCUS, 1);
    } else {
        v4l2_set_ctrl(capture, V4L2_CID_AUTOFOCUS, 0);
        v4l2_set_ctrl(capture, V4L2_CID_FOCUS_ABSOLUTE, value);
    }
    return 0;
}

// ============================================================================
// 统计
// ============================================================================

void v4l2_get_stats(V4L2Capture* capture, V4L2Stats* stats) {
    if (!capture || !stats) return;
    *stats = capture->stats;
}

void v4l2_reset_stats(V4L2Capture* capture) {
    if (!capture) return;
    memset(&capture->stats, 0, sizeof(capture->stats));
}
