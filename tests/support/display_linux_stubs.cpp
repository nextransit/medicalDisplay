#include "display_engine_internal.h"

#include <cstring>
#include <vector>

namespace {

struct StubFrame {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    std::vector<uint8_t> bytes;
};

}  // namespace

int display_linux_enumerate(Display_Device* device_array, int max_devices) {
    if (!device_array || max_devices <= 0) {
        return 0;
    }

    device_array[0] = reinterpret_cast<Display_Device>(0x1);
    return 1;
}

int display_linux_open(const char* device_path) {
    (void)device_path;
    return 42;
}

void display_linux_close(int fd) {
    (void)fd;
}

void display_linux_get_caps(int fd, Display_Capabilities* caps) {
    (void)fd;
    if (!caps) {
        return;
    }

    caps->max_width = 4096;
    caps->max_height = 2160;
    caps->max_bit_depth = 12;
    caps->supports_hdr = true;
    caps->max_display_count = 2;
}

void display_linux_apply_config(int fd, const Display_Config* config) {
    (void)fd;
    (void)config;
}

void display_linux_load_degamma_lut(int fd, const uint16_t* lut, int size) {
    (void)fd;
    (void)lut;
    (void)size;
}

void display_linux_set_colorspace(int fd, Display_ColorSpace colorspace) {
    (void)fd;
    (void)colorspace;
}

void display_linux_set_hdr_mode(int fd, Display_HDRMode mode, const void* metadata) {
    (void)fd;
    (void)mode;
    (void)metadata;
}

Display_Frame display_linux_create_frame(uint32_t width, uint32_t height, uint32_t format, const void* data) {
    auto* frame = new StubFrame{};
    frame->width = width;
    frame->height = height;
    frame->format = format;
    if (data && width > 0 && height > 0) {
        size_t byte_count = static_cast<size_t>(width) * static_cast<size_t>(height);
        frame->bytes.resize(byte_count);
        std::memcpy(frame->bytes.data(), data, byte_count);
    }
    return frame;
}

void display_linux_destroy_frame(Display_Frame frame) {
    delete static_cast<StubFrame*>(frame);
}

int display_linux_present(int fd, uint32_t crtc_id, Display_Frame frame, const Display_State* state) {
    (void)fd;
    (void)crtc_id;
    (void)frame;
    (void)state;
    return 0;
}

void display_linux_present_async(int fd, uint32_t crtc_id, Display_Frame frame, void* sync_point) {
    (void)fd;
    (void)crtc_id;
    (void)frame;
    (void)sync_point;
}

void display_linux_wait_sync(const void* sync_point) {
    (void)sync_point;
}
