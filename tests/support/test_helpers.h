#ifndef TESTS_SUPPORT_TEST_HELPERS_H
#define TESTS_SUPPORT_TEST_HELPERS_H

#include "ai_engine.h"
#include "display_engine_internal.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace test_helpers {

inline AIEngineConfig make_ai_config(int input_width = 16, int input_height = 16) {
    AIEngineConfig config{};
    std::memset(config.model_path, 0, sizeof(config.model_path));
    config.num_threads = 1;
    config.use_npu = false;
    config.use_gpu = false;
    config.max_batch_size = 4;
    config.input_width = input_width;
    config.input_height = input_height;
    config.score_threshold = 0.5f;
    return config;
}

inline Display_Config make_display_config() {
    Display_Config config{};
    config.color_space = DISPLAY_COLORSPACE_DICOM_GSDF;
    config.gsdf_profile = DISPLAY_GSDF_CT;
    config.gamma = DISPLAY_GAMMA_2_2;
    config.enable_gsdf = true;
    config.enable_local_enhance = false;
    config.target_luminance = 500.0f;
    config.ambient_light_sensor = 12.5f;
    config.hdr_mode = DISPLAY_HDR_OFF;
    config.window_width = 400.0f;
    config.window_center = 40.0f;
    return config;
}

inline std::string create_minimal_dicom_file(const std::string& stem = "medicaldisplay-test") {
    char path_buffer[1024];
    std::snprintf(path_buffer, sizeof(path_buffer), "/tmp/%s-XXXXXX.dcm", stem.c_str());

    std::string path = path_buffer;
    std::vector<char> writable(path.begin(), path.end());
    writable.push_back('\0');

    int fd = mkstemps(writable.data(), 4);
    if (fd < 0) {
        return {};
    }

    FILE* file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        return {};
    }

    std::array<uint8_t, 128> preamble{};
    std::fwrite(preamble.data(), 1, preamble.size(), file);
    std::fwrite("DICM", 1, 4, file);

    // Implicit VR Little Endian format: tag (4 bytes) + length (4 bytes) + data
    // SOP Class UID (0008,0016) - CT Image Storage
    uint8_t sop_class_uid[] = {
        0x08, 0x00, 0x16, 0x00,  // Tag: (0008,0016)
        0x1A, 0x00, 0x00, 0x00,  // Length: 26 (implicit VR)
        '1', '.', '2', '.', '8', '4', '0', '.', '1', '0', '0', '0', '8', '.',
        '5', '.', '1', '.', '4', '.', '1', '.', '1', '.', '2', '\0'
    };
    std::fwrite(sop_class_uid, 1, sizeof(sop_class_uid), file);

    // Modality (0008,0060) - "CT" (implicit VR)
    uint8_t modality_tag[] = {
        0x08, 0x00, 0x60, 0x00,  // Tag: (0008,0060)
        0x02, 0x00, 0x00, 0x00,  // Length: 2 (implicit VR)
        'C', 'T'                    // Value: "CT"
    };
    std::fwrite(modality_tag, 1, sizeof(modality_tag), file);

    std::fclose(file);

    return std::string(writable.data());
}

