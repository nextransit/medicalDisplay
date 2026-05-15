#include "drm_mock.h"

#include "xf86drm.h"
#include "xf86drmMode.h"

#include <cstdlib>
#include <cstring>

namespace {

DrmMockState g_state{};
uint32_t g_next_blob_id = 100;
uint32_t g_next_fb_id = 200;

template <typename T>
T* alloc_one() {
    return static_cast<T*>(std::calloc(1, sizeof(T)));
}

char* dup_string(const char* value) {
    size_t size = std::strlen(value) + 1;
    char* copy = static_cast<char*>(std::malloc(size));
    std::memcpy(copy, value, size);
    return copy;
}

void fill_mode(drmModeModeInfo* mode, uint16_t width, uint16_t height, uint32_t refresh) {
    mode->clock = 148500;
    mode->hdisplay = width;
    mode->hsync_start = width + 88;
    mode->hsync_end = width + 132;
    mode->htotal = width + 280;
    mode->vdisplay = height;
    mode->vsync_start = height + 4;
    mode->vsync_end = height + 9;
    mode->vtotal = height + 45;
    mode->vrefresh = refresh;
    mode->flags = 0;
}

}  // namespace

extern "C" {

void drm_mock_reset(void) {
    std::memset(&g_state, 0, sizeof(g_state));
    g_next_blob_id = 100;
    g_next_fb_id = 200;
}

const DrmMockState* drm_mock_state(void) {
    return &g_state;
}

drmVersion* drmGetVersion(int fd) {
    (void)fd;
    auto* version = alloc_one<drmVersion>();
    version->name = dup_string("mockdrm");
    version->date = dup_string("2026-05-15");
    return version;
}

void drmFreeVersion(drmVersion* version) {
    if (!version) {
        return;
    }
    std::free(version->name);
    std::free(version->date);
    std::free(version);
}

int drmIoctl(int fd, unsigned long request, void* arg) {
    (void)fd;

    if (request == DRM_IOCTL_MODE_CREATE_DUMB) {
        auto* create = static_cast<drm_mode_create_dumb*>(arg);
        create->handle = 77;
        create->pitch = create->width * (create->bpp / 8);
        create->size = static_cast<uint64_t>(create->pitch) * create->height;
        return 0;
    }

    if (request == DRM_IOCTL_MODE_MAP_DUMB) {
        auto* map = static_cast<drm_mode_map_dumb*>(arg);
        map->offset = 0;
        return 0;
    }

    if (request == DRM_IOCTL_MODE_DESTROY_DUMB) {
        return 0;
    }

    return 0;
}

int drmWaitVBlank(int fd, drmVBlankSeq* sequence) {
    (void)fd;
    g_state.last_page_flip_user_data = static_cast<uintptr_t>(sequence ? sequence->request.signal : 0);
    return 0;
}

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t* handle) {
    (void)fd;
    (void)flags;
    if (handle) {
        *handle = 501;
    }
    return 0;
}

int drmSyncobjDestroy(int fd, uint32_t handle) {
    (void)fd;
    (void)handle;
    return 0;
}

int drmSyncobjHandleToFd(int fd, uint32_t handle, int* fd_out) {
    (void)fd;
    if (fd_out) {
        *fd_out = static_cast<int>(handle);
    }
    return 0;
}

int drmSyncobjFdToHandle(int fd, int fd_in, uint32_t* handle_out) {
    (void)fd;
    if (handle_out) {
        *handle_out = static_cast<uint32_t>(fd_in);
    }
    return 0;
}

int drmSyncobjWait(int fd, uint32_t* handles, int count, int64_t timeout_ns, uint32_t flags, uint32_t* signaled) {
    (void)fd;
    (void)handles;
    (void)count;
    (void)timeout_ns;
    (void)flags;
    if (signaled) {
        *signaled = 1;
    }
    return 0;
}

int drmSyncobjReset(int fd, const uint32_t* handles, int count) {
    (void)fd;
    (void)handles;
    (void)count;
    return 0;
}

int drmSyncobjSignal(int fd, const uint32_t* handles, int count) {
    (void)fd;
    (void)handles;
    (void)count;
    return 0;
}

