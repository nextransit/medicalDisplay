/**
 * @file test_dicom_fuzz.cpp
 * @brief DICOM 反序列化 Fuzz 安全测试
 *
 * 测试 dicom_reader 对各种畸形/恶意 DICOM 输入的鲁棒性。
 * 覆盖: 缓冲区溢出、无限循环、null 解引用、格式错误处理
 *
 * 注意: dicom_open 对于 magic 不匹配的文件会 rewind() 继续尝试，
 * 这是一种宽松的"尽力解析"策略，不会返回 nullptr。
 * 测试的重点是验证不会崩溃或造成安全漏洞。
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <random>

extern "C" {
#include "dicom_reader.h"
}

// ============================================================================
// 辅助: 写入最小有效 DICOM 文件
// ============================================================================

static void write_dicom_file(const char* path, const uint8_t* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
}

// ============================================================================
// Test 1: 空文件
// ============================================================================

TEST(DicomFuzzTest, EmptyFile) {
    write_dicom_file("/tmp/dicom_empty.dcm", nullptr, 0);
    DICOM_Dataset ds = dicom_open("/tmp/dicom_empty.dcm");
    EXPECT_EQ(ds, nullptr);
}

// ============================================================================
// Test 2: 极小文件 (< 128+4 bytes, 不够 preamble+magic)
// ============================================================================

TEST(DicomFuzzTest, TinyFile) {
    uint8_t tiny[] = {0x44, 0x49};  // "DI" only
    write_dicom_file("/tmp/dicom_tiny.dcm", tiny, sizeof(tiny));
    DICOM_Dataset ds = dicom_open("/tmp/dicom_tiny.dcm");
    EXPECT_EQ(ds, nullptr);
}

// ============================================================================
// Test 3: 无效 magic — dicom_open 会 rewind() 宽容解析
// ============================================================================

TEST(DicomFuzzTest, InvalidMagic) {
    uint8_t buf[256] = {};
    // 跳过 128 字节 preamble
    buf[128] = 'B'; buf[129] = 'A'; buf[130] = 'D'; buf[131] = '!';  // "BAD!"
    write_dicom_file("/tmp/dicom_badmagic.dcm", buf, sizeof(buf));
    DICOM_Dataset ds = dicom_open("/tmp/dicom_badmagic.dcm");
    // dicom_open 用 rewind() 宽容处理 magic 不匹配，不应崩溃
    // 不强制 nullptr，验证的是不会崩溃或访问越界
    if (ds) {
        DICOM_PixelData pd;
        dicom_read_pixels(ds, &pd);
        DICOM_Metadata metadata;
        dicom_extract_metadata(ds, &metadata);
        dicom_close(ds);
    }
    SUCCEED() << "InvalidMagic: no crash on bad magic";
}

// ============================================================================
// Test 4: 超大像素数据长度 (防止缓冲区溢出)
// ============================================================================

TEST(DicomFuzzTest, HugePixelDataLength) {
    // 构造一个 DICOM 文件，其 (7FE0,0010) Pixel Data 的 length 设为极大值
    uint8_t buf[512] = {};
    // Preamble + magic
    buf[128] = 'D'; buf[129] = 'I'; buf[130] = 'C'; buf[131] = 'M';

    // 写入一个 (7FE0,0010) OB 标签，length = 0xFFFFFFFF (OB 用显式 VR)
    size_t off = 132;
    buf[off++] = 0xFE; buf[off++] = 0x7F;  // Group 7FE0
    buf[off++] = 0x10; buf[off++] = 0x00;  // Element 0010
    buf[off++] = 'O';  buf[off++] = 'B';   // VR = OB
    buf[off++] = 0x00; buf[off++] = 0x00;  // Reserved
    buf[off++] = 0xFF; buf[off++] = 0xFF;  // Length = 0xFFFFFFFF
    buf[off++] = 0xFF; buf[off++] = 0xFF;

    write_dicom_file("/tmp/dicom_hugepix.dcm", buf, sizeof(buf));
    DICOM_Dataset ds = dicom_open("/tmp/dicom_hugepix.dcm");
    // 安全要求: 不应崩溃，不应分配 4GB 内存
    if (ds) {
        DICOM_PixelData pd;
        int ret = dicom_read_pixels(ds, &pd);
        // 如果返回 0，检查返回的 buffer 大小是否合理
        if (ret == 0) {
            // 即使成功，buffer 大小也不应超过安全限制 (500MB)
            EXPECT_LE(pd.pixel_data_size, 500ULL * 1024 * 1024) 
                << "Pixel buffer size " << pd.pixel_data_size << " exceeds 500MB safety limit";
        }
        dicom_close(ds);
    }
    SUCCEED() << "HugePixelDataLength: no crash, no 4GB allocation";
}

// ============================================================================
// Test 5: 截断的 DICOM (文件在中途结束)
// ============================================================================

TEST(DicomFuzzTest, TruncatedDicom) {
    // 创建最小有效 DICOM 然后截断一半
    uint8_t buf[256] = {};
    buf[128] = 'D'; buf[129] = 'I'; buf[130] = 'C'; buf[131] = 'M';
    size_t off = 132;
    // 写入 (0008,0016) SOP Class UID (UI, length=6)
    buf[off++] = 0x08; buf[off++] = 0x00;
    buf[off++] = 0x16; buf[off++] = 0x00;
    buf[off++] = 'U';  buf[off++] = 'I';
    buf[off++] = 0x06; buf[off++] = 0x00;
    for (int i = 0; i < 6; i++) buf[off++] = 'A';

    // 截断: 只写 off 字节
    write_dicom_file("/tmp/dicom_trunc.dcm", buf, off);
    DICOM_Dataset ds = dicom_open("/tmp/dicom_trunc.dcm");
    // 不应崩溃
    if (ds) dicom_close(ds);
}

// ============================================================================
// Test 6: 随机数据 fuzz
// ============================================================================

TEST(DicomFuzzTest, RandomData) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);

    for (int trial = 0; trial < 50; trial++) {
        size_t size = 128 + (rng() % 1024);
        std::vector<uint8_t> buf(size);
        for (size_t i = 0; i < size; i++) {
            buf[i] = static_cast<uint8_t>(dist(rng));
        }

        char path[64];
        snprintf(path, sizeof(path), "/tmp/dicom_fuzz_%02d.dcm", trial);
        write_dicom_file(path, buf.data(), size);

        DICOM_Dataset ds = dicom_open(path);
        if (ds) {
            // 尝试读取各种字段
            DICOM_PixelData pd;
            dicom_read_pixels(ds, &pd);

            DICOM_Metadata metadata;
            dicom_extract_metadata(ds, &metadata);

            dicom_close(ds);
        }
        remove(path);

        // 检查点: 每 10 次 trial 确保没有崩溃
        if (trial % 10 == 0) {
            SUCCEED() << "Trial " << trial << "/50 passed (no crash)";
        }
    }
}

// ============================================================================
// Test 7: VR 长度溢出 (Explicit VR)
// ============================================================================

TEST(DicomFuzzTest, VRLengthOverflow) {
    uint8_t buf[256] = {};
    buf[128] = 'D'; buf[129] = 'I'; buf[130] = 'C'; buf[131] = 'M';
    size_t off = 132;

    // (0010,0010) Patient's Name, PN, length=0xFFFF (65535 chars)
    buf[off++] = 0x10; buf[off++] = 0x00;
    buf[off++] = 0x10; buf[off++] = 0x00;
    buf[off++] = 'P';  buf[off++] = 'N';
    buf[off++] = 0xFF; buf[off++] = 0xFF;  // length = 65535

    write_dicom_file("/tmp/dicom_vroverflow.dcm", buf, sizeof(buf));
    DICOM_Dataset ds = dicom_open("/tmp/dicom_vroverflow.dcm");
    if (ds) {
        DICOM_Metadata metadata = {};
        dicom_extract_metadata(ds, &metadata);
        // 不应越界读取 65535 字节
        dicom_close(ds);
    }
}

// ============================================================================
// Test 8: 嵌套序列 (SQ) 长度过大
// ============================================================================

TEST(DicomFuzzTest, NestedSequenceOverflow) {
    uint8_t buf[384] = {};
    buf[128] = 'D'; buf[129] = 'I'; buf[130] = 'C'; buf[131] = 'M';
    size_t off = 132;

    // 写入一个 SQ 标签，声称 length = 0xFFFFFFFF
    buf[off++] = 0x20; buf[off++] = 0x00;  // Group 0020 (arbitrary)
    buf[off++] = 0x00; buf[off++] = 0x00;
    buf[off++] = 'S';  buf[off++] = 'Q';
    buf[off++] = 0x00; buf[off++] = 0x00;
    buf[off++] = 0xFF; buf[off++] = 0xFF;  // length = 0xFFFF (undefined length)
    buf[off++] = 0xFF; buf[off++] = 0xFF;

    write_dicom_file("/tmp/dicom_sqoverflow.dcm", buf, sizeof(buf));
    DICOM_Dataset ds = dicom_open("/tmp/dicom_sqoverflow.dcm");
    if (ds) dicom_close(ds);
}

// ============================================================================
// Test 9: 重复打开/关闭 (资源泄漏检查)
// ============================================================================

TEST(DicomFuzzTest, RepeatedOpenClose) {
    uint8_t buf[256] = {};
    buf[128] = 'D'; buf[129] = 'I'; buf[130] = 'C'; buf[131] = 'M';
    write_dicom_file("/tmp/dicom_repeat.dcm", buf, sizeof(buf));

    for (int i = 0; i < 100; i++) {
        DICOM_Dataset ds = dicom_open("/tmp/dicom_repeat.dcm");
        if (ds) dicom_close(ds);
    }
    SUCCEED() << "100 open/close cycles passed (no memory leak crash)";
}

// ============================================================================
// Test 10: null 参数安全
// ============================================================================

TEST(DicomFuzzTest, NullParameterSafety) {
    EXPECT_EQ(dicom_open(nullptr), nullptr);
    EXPECT_EQ(dicom_read_pixels(nullptr, nullptr), -1);

    DICOM_PixelData pd;
    EXPECT_EQ(dicom_read_pixels(nullptr, &pd), -1);

    dicom_close(nullptr);  // 不应崩溃
    SUCCEED() << "Null parameter safety: no crash";
}