inline std::string create_multiframe_dicom_file(const std::string& stem = "medicaldisplay-multiframe") {
    char path_buffer[1024];
    std::snprintf(path_buffer, sizeof(path_buffer), "/tmp/%s-XXXXXX.dcm", stem.c_str());

    std::string path = path_buffer;
    std::vector<char> writable(path.begin(), path.end());
    writable.push_back('\0');

    int fd = mkstemps(writable.data(), 4);
    if (fd < 0) {
        return {};
    }

    FILE* file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        return {};
    }

    auto write_tag = [&](uint16_t group, uint16_t element, const void* data, uint32_t length) {
        const uint8_t tag_header[8] = {
            static_cast<uint8_t>(group & 0xFF), static_cast<uint8_t>((group >> 8) & 0xFF),
            static_cast<uint8_t>(element & 0xFF), static_cast<uint8_t>((element >> 8) & 0xFF),
            static_cast<uint8_t>(length & 0xFF), static_cast<uint8_t>((length >> 8) & 0xFF),
            static_cast<uint8_t>((length >> 16) & 0xFF), static_cast<uint8_t>((length >> 24) & 0xFF),
        };
        std::fwrite(tag_header, 1, sizeof(tag_header), file);
        if (length > 0 && data) {
            std::fwrite(data, 1, length, file);
        }
    };

    std::array<uint8_t, 128> preamble{};
    std::fwrite(preamble.data(), 1, preamble.size(), file);
    std::fwrite("DICM", 1, 4, file);

    const char modality[] = {'U', 'S'};
    write_tag(0x0008, 0x0060, modality, 2);

    const uint16_t rows = 2;
    const uint16_t cols = 2;
    const uint16_t bits_allocated = 16;
    const uint16_t bits_stored = 16;
    const uint16_t high_bit = 15;
    const uint16_t pixel_representation = 0;
    const uint16_t samples_per_pixel = 1;
    write_tag(0x0028, 0x0010, &rows, sizeof(rows));
    write_tag(0x0028, 0x0011, &cols, sizeof(cols));
    write_tag(0x0028, 0x0100, &bits_allocated, sizeof(bits_allocated));
    write_tag(0x0028, 0x0101, &bits_stored, sizeof(bits_stored));
    write_tag(0x0028, 0x0102, &high_bit, sizeof(high_bit));
    write_tag(0x0028, 0x0103, &pixel_representation, sizeof(pixel_representation));
    write_tag(0x0028, 0x0002, &samples_per_pixel, sizeof(samples_per_pixel));

    const char frame_count[] = {'2'};
    write_tag(0x0028, 0x0008, frame_count, 1);

    const uint16_t frames[] = {
        1, 2, 3, 4,
        11, 12, 13, 14
    };
    write_tag(0x7FE0, 0x0010, frames, static_cast<uint32_t>(sizeof(frames)));

    std::fclose(file);
    return std::string(writable.data());
}

