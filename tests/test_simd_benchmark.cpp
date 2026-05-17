/**
 * @file test_simd_benchmark.cpp
 * @brief SIMD性能基准测试
 */

#include "gtest/gtest.h"
#include "simd_processing.h"
#include "surgical_video.h"
#include "multimodal_fusion.h"
#include "volume_renderer.h"
#include <cstring>
#include <chrono>
#include <vector>
#include <random>
#include <cmath>

// ============================================================================
// 测试配置
// ============================================================================

struct TestConfig {
    int width;
    int height;
    int iterations;
};

static const TestConfig configs[] = {
    {640, 480, 100},
    {1280, 720, 50},
    {1920, 1080, 20},
};

static const int num_configs = sizeof(configs) / sizeof(configs[0]);

// ============================================================================
// 辅助函数
// ============================================================================

static void generate_test_data(uint8_t* data, int size) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (int i = 0; i < size; i++) {
        data[i] = dis(gen);
    }
}

static double measure_time_ms(const std::function<void()>& func, int iterations) {
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        func();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(end - start);
    
    return duration.count() / iterations;
}

// ============================================================================
// SIMD后端检测测试
// ============================================================================

TEST(SIMDBackendTest, BackendDetection) {
    SIMDBackend backend = simd_get_backend();
    const char* name = simd_get_backend_name(backend);
    
    printf("  Detected SIMD backend: %s\n", name);
    
    EXPECT_GE(backend, SIMD_NONE);
    EXPECT_LE(backend, SIMD_NEON);
    EXPECT_NE(name, nullptr);
}

// ============================================================================
// 亮度/对比度测试
// ============================================================================

class SIMDBrightnessContrastBenchmark : public ::testing::TestWithParam<TestConfig> {};

TEST_P(SIMDBrightnessContrastBenchmark, Benchmark) {
    TestConfig cfg = GetParam();
    int size = cfg.width * cfg.height * 3;
    
    std::vector<uint8_t> input(size);
    std::vector<uint8_t> output(size);
    
    generate_test_data(input.data(), size);
    
    auto func = [&]() {
        simd_adjust_brightness_contrast(input.data(), output.data(),
                                       cfg.width, cfg.height, 0.1f, 1.1f);
    };
    
    double avg_ms = measure_time_ms(func, cfg.iterations);
    double fps = 1000.0 / avg_ms;
    
    printf("  %4dx%-4d: %.3f ms/frame, %.1f fps\n", 
           cfg.width, cfg.height, avg_ms, fps);
    
    // 验证输出有效（检查是否有任何非零像素）
    bool has_nonzero = false;
    for (int i = 0; i < std::min(100, size); i++) {
        if (output[i] != 0) { has_nonzero = true; break; }
    }
    EXPECT_TRUE(has_nonzero);
}

INSTANTIATE_TEST_SUITE_P(
    ResolutionSuite,
    SIMDBrightnessContrastBenchmark,
    testing::ValuesIn(configs)
);

// ============================================================================
// 饱和度调整测试
// ============================================================================

class SIMDSaturationBenchmark : public ::testing::TestWithParam<TestConfig> {};

TEST_P(SIMDSaturationBenchmark, Benchmark) {
    TestConfig cfg = GetParam();
    int size = cfg.width * cfg.height * 3;
    
    std::vector<uint8_t> input(size);
    std::vector<uint8_t> output(size);
    
    generate_test_data(input.data(), size);
    
    auto func = [&]() {
        simd_adjust_saturation(input.data(), output.data(),
                             cfg.width, cfg.height, 1.5f);
    };
    
    double avg_ms = measure_time_ms(func, cfg.iterations);
    double fps = 1000.0 / avg_ms;
    
    printf("  %4dx%-4d: %.3f ms/frame, %.1f fps\n",
           cfg.width, cfg.height, avg_ms, fps);
}

INSTANTIATE_TEST_SUITE_P(
    ResolutionSuite,
    SIMDSaturationBenchmark,
    testing::ValuesIn(configs)
);

// ============================================================================
// RGB到灰度测试
// ============================================================================

class SIMDGrayscaleBenchmark : public ::testing::TestWithParam<TestConfig> {};

