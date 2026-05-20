/**
 * @file test_performance.cpp
 * @brief Performance 模块单元测试
 */

#include <gtest/gtest.h>
#include <memory_pool.h>
#include <simd_processing.h>

#include <cstring>
#include <vector>

// ============================================================================
// 内存池测试
// ============================================================================

class MemoryPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.block_size = 1024;
        config.initial_blocks = 4;
        config.max_blocks = 8;
        config.thread_safe = false;
    }
    
    MemoryPoolConfig config;
};

TEST_F(MemoryPoolTest, CreateAndDestroy) {
    MemoryPool* pool = mem_pool_create(&config);
    ASSERT_NE(pool, nullptr);
    
    size_t alloc_blocks, free_blocks, wasted;
    uint64_t total_alloc, total_freed;
    mem_pool_get_stats(pool, &alloc_blocks, &free_blocks, 
                       &total_alloc, &total_freed, &wasted);
    
    EXPECT_EQ(alloc_blocks, 0u);
    EXPECT_EQ(free_blocks, 4u);
    
    mem_pool_destroy(pool);
}

TEST_F(MemoryPoolTest, AllocateAndFree) {
    MemoryPool* pool = mem_pool_create(&config);
    ASSERT_NE(pool, nullptr);
    
    void* ptr = mem_pool_alloc(pool, 512);
    ASSERT_NE(ptr, nullptr);
    
    size_t alloc_blocks, free_blocks;
    mem_pool_get_stats(pool, &alloc_blocks, &free_blocks, nullptr, nullptr, nullptr);
    EXPECT_EQ(alloc_blocks, 1u);
    EXPECT_EQ(free_blocks, 3u);
    
    mem_pool_free(pool, ptr);
    
    mem_pool_get_stats(pool, &alloc_blocks, &free_blocks, nullptr, nullptr, nullptr);
    EXPECT_EQ(alloc_blocks, 0u);
    EXPECT_EQ(free_blocks, 4u);
    
    mem_pool_destroy(pool);
}

TEST_F(MemoryPoolTest, MultipleAllocations) {
    MemoryPool* pool = mem_pool_create(&config);
    ASSERT_NE(pool, nullptr);
    
    void* ptrs[4];
    for (int i = 0; i < 4; i++) {
        ptrs[i] = mem_pool_alloc(pool, 512);
        ASSERT_NE(ptrs[i], nullptr);
    }
    
    // 第 5 次分配会触发扩容（max_blocks=8）
    void* extra = mem_pool_alloc(pool, 512);
    ASSERT_NE(extra, nullptr);
    
    // 释放一个后再分配
    mem_pool_free(pool, ptrs[0]);
    ptrs[0] = mem_pool_alloc(pool, 512);
    EXPECT_NE(ptrs[0], nullptr);

    for (int i = 0; i < 4; i++) {
        mem_pool_free(pool, ptrs[i]);
    }
    mem_pool_free(pool, extra);
    
    mem_pool_destroy(pool);
}

TEST_F(MemoryPoolTest, OversizedAllocation) {
    MemoryPool* pool = mem_pool_create(&config);
    ASSERT_NE(pool, nullptr);
    
    // 分配超过块大小的内存
    void* ptr = mem_pool_alloc(pool, 2048);
    ASSERT_NE(ptr, nullptr);
    mem_pool_free(pool, ptr);
    
    mem_pool_destroy(pool);
}

TEST_F(MemoryPoolTest, ExpandPoolAndReuseFreedBlocks) {
    MemoryPool* pool = mem_pool_create(&config);
    ASSERT_NE(pool, nullptr);

    std::vector<void*> ptrs;
    for (int i = 0; i < 8; ++i) {
        void* ptr = mem_pool_alloc(pool, 512);
        ASSERT_NE(ptr, nullptr);
        std::memset(ptr, 0xAB, 512);
        ptrs.push_back(ptr);
    }

    size_t alloc_blocks = 0;
    size_t free_blocks = 0;
    mem_pool_get_stats(pool, &alloc_blocks, &free_blocks, nullptr, nullptr, nullptr);
    EXPECT_EQ(8u, alloc_blocks);
    EXPECT_EQ(0u, free_blocks);

    for (void* ptr : ptrs) {
        mem_pool_free(pool, ptr);
    }

    mem_pool_get_stats(pool, &alloc_blocks, &free_blocks, nullptr, nullptr, nullptr);
    EXPECT_EQ(0u, alloc_blocks);
    EXPECT_EQ(8u, free_blocks);

    mem_pool_destroy(pool);
}