drmModeResPtr drmModeGetResources(int fd) {
    (void)fd;
    auto* res = alloc_one<drmModeRes>();
    res->count_encoders = 2;
    res->encoders = static_cast<uint32_t*>(std::calloc(2, sizeof(uint32_t)));
    res->encoders[0] = 11;
    res->encoders[1] = 12;
    res->count_connectors = 2;
    res->connectors = static_cast<uint32_t*>(std::calloc(2, sizeof(uint32_t)));
    res->connectors[0] = 21;
    res->connectors[1] = 22;
    res->count_crtcs = 1;
    res->crtcs = static_cast<uint32_t*>(std::calloc(1, sizeof(uint32_t)));
    res->crtcs[0] = 31;
    res->min_width = 640;
    res->max_width = 4096;
    res->min_height = 480;
    res->max_height = 2160;
    return res;
}

void drmModeFreeResources(drmModeResPtr ptr) {
    if (!ptr) {
        return;
    }
    std::free(ptr->encoders);
    std::free(ptr->connectors);
    std::free(ptr->crtcs);
    std::free(ptr);
}

drmModePlaneResPtr drmModeGetPlaneResources(int fd) {
    (void)fd;
    auto* res = alloc_one<drmModePlaneRes>();
    res->count_planes = 1;
    res->planes = static_cast<uint32_t*>(std::calloc(1, sizeof(uint32_t)));
    res->planes[0] = 41;
    return res;
}

void drmModeFreePlaneResources(drmModePlaneResPtr ptr) {
    if (!ptr) {
        return;
    }
    std::free(ptr->planes);
    std::free(ptr);
}

drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connector_id) {
    (void)fd;
    auto* connector = alloc_one<drmModeConnector>();
    connector->connection = DRM_MODE_CONNECTED;
    connector->count_encoders = 1;
    connector->encoder_id = 11;
    connector->count_modes = 2;
    connector->modes = static_cast<drmModeModeInfo*>(std::calloc(2, sizeof(drmModeModeInfo)));
    fill_mode(&connector->modes[0], 1920, 1080, 60);
    fill_mode(&connector->modes[1], 2560, 1440, 75);
    if (connector_id == 0) {
        connector->connection = 0;
    }
    return connector;
}

void drmModeFreeConnector(drmModeConnectorPtr ptr) {
    if (!ptr) {
        return;
    }
    std::free(ptr->modes);
    std::free(ptr);
}

drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t crtc_id) {
    (void)fd;
    auto* crtc = alloc_one<drmModeCrtc>();
    crtc->crtc_id = crtc_id;
    crtc->encoder_id = 11;
    crtc->mode_valid = 1;
    fill_mode(&crtc->mode, 3840, 2160, 60);
    return crtc;
}

void drmModeFreeCrtc(drmModeCrtcPtr ptr) {
    std::free(ptr);
}

int drmModeSetCrtc(int fd, uint32_t crtc_id, uint32_t buffer_id, uint32_t x, uint32_t y, const uint32_t* connectors, int count, const drmModeModeInfo* mode) {
    (void)fd;
    (void)x;
    (void)y;
    (void)connectors;
    (void)count;
    (void)mode;
    g_state.last_page_flip_crtc = crtc_id;
    g_state.last_page_flip_fb = buffer_id;
    return 0;
}

int drmModeSetPlane(int fd, uint32_t plane_id, uint32_t crtc_id, uint32_t fb_id, uint32_t flags, int32_t crtc_x, int32_t crtc_y, uint32_t crtc_w, uint32_t crtc_h, uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h) {
    (void)fd;
    (void)plane_id;
    (void)crtc_id;
    (void)fb_id;
    (void)flags;
    (void)crtc_x;
    (void)crtc_y;
    (void)crtc_w;
    (void)crtc_h;
    (void)src_x;
    (void)src_y;
    (void)src_w;
    (void)src_h;
    return 0;
}

int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t flags, void* user_data) {
    (void)fd;
    (void)flags;
    g_state.last_page_flip_crtc = crtc_id;
    g_state.last_page_flip_fb = fb_id;
    g_state.last_page_flip_user_data = reinterpret_cast<uintptr_t>(user_data);
    return 0;
}