TEST_P(SIMDGrayscaleBenchmark, Benchmark) {
    TestConfig cfg = GetParam();
    int size = cfg.width * cfg.height * 3;
    
    std::vector<uint8_t> input(size);
    std::vector<uint8_t> gray(cfg.width * cfg.height);
    
    generate_test_data(input.data(), size);
    
    auto func = [&]() {
        simd_rgb_to_grayscale(input.data(), gray.data(),
                             cfg.width, cfg.height);
    };
    
    double avg_ms = measure_time_ms(func, cfg.iterations);
    double fps = 1000.0 / avg_ms;
    
    printf("  %4dx%-4d: %.3f ms/frame, %.1f fps\n",
           cfg.width, cfg.height, avg_ms, fps);
}

INSTANTIATE_TEST_SUITE_P(
    ResolutionSuite,
    SIMDGrayscaleBenchmark,
    testing::ValuesIn(configs)
);

// ============================================================================
// GSDF查表测试
// ============================================================================

class SIMDGsdfLutBenchmark : public ::testing::TestWithParam<TestConfig> {};

TEST_P(SIMDGsdfLutBenchmark, Benchmark) {
    TestConfig cfg = GetParam();
    int size = cfg.width * cfg.height * 3;
    
    std::vector<uint8_t> input(size);
    std::vector<uint8_t> output(size);
    
    generate_test_data(input.data(), size);
    
    uint8_t lut[256];
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f;
        lut[i] = (uint8_t)(255.0f * std::pow(t, 0.8f));
    }
    
    auto func = [&]() {
        simd_gsdf_lut_apply(input.data(), output.data(),
                           cfg.width, cfg.height, lut);
    };
    
    double avg_ms = measure_time_ms(func, cfg.iterations);
    double fps = 1000.0 / avg_ms;
    
    printf("  %4dx%-4d: %.3f ms/frame, %.1f fps\n",
           cfg.width, cfg.height, avg_ms, fps);
}

INSTANTIATE_TEST_SUITE_P(
    ResolutionSuite,
    SIMDGsdfLutBenchmark,
    testing::ValuesIn(configs)
);

// ============================================================================
// Sobel边缘检测测试
// ============================================================================

class SIMDSobelBenchmark : public ::testing::TestWithParam<TestConfig> {};

TEST_P(SIMDSobelBenchmark, Benchmark) {
    TestConfig cfg = GetParam();
    int size = cfg.width * cfg.height * 3;
    
    std::vector<uint8_t> input(size);
    std::vector<uint8_t> edge(cfg.width * cfg.height);
    
    generate_test_data(input.data(), size);
    
    auto func = [&]() {
        simd_edge_detection_sobel(input.data(), edge.data(),
                                cfg.width, cfg.height, 50.0f);
    };
    
    double avg_ms = measure_time_ms(func, cfg.iterations);
    double fps = 1000.0 / avg_ms;
    
    printf("  %4dx%-4d: %.3f ms/frame, %.1f fps\n",
           cfg.width, cfg.height, avg_ms, fps);
}

INSTANTIATE_TEST_SUITE_P(
    ResolutionSuite,
    SIMDSobelBenchmark,
    testing::ValuesIn(configs)
);

// ============================================================================
// 无血术野增强测试
// ============================================================================

class SIMDBloodlessBenchmark : public ::testing::TestWithParam<TestConfig> {};

TEST_P(SIMDBloodlessBenchmark, Benchmark) {
    TestConfig cfg = GetParam();
    int size = cfg.width * cfg.height * 3;
    
    std::vector<uint8_t> input(size);
    std::vector<uint8_t> output(size);
    
    generate_test_data(input.data(), size);
    
    auto func = [&]() {
        simd_bloodless_enhance(input.data(), output.data(),
                             cfg.width, cfg.height,
                             0.5f, 0.3f, 0.8f);
    };
    
    double avg_ms = measure_time_ms(func, cfg.iterations);
    double fps = 1000.0 / avg_ms;
    
    printf("  %4dx%-4d: %.3f ms/frame, %.1f fps\n",
           cfg.width, cfg.height, avg_ms, fps);
}

INSTANTIATE_TEST_SUITE_P(
    ResolutionSuite,
    SIMDBloodlessBenchmark,
    testing::ValuesIn(configs)
);

// ============================================================================
// 流水线处理测试
// ============================================================================

class SIMDPipelineBenchmark : public ::testing::TestWithParam<TestConfig> {};

