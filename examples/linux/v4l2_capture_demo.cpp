/**
 * @file v4l2_capture_demo.cpp
 * @brief V4L2 视频采集演示程序
 * 
 * 用于测试USB摄像头、HDMI采集卡、医疗内窥镜等V4L2设备
 */

#include "v4l2_capture.h"
#include "simd_processing.h"
#include "surgical_video.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <getopt.h>

// ============================================================================
// 全局状态
// ============================================================================

static volatile bool g_running = true;

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

// ============================================================================
// 帮助信息
// ============================================================================

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\nOptions:\n");
    printf("  -d, --device PATH    V4L2 device path (default: /dev/video0)\n");
    printf("  -w, --width W       Frame width (default: 640)\n");
    printf("  -h, --height H      Frame height (default: 480)\n");
    printf("  -f, --fps FPS       Frames per second (default: 30)\n");
    printf("  -c, --count N       Number of frames to capture (default: 0 = unlimited)\n");
    printf("  -o, --output DIR    Output directory (default: ./output)\n");
    printf("  -b, --brightness V  Brightness adjustment (-1.0 to 1.0)\n");
    printf("  -s, --saturation V  Saturation adjustment (0.0 to 2.0)\n");
    printf("  -e, --enhance       Enable surgical enhancement\n");
    printf("  -l, --list          List available V4L2 devices\n");
    printf("  -v, --verbose       Verbose output\n");
    printf("      --help          Show this help\n");
    printf("\nExample:\n");
    printf("  %s -d /dev/video1 -w 1920 -H 1080 -f 60 -c 300\n", prog);
    printf("\n");
}

// ============================================================================
// 设备列表
// ============================================================================

static void list_devices(void) {
    printf("Available V4L2 devices:\n\n");
    
    char* devices[16];
    int count = v4l2_list_devices(devices, 16);
    
    if (count <= 0) {
        printf("  No V4L2 devices found.\n");
        return;
    }
    
    for (int i = 0; i < count; i++) {
        V4L2DeviceInfo info;
        if (v4l2_get_device_info(devices[i], &info) == 0) {
            printf("  Device: %s\n", devices[i]);
            printf("    Name: %s\n", info.device_name);
            printf("    Capabilities: 0x%08X\n", info.capabilities);
            printf("    Supported formats:\n");
            
            for (int j = 0; j < info.num_formats; j++) {
                uint32_t fmt = info.pixel_formats[j];
                printf("      - %c%c%c%c (0x%08X)\n",
                       fmt & 0xFF,
                       (fmt >> 8) & 0xFF,
                       (fmt >> 16) & 0xFF,
                       (fmt >> 24) & 0xFF,
                       fmt);
            }
            
            if (info.max_width > 0) {
                printf("    Resolution: %ux%u to %ux%u\n",
                       info.min_width, info.min_height,
                       info.max_width, info.max_height);
            }
            printf("\n");
        }
        
        free(devices[i]);
    }
}

// ============================================================================
// 主处理循环
// ============================================================================