int drmModeAddFB2(int fd, uint32_t width, uint32_t height, uint32_t pixel_format, const uint32_t* bo_handles, const uint32_t* pitches, const uint32_t* offsets, uint32_t* buf_id, uint32_t flags) {
    (void)fd;
    (void)width;
    (void)height;
    (void)pixel_format;
    (void)bo_handles;
    (void)pitches;
    (void)offsets;
    (void)flags;
    if (buf_id) {
        *buf_id = g_next_fb_id++;
    }
    return 0;
}

int drmModeAddFB2WithModifiers(int fd, uint32_t width, uint32_t height, uint32_t pixel_format, const uint32_t* bo_handles, const uint32_t* pitches, const uint32_t* offsets, const uint64_t* modifier, uint32_t* buf_id, uint32_t flags) {
    (void)modifier;
    return drmModeAddFB2(fd, width, height, pixel_format, bo_handles, pitches, offsets, buf_id, flags);
}

int drmModeRmFB(int fd, uint32_t buffer_id) {
    (void)fd;
    (void)buffer_id;
    return 0;
}

int drmModeCrtcSetGamma(int fd, uint32_t crtc_id, uint32_t size, uint16_t* red, uint16_t* green, uint16_t* blue) {
    (void)fd;
    (void)crtc_id;
    (void)red;
    (void)green;
    (void)blue;
    g_state.last_gamma_size = static_cast<uint16_t>(size);
    return 0;
}

int drmModeGetCrtcGammaSize(int fd, uint32_t crtc_id) {
    (void)fd;
    (void)crtc_id;
    return 256;
}

int drmModeCreatePropertyBlob(int fd, const void* data, size_t size, uint32_t* id) {
    (void)fd;
    (void)data;
    if (id) {
        *id = g_next_blob_id++;
        g_state.last_created_blob_id = *id;
    }
    g_state.last_created_blob_size = size;
    return 0;
}

int drmModeDestroyPropertyBlob(int fd, uint32_t id) {
    (void)fd;
    (void)id;
    return 0;
}

drmModeObjectPropertiesPtr drmModeObjectGetProperties(int fd, uint32_t object_id, uint32_t object_type) {
    (void)fd;
    (void)object_id;
    (void)object_type;
    auto* props = alloc_one<drmModeObjectProperties>();
    props->count_props = 3;
    props->props = static_cast<uint32_t*>(std::calloc(3, sizeof(uint32_t)));
    props->prop_values = static_cast<uint64_t*>(std::calloc(3, sizeof(uint64_t)));
    props->props[0] = 1;
    props->props[1] = 2;
    props->props[2] = 3;
    return props;
}

drmModePropertyPtr drmModeGetProperty(int fd, uint32_t property_id) {
    (void)fd;
    auto* prop = alloc_one<drmModePropertyRes>();
    prop->prop_id = property_id;
    if (property_id == 1) {
        std::strncpy(prop->name, "DEGAMMA_LUT", sizeof(prop->name) - 1);
    } else if (property_id == 2) {
        std::strncpy(prop->name, "CTM", sizeof(prop->name) - 1);
    } else if (property_id == 3) {
        std::strncpy(prop->name, "GAMMA_LUT", sizeof(prop->name) - 1);
    }
    return prop;
}

void drmModeFreeProperty(drmModePropertyPtr ptr) {
    std::free(ptr);
}

void drmModeFreeObjectProperties(drmModeObjectPropertiesPtr ptr) {
    if (!ptr) {
        return;
    }
    std::free(ptr->props);
    std::free(ptr->prop_values);
    std::free(ptr);
}

int drmModeObjSetPropertyBlob(int fd, uint32_t object_id, uint32_t object_type, uint32_t property_id, uint32_t blob_id) {
    (void)fd;
    (void)object_id;
    (void)object_type;
    if (property_id == 1) {
        g_state.last_set_color_degamma_blob = blob_id;
    } else if (property_id == 2) {
        g_state.last_set_color_ctm_blob = blob_id;
    } else if (property_id == 3) {
        g_state.last_set_color_gamma_blob = blob_id;
    }
    return 0;
}

}