TEST_P(SIMDPipelineBenchmark, FullPipeline) {
    TestConfig cfg = GetParam();
    int size = cfg.width * cfg.height * 3;
    
    std::vector<uint8_t> input(size);
    std::vector<uint8_t> output(size);
    
    generate_test_data(input.data(), size);
    
    uint8_t lut[256];
    for (int i = 0; i < 256; i++) {
        lut[i] = i;
    }
    
    SimdPipelineConfig config = {};
    config.brightness = 0.1f;
    config.contrast = 1.1f;
    config.saturation = 1.2f;
    config.gsdf_lut = lut;
    config.enable_bloodless = true;
    config.blood_suppress_level = 0.5f;
    config.tissue_enhance = 0.3f;
    config.edge_threshold = 50.0f;
    
    auto func = [&]() {
        simd_pipeline_process(input.data(), output.data(),
                            cfg.width, cfg.height, &config);
    };
    
    double avg_ms = measure_time_ms(func, cfg.iterations);
    double fps = 1000.0 / avg_ms;
    
    printf("  %4dx%-4d FULL PIPELINE: %.3f ms/frame, %.1f fps\n",
           cfg.width, cfg.height, avg_ms, fps);
}

INSTANTIATE_TEST_SUITE_P(
    ResolutionSuite,
    SIMDPipelineBenchmark,
    testing::ValuesIn(configs)
);

// ============================================================================
// 全性能对比测试
// ============================================================================

TEST(SIMDPerformanceComparison, AllOperations) {
    const int width = 640;
    const int height = 480;
    int size = width * height * 3;
    
    std::vector<uint8_t> input(size);
    std::vector<uint8_t> output(size);
    std::vector<uint8_t> gray(width * height);
    
    generate_test_data(input.data(), size);
    
    uint8_t lut[256];
    for (int i = 0; i < 256; i++) {
        lut[i] = i;
    }
    
    printf("\n  === 640x480 Performance ===\n\n");
    
    // 亮度/对比度
    {
        auto func = [&]() {
            simd_adjust_brightness_contrast(input.data(), output.data(),
                                          width, height, 0.1f, 1.1f);
        };
        double ms = measure_time_ms(func, 100);
        printf("  Brightness/Contrast: %.3f ms (%.0f fps)\n", ms, 1000.0/ms);
    }
    
    // 饱和度
    {
        auto func = [&]() {
            simd_adjust_saturation(input.data(), output.data(),
                                 width, height, 1.5f);
        };
        double ms = measure_time_ms(func, 100);
        printf("  Saturation:           %.3f ms (%.0f fps)\n", ms, 1000.0/ms);
    }
    
    // RGB到灰度
    {
        auto func = [&]() {
            simd_rgb_to_grayscale(input.data(), gray.data(), width, height);
        };
        double ms = measure_time_ms(func, 100);
        printf("  RGB to Grayscale:     %.3f ms (%.0f fps)\n", ms, 1000.0/ms);
    }
    
    // GSDF LUT
    {
        auto func = [&]() {
            simd_gsdf_lut_apply(input.data(), output.data(),
                               width, height, lut);
        };
        double ms = measure_time_ms(func, 100);
        printf("  GSDF LUT Apply:       %.3f ms (%.0f fps)\n", ms, 1000.0/ms);
    }
    
    // Sobel边缘检测
    {
        auto func = [&]() {
            simd_edge_detection_sobel(input.data(), gray.data(),
                                    width, height, 50.0f);
        };
        double ms = measure_time_ms(func, 50);
        printf("  Sobel Edge Detect:    %.3f ms (%.0f fps)\n", ms, 1000.0/ms);
    }
    
    // 无血术野增强
    {
        auto func = [&]() {
            simd_bloodless_enhance(input.data(), output.data(),
                                 width, height, 0.5f, 0.3f, 0.8f);
        };
        double ms = measure_time_ms(func, 50);
        printf("  Bloodless Enhance:    %.3f ms (%.0f fps)\n", ms, 1000.0/ms);
    }
    
    printf("\n");
}

// ============================================================================
// 基准测试入口
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    SIMDBackend backend = simd_get_backend();
    const char* name = simd_get_backend_name(backend);
    
    printf("\n================================================\n");
    printf("  SIMD Performance Benchmark\n");
    printf("  Backend: %s\n", name);
    printf("  CPU: %s\n",
#if defined(__x86_64__) || defined(_M_X64)
           "x86_64"
#elif defined(__arm64__) || defined(__aarch64__)
           "ARM64"
#else
           "Unknown"
#endif
    );
    printf("================================================\n\n");
    
    return RUN_ALL_TESTS();
}
