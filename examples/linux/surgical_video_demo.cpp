/**
 * @file surgical_video_demo.cpp
 * @brief 术野视频实时增强示例程序
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include "surgical_video.h"

static unsigned long get_time_ms() {
    return (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC);
}

static void generate_test_frame(uint8_t* frame, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            
            frame[idx + 0] = (uint8_t)((x * 255) / width);
            frame[idx + 1] = (uint8_t)((y * 255) / height);
            frame[idx + 2] = 128;
            
            int cx = width / 2, cy = height / 2;
            int r = width / 4;
            int dx = x - cx, dy = y - cy;
            int dist = (int)sqrt((double)dx*dx + dy*dy);
            
            if (dist < r) {
                frame[idx + 0] = 200;
                frame[idx + 1] = 60;
                frame[idx + 2] = 60;
            }
            
            if (dist >= r - 5 && dist <= r + 5) {
                frame[idx + 0] = 255;
                frame[idx + 1] = 255;
                frame[idx + 2] = 255;
            }
        }
    }
}

int main() {
    printf("=== 术野视频实时增强系统演示 ===\n\n");
    
    const int width = 640;  // 缩小尺寸加快处理
    const int height = 480;
    const int num_frames = 30;
    
    printf("创建腹腔镜术野增强引擎...\n");
    SurgicalVideoEngine* engine = surgical_engine_create(SURGICAL_LAPAROSCOPIC, false);
    if (!engine) {
        printf("错误: 无法创建引擎\n");
        return 1;
    }
    
    printf("应用腹腔镜预设...\n");
    EnhancementParams params;
    surgical_preset_laparoscopic(&params);
    surgical_engine_config(engine, &params);
    
    size_t frame_size = width * height * 3;
    uint8_t* input_frame = new uint8_t[frame_size];
    uint8_t* output_frame = new uint8_t[frame_size];
    
    printf("\n处理 %d 帧 (%dx%d)...\n\n", num_frames, width, height);
    
    PerformanceStats stats;
    surgical_engine_reset_stats(engine);
    
    for (int i = 0; i < num_frames; i++) {
        generate_test_frame(input_frame, width, height);
        
        VideoFrameInfo info = {};
        info.width = width;
        info.height = height;
        info.format = 1;
        info.timestamp_us = get_time_ms() * 1000;
        info.frame_rate = 60.0f;
        
        unsigned long start = get_time_ms();
        
        if (surgical_engine_process_frame(engine, input_frame, &info, 
                                         output_frame, nullptr) != 0) {
            printf("帧 %d 处理失败\n", i);
            continue;
        }
        
        unsigned long end = get_time_ms();
        float latency_ms = (float)(end - start);
        
        if (i % 10 == 0 || i == num_frames - 1) {
            printf("帧 %3d: 延迟 %.2f ms\n", i, latency_ms);
        }
    }
    
    surgical_engine_get_stats(engine, &stats);
    
    printf("\n=== 性能统计 ===\n");
    printf("总处理帧数: %u\n", stats.frames_processed);
    printf("平均延迟:   %.2f ms\n", stats.avg_latency_ms);
    printf("P95延迟:    %.2f ms\n", stats.p95_latency_ms);
    printf("P99延迟:    %.2f ms\n", stats.p99_latency_ms);
    printf("最大延迟:   %.2f ms\n", stats.max_latency_ms);
    printf("当前帧率:   %.1f fps\n", stats.current_fps);
    
    bool latency_ok = surgical_engine_check_latency(engine, 20.0f);
    printf("\n延迟要求检查 (< 20ms): %s\n", latency_ok ? "✓ 通过" : "✗ 未通过");
    
    printf("\n=== 增强模式测试 ===\n");
    
    EnhancementMode modes[] = { ENHANCE_NONE, ENHANCE_TISSUE_BOUNDRY, ENHANCE_VASCULAR, ENHANCE_NERVE };
    const char* mode_names[] = { "无增强", "组织边界", "血管增强", "神经增强" };
    
    for (int m = 0; m < 4; m++) {
        surgical_engine_set_mode(engine, modes[m]);
        
        generate_test_frame(input_frame, width, height);
        VideoFrameInfo info = { (uint32_t)width, (uint32_t)height, 1, 0, 60.0f, 0, 0 };
        
        unsigned long start = get_time_ms();
        surgical_engine_process_frame(engine, input_frame, &info, output_frame, nullptr);
        unsigned long end = get_time_ms();
        
        printf("  %s: %.2f ms\n", mode_names[m], (float)(end - start));
    }
    
    printf("\n=== 手术类型预设 ===\n");
    
    EnhancementParams preset;
    surgical_preset_laparoscopic(&preset);
    printf("腹腔镜: brightness=%.2f, contrast=%.2f, edge=%.2f\n",
           preset.brightness, preset.contrast, preset.edge_strength);
    
    surgical_preset_bloodless(&preset);
    printf("无血术野: brightness=%.2f, contrast=%.2f, bloodless=%.2f\n",
           preset.brightness, preset.contrast, preset.bloodless_strength);
    
    delete[] input_frame;
    delete[] output_frame;
    surgical_engine_destroy(engine);
    
    printf("\n=== 演示完成 ===\n");
    return 0;
}