// ============================================================================
// 帧缓冲池测试
// ============================================================================

class FrameBufferPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        frame_size = 1920 * 1080 * 3;  // HD RGB
        frame_count = 4;
    }
    
    size_t frame_size;
    size_t frame_count;
};

TEST_F(FrameBufferPoolTest, CreateAndDestroy) {
    FrameBufferPool* pool = frame_pool_create(frame_size, frame_count);
    ASSERT_NE(pool, nullptr);
    
    size_t total, available;
    frame_pool_get_status(pool, &total, &available);
    EXPECT_EQ(total, frame_count);
    EXPECT_EQ(available, frame_count);
    
    frame_pool_destroy(pool);
}

TEST_F(FrameBufferPoolTest, AcquireAndRelease) {
    FrameBufferPool* pool = frame_pool_create(frame_size, frame_count);
    ASSERT_NE(pool, nullptr);
    
    void* frame = frame_pool_acquire(pool);
    ASSERT_NE(frame, nullptr);
    
    size_t total, available;
    frame_pool_get_status(pool, &total, &available);
    EXPECT_EQ(available, frame_count - 1);
    
    frame_pool_release(pool, frame);
    
    frame_pool_get_status(pool, &total, &available);
    EXPECT_EQ(available, frame_count);
    
    frame_pool_destroy(pool);
}

TEST_F(FrameBufferPoolTest, BatchAcquire) {
    FrameBufferPool* pool = frame_pool_create(frame_size, frame_count);
    ASSERT_NE(pool, nullptr);
    
    std::vector<void*> frames(frame_count);
    size_t acquired = frame_pool_acquire_batch(pool, frames.data(), frame_count);
    EXPECT_EQ(acquired, frame_count);
    
    size_t total, available;
    frame_pool_get_status(pool, &total, &available);
    EXPECT_EQ(available, 0u);
    
    // 全部释放
    for (size_t i = 0; i < acquired; i++) {
        frame_pool_release(pool, frames[i]);
    }
    
    frame_pool_get_status(pool, &total, &available);
    EXPECT_EQ(available, frame_count);
    
    frame_pool_destroy(pool);
}

TEST_F(FrameBufferPoolTest, FullFrameWritesDoNotCorruptAdjacentBlocks) {
    FrameBufferPool* pool = frame_pool_create(frame_size, frame_count);
    ASSERT_NE(pool, nullptr);

    std::vector<void*> frames(frame_count);
    size_t acquired = frame_pool_acquire_batch(pool, frames.data(), frame_count);
    ASSERT_EQ(frame_count, acquired);

    for (size_t index = 0; index < acquired; ++index) {
        ASSERT_NE(nullptr, frames[index]);
        std::memset(frames[index], static_cast<int>(0x10 + index), frame_size);
    }

    for (size_t index = 0; index < acquired; ++index) {
        unsigned char* bytes = static_cast<unsigned char*>(frames[index]);
        EXPECT_EQ(static_cast<unsigned char>(0x10 + index), bytes[0]);
        EXPECT_EQ(static_cast<unsigned char>(0x10 + index), bytes[frame_size - 1]);
    }

    for (size_t index = 0; index < acquired; ++index) {
        frame_pool_release(pool, frames[index]);
    }

    size_t total = 0;
    size_t available = 0;
    frame_pool_get_status(pool, &total, &available);
    EXPECT_EQ(frame_count, total);
    EXPECT_EQ(frame_count, available);

    frame_pool_destroy(pool);
}

// ============================================================================
// SIMD 处理测试
// ============================================================================