inline std::string create_rle_dicom_file(const std::string& stem = "medicaldisplay-rle") {
    char path_buffer[1024];
    std::snprintf(path_buffer, sizeof(path_buffer), "/tmp/%s-XXXXXX.dcm", stem.c_str());

    std::string path = path_buffer;
    std::vector<char> writable(path.begin(), path.end());
    writable.push_back('\0');

    int fd = mkstemps(writable.data(), 4);
    if (fd < 0) {
        return {};
    }

    FILE* file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        return {};
    }

    auto write_explicit_ui = [&](uint16_t group, uint16_t element, const char* value, uint16_t length) {
        const uint8_t header[8] = {
            static_cast<uint8_t>(group & 0xFF), static_cast<uint8_t>((group >> 8) & 0xFF),
            static_cast<uint8_t>(element & 0xFF), static_cast<uint8_t>((element >> 8) & 0xFF),
            'U', 'I',
            static_cast<uint8_t>(length & 0xFF), static_cast<uint8_t>((length >> 8) & 0xFF),
        };
        std::fwrite(header, 1, sizeof(header), file);
        if (length > 0 && value) {
            std::fwrite(value, 1, length, file);
        }
    };

    auto write_explicit_short = [&](uint16_t group, uint16_t element, const char vr[2], const void* data, uint16_t length) {
        const uint8_t header[8] = {
            static_cast<uint8_t>(group & 0xFF), static_cast<uint8_t>((group >> 8) & 0xFF),
            static_cast<uint8_t>(element & 0xFF), static_cast<uint8_t>((element >> 8) & 0xFF),
            static_cast<uint8_t>(vr[0]), static_cast<uint8_t>(vr[1]),
            static_cast<uint8_t>(length & 0xFF), static_cast<uint8_t>((length >> 8) & 0xFF),
        };
        std::fwrite(header, 1, sizeof(header), file);
        if (length > 0 && data) {
            std::fwrite(data, 1, length, file);
        }
    };

    auto write_explicit_long = [&](uint16_t group, uint16_t element, const char vr[2], const void* data, uint32_t length) {
        const uint8_t header[8] = {
            static_cast<uint8_t>(group & 0xFF), static_cast<uint8_t>((group >> 8) & 0xFF),
            static_cast<uint8_t>(element & 0xFF), static_cast<uint8_t>((element >> 8) & 0xFF),
            static_cast<uint8_t>(vr[0]), static_cast<uint8_t>(vr[1]),
            0x00, 0x00,
        };
        const uint8_t len_bytes[4] = {
            static_cast<uint8_t>(length & 0xFF), static_cast<uint8_t>((length >> 8) & 0xFF),
            static_cast<uint8_t>((length >> 16) & 0xFF), static_cast<uint8_t>((length >> 24) & 0xFF),
        };
        std::fwrite(header, 1, sizeof(header), file);
        std::fwrite(len_bytes, 1, sizeof(len_bytes), file);
        if (length > 0 && data) {
            std::fwrite(data, 1, length, file);
        }
    };

    std::array<uint8_t, 128> preamble{};
    std::fwrite(preamble.data(), 1, preamble.size(), file);
    std::fwrite("DICM", 1, 4, file);

    const char meta_ts_uid[] = "1.2.840.10008.1.2.5";
    write_explicit_ui(0x0002, 0x0010, meta_ts_uid, static_cast<uint16_t>(sizeof(meta_ts_uid) - 1));

    const char modality[] = {'C', 'T'};
    write_explicit_short(0x0008, 0x0060, "CS", modality, 2);

    const uint16_t rows = 2;
    const uint16_t cols = 2;
    const uint16_t bits_allocated = 8;
    const uint16_t bits_stored = 8;
    const uint16_t high_bit = 7;
    const uint16_t pixel_representation = 0;
    const uint16_t samples_per_pixel = 1;
    write_explicit_short(0x0028, 0x0010, "US", &rows, sizeof(rows));
    write_explicit_short(0x0028, 0x0011, "US", &cols, sizeof(cols));
    write_explicit_short(0x0028, 0x0100, "US", &bits_allocated, sizeof(bits_allocated));
    write_explicit_short(0x0028, 0x0101, "US", &bits_stored, sizeof(bits_stored));
    write_explicit_short(0x0028, 0x0102, "US", &high_bit, sizeof(high_bit));
    write_explicit_short(0x0028, 0x0103, "US", &pixel_representation, sizeof(pixel_representation));
    write_explicit_short(0x0028, 0x0002, "US", &samples_per_pixel, sizeof(samples_per_pixel));

    std::vector<uint8_t> rle;
    rle.resize(64, 0);
    const uint32_t segment_count = 1;
    const uint32_t segment_offset = 64;
    std::memcpy(rle.data(), &segment_count, sizeof(segment_count));
    std::memcpy(rle.data() + 4, &segment_offset, sizeof(segment_offset));
    rle.push_back(3);  // literal run of 4 bytes
    rle.push_back(1);
    rle.push_back(2);
    rle.push_back(3);
    rle.push_back(4);

    write_explicit_long(0x7FE0, 0x0010, "OB", rle.data(), static_cast<uint32_t>(rle.size()));

    std::fclose(file);
    return std::string(writable.data());
}

inline std::vector<uint8_t> make_rgb_frame(int width, int height, uint8_t base = 64) {
    std::vector<uint8_t> buffer(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
    for (size_t index = 0; index < buffer.size(); index += 3) {
        buffer[index + 0] = base;
        buffer[index + 1] = static_cast<uint8_t>(base + 16);
        buffer[index + 2] = static_cast<uint8_t>(base + 32);
    }
    return buffer;
}

inline std::vector<uint16_t> make_mono16_frame(int width, int height, uint16_t base = 1024) {
    std::vector<uint16_t> buffer(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t index = 0; index < buffer.size(); ++index) {
        buffer[index] = static_cast<uint16_t>(base + (index % 32u));
    }
    return buffer;
}

}  // namespace test_helpers

#endif