static int process_loop(V4L2Capture* capture,
                       const char* output_dir,
                       bool enable_enhancement,
                       float brightness,
                       float saturation,
                       int max_frames,
                       bool verbose) {
    // 创建输出目录
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", output_dir);
    system(cmd);
    
    // 外科增强引擎
    SurgicalVideoEngine* engine = nullptr;
    if (enable_enhancement) {
        engine = surgical_engine_create(SURGICAL_LAPAROSCOPIC, false);
        if (!engine) {
            fprintf(stderr, "Failed to create surgical engine\n");
        } else {
            printf("Surgical enhancement enabled\n");
        }
    }
    
    // 分配帧缓冲
    V4L2FrameFormat fmt;
    v4l2_capture_get_format(capture, &fmt);
    
    size_t frame_size = fmt.image_size;
    std::vector<uint8_t> frame_buffer(frame_size);
    std::vector<uint8_t> rgb_buffer(fmt.width * fmt.height * 3);
    std::vector<uint8_t> output_buffer(fmt.width * fmt.height * 3);
    
    // 统计
    int64_t total_bytes = 0;
    int64_t frame_count = 0;
    double total_latency_ms = 0;
    
    printf("\nStarting capture: %dx%d @ %dfps\n",
           fmt.width, fmt.height, fmt.framerate_numer);
    printf("Press Ctrl+C to stop\n\n");
    
    // 启动采集
    if (v4l2_capture_start(capture) < 0) {
        fprintf(stderr, "Failed to start capture\n");
        return -1;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    while (g_running) {
        // 轮询新帧
        int ret = v4l2_capture_poll(capture, 1000);
        if (ret < 0) {
            fprintf(stderr, "Poll error\n");
            break;
        }
        if (ret > 0) {
            continue;  // 无帧
        }
        
        // 获取帧
        ret = v4l2_capture_get_frame(capture, frame_buffer.data(), frame_size);
        if (ret < 0) {
            fprintf(stderr, "Failed to get frame\n");
            continue;
        }
        
        auto frame_start = std::chrono::high_resolution_clock::now();
        
        // 转换为RGB
        if (fmt.pixel_format == V4L2_PIX_FMT_YUYV) {
            // YUYV -> RGB
            int idx = 0;
            for (int y = 0; y < fmt.height; y++) {
                for (int x = 0; x < fmt.width; x += 2) {
                    int src_idx = y * fmt.bytes_per_line + x * 2;
                    int y0 = frame_buffer[src_idx];
                    int y1 = frame_buffer[src_idx + 2];
                    int u = frame_buffer[src_idx + 1] - 128;
                    int v = frame_buffer[src_idx + 3] - 128;
                    
                    auto yuv2rgb = [](int y, int u, int v) -> uint8_t {
                        int r = y + (v * 1436) / 1024;
                        int g = y - (u * 352) / 1024 - (v * 731) / 1024;
                        int b = y + (u * 1814) / 1024;
                        return (r < 0 ? 0 : r > 255 ? 255 : r);
                    };
                    
                    int dst_idx = (y * fmt.width + x) * 3;
                    rgb_buffer[dst_idx] = yuv2rgb(y0, u, v);
                    rgb_buffer[dst_idx + 1] = yuv2rgb(y0, u, v);
                    rgb_buffer[dst_idx + 2] = yuv2rgb(y0, u, v);
                    
                    if (x + 1 < fmt.width) {
                        int dst_idx2 = (y * fmt.width + x + 1) * 3;
                        rgb_buffer[dst_idx2] = yuv2rgb(y1, u, v);
                        rgb_buffer[dst_idx2 + 1] = yuv2rgb(y1, u, v);
                        rgb_buffer[dst_idx2 + 2] = yuv2rgb(y1, u, v);
                    }
                }
            }
        } else {
            memcpy(rgb_buffer.data(), frame_buffer.data(), 
                   std::min((size_t)frame_size, rgb_buffer.size()));
        }
        
        // 应用处理
        uint8_t* processed = rgb_buffer.data();
        
        if (engine || brightness != 0.0f || saturation != 1.0f) {
            if (engine) {
                EnhancementParams params = {};
                params.brightness = brightness;
                params.contrast = 1.0f;
                params.saturation = saturation;
                
                surgical_enhance_frame(engine, rgb_buffer.data(),
                                     output_buffer.data(),
                                     fmt.width, fmt.height, &params);
                processed = output_buffer.data();
            } else if (brightness != 0.0f || saturation != 1.0f) {
                if (brightness != 0.0f) {
                    simd_adjust_brightness_contrast(rgb_buffer.data(),
                                                  output_buffer.data(),
                                                  fmt.width, fmt.height,
                                                  brightness, 1.0f);
                    processed = output_buffer.data();
                }
                if (saturation != 1.0f) {
                    simd_adjust_saturation(processed,
                                          processed,
                                          fmt.width, fmt.height,
                                          saturation);
                }
            }
        }
        
        auto frame_end = std::chrono::high_resolution_clock::now();
        double frame_ms = std::chrono::duration<double, std::milli>(
            frame_end - frame_start).count();
        
        total_latency_ms += frame_ms;
        total_bytes += frame_size;
        frame_count++;
        
        if (verbose || frame_count % 30 == 0) {
            printf("\rFrames: %d | Latency: %.2f ms | FPS: %.1f   ",
                   frame_count, frame_ms, 1000.0 / frame_ms);
            fflush(stdout);
        }
        
        // 检查帧数限制
        if (max_frames > 0 && frame_count >= max_frames) {
            break;
        }
    }
    
    v4l2_capture_stop(capture);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    double total_time = std::chrono::duration<double>(end_time - start_time).count();
    
    printf("\n\n=== Capture Summary ===\n");
    printf("Total frames: %d\n", frame_count);
    printf("Total time: %.2f s\n", total_time);
    printf("Average FPS: %.2f\n", frame_count / total_time);
    printf("Average latency: %.2f ms\n", total_latency_ms / frame_count);
    printf("Total data: %.2f MB\n", total_bytes / (1024.0 * 1024.0));
    printf("Throughput: %.2f Mbps\n", total_bytes * 8.0 / total_time / 1000000.0);
    
    // 清理
    if (engine) {
        surgical_engine_destroy(engine);
    }
    
    return 0;
}

// ============================================================================
// 主程序
// ============================================================================

int main(int argc, char* argv[]) {
    // 默认参数
    const char* device_path = "/dev/video0";
    const char* output_dir = "./output";
    uint32_t width = 640;
    uint32_t height = 480;
    uint32_t fps = 30;
    int max_frames = 0;
    float brightness = 0.0f;
    float saturation = 1.0f;
    bool enable_enhancement = false;
    bool verbose = false;
    
    // 解析参数
    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'H'},
        {"fps", required_argument, 0, 'f'},
        {"count", required_argument, 0, 'c'},
        {"output", required_argument, 0, 'o'},
        {"brightness", required_argument, 0, 'b'},
        {"saturation", required_argument, 0, 's'},
        {"enhance", no_argument, 0, 'e'},
        {"list", no_argument, 0, 'l'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, '?'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "d:w:H:f:c:o:b:s:elv", 
                              long_options, nullptr)) >= 0) {
        switch (opt) {
            case 'd': device_path = optarg; break;
            case 'w': width = atoi(optarg); break;
            case 'H': height = atoi(optarg); break;
            case 'f': fps = atoi(optarg); break;
            case 'c': max_frames = atoi(optarg); break;
            case 'o': output_dir = optarg; break;
            case 'b': brightness = atof(optarg); break;
            case 's': saturation = atof(optarg); break;
            case 'e': enable_enhancement = true; break;
            case 'l': list_devices(); return 0;
            case 'v': verbose = true; break;
            case '?': print_usage(argv[0]); return 0;
        }
    }
    
    // 信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("V4L2 Video Capture Demo\n");
    printf("======================\n\n");
    
    // 列出设备
    list_devices();
    
    // 打开设备
    printf("Opening device: %s\n", device_path);
    
    V4L2Capture* capture = v4l2_capture_open(device_path, width, height,
                                            V4L2_PIX_FMT_YUYV, fps);
    if (!capture) {
        fprintf(stderr, "Failed to open device: %s\n", device_path);
        fprintf(stderr, "Make sure the device exists and you have permission.\n");
        return 1;
    }
    
    V4L2FrameFormat fmt;
    v4l2_capture_get_format(capture, &fmt);
    
    printf("Device opened successfully\n");
    printf("Format: %dx%d @ %dfps\n", fmt.width, fmt.height, fmt.framerate_numer);
    printf("Pixel format: YUYV\n");
    printf("Buffer size: %u bytes\n", fmt.image_size);
    
    // 处理循环
    int ret = process_loop(capture, output_dir, enable_enhancement,
                          brightness, saturation, max_frames, verbose);
    
    // 关闭设备
    v4l2_capture_close(capture);
    
    printf("\nCapture stopped.\n");
    
    return ret;
}
