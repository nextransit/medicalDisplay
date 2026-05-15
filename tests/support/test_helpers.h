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

    const uint8_t payload[] = {
        0x08, 0x00, 0x16, 0x00, 'U', 'I', 0x1a, 0x00,
        '1', '.', '2', '.', '8', '4', '0', '.', '1', '0', '0', '0', '8', '.',
        '5', '.', '1', '.', '4', '.', '1', '.', '1', '.', '2', '\0'
    };
    std::fwrite(payload, 1, sizeof(payload), file);
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