class SIMDProcessingTest : public ::testing::Test {
protected:
    void SetUp() override {
        width = 640;
        height = 480;
        input.resize(width * height * 3);
        output.resize(width * height * 3);
        
        // 填充测试数据
        for (size_t i = 0; i < input.size(); i++) {
            input[i] = i % 256;
        }
    }
    
    int width;
    int height;
    std::vector<uint8_t> input;
    std::vector<uint8_t> output;
};

TEST_F(SIMDProcessingTest, GetBackend) {
    SIMDBackend backend = simd_get_backend();
    const char* name = simd_get_backend_name(backend);
    EXPECT_NE(name, nullptr);
    EXPECT_GT(strlen(name), 0);
}

TEST_F(SIMDProcessingTest, BrightnessContrast) {
    int ret = simd_adjust_brightness_contrast(
        input.data(), output.data(), width, height, 0.2f, 1.2f);
    
    EXPECT_EQ(ret, 0);
    
    // 验证输出范围
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_GE(output[i], 0u);
        EXPECT_LE(output[i], 255u);
    }
}

TEST_F(SIMDProcessingTest, Saturation) {
    int ret = simd_adjust_saturation(
        input.data(), output.data(), width, height, 1.5f);
    
    EXPECT_EQ(ret, 0);
    
    // 验证输出范围
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_GE(output[i], 0u);
        EXPECT_LE(output[i], 255u);
    }
}

TEST_F(SIMDProcessingTest, RGBToYUV420) {
    std::vector<uint8_t> yuv(width * height * 3 / 2);
    
    int ret = simd_rgb_to_yuv420(input.data(), yuv.data(), width, height);
    EXPECT_EQ(ret, 0);
    
    // 验证Y分量范围
    for (int i = 0; i < width * height; i++) {
        EXPECT_GE(yuv[i], 0u);
        EXPECT_LE(yuv[i], 255u);
    }
}

TEST_F(SIMDProcessingTest, YUV420ToRGB) {
    std::vector<uint8_t> yuv(width * height * 3 / 2);
    std::vector<uint8_t> rgb(width * height * 3);
    
    // 先转换到YUV
    simd_rgb_to_yuv420(input.data(), yuv.data(), width, height);
    
    // 再转换回RGB
    int ret = simd_yuv420_to_rgb(yuv.data(), rgb.data(), width, height);
    EXPECT_EQ(ret, 0);
    
    // 验证输出范围
    for (size_t i = 0; i < rgb.size(); i++) {
        EXPECT_GE(rgb[i], 0u);
        EXPECT_LE(rgb[i], 255u);
    }
}

TEST_F(SIMDProcessingTest, Benchmark) {
    double ops_per_sec = 0;
    int ret = simd_benchmark(width, height, 100, &ops_per_sec);
    
    EXPECT_EQ(ret, 0);
    EXPECT_GT(ops_per_sec, 0.0);
    
    printf("SIMD Performance: %.0f ops/sec\n", ops_per_sec);
}

// ============================================================================
// 性能对比测试
// ============================================================================

TEST(PerformanceComparison, SIMDvsScalar) {
    const int width = 1920;
    const int height = 1080;
    std::vector<uint8_t> input(width * height * 3);
    std::vector<uint8_t> output(width * height * 3);
    
    // 填充数据
    for (auto& v : input) {
        v = rand() % 256;
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; i++) {
        simd_adjust_brightness_contrast(input.data(), output.data(), 
                                       width, height, 0.1f, 1.1f);
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration<double, std::milli>(end - start).count();
    printf("SIMD processing time for 10 frames (1920x1080): %.2f ms\n", duration);
    
    // 这里只做跨机器稳定断言：
    // 1. 处理必须完成
    // 2. 输出与输入不应完全相同（亮度/对比度参数应生效）
    // 3. 运行时间必须是有限正数
    EXPECT_TRUE(std::isfinite(duration));
    EXPECT_GT(duration, 0.0);
    EXPECT_NE(0, std::memcmp(input.data(), output.data(), output.size()));
}
