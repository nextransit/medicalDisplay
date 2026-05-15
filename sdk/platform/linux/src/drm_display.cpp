/**
 * @file drm_display.cpp
 * @brief Linux DRM/KMS Display Interface Implementation
 */

#include "drm_display.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DRM Device
// ============================================================================

int drm_open(const char* device_path) {
    if (!device_path) {
        // Try default devices
        const char* default_paths[] = {
            "/dev/dri/card0",
            "/dev/dri/card1",
            "/dev/dri/renderD128",
            NULL
        };
        
        for (int i = 0; default_paths[i]; i++) {
            int fd = open(default_paths[i], O_RDWR | O_CLOEXEC);
            if (fd >= 0) {
                // Check if it's a render node or card
                drmVersion* version = drmGetVersion(fd);
                if (version) {
                    drmFreeVersion(version);
                    return fd;
                }
                close(fd);
            }
        }
        return -1;
    }
    
    return open(device_path, O_RDWR | O_CLOEXEC);
}

void drm_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

const char* drm_get_device_name(int fd) {
    if (fd < 0) return "Unknown";
    
    static char name[256] = {0};
    drmVersion* version = drmGetVersion(fd);
    if (version) {
        snprintf(name, sizeof(name), "%s %s", version->name, version->date);
        drmFreeVersion(version);
        return name;
    }
    return "Unknown";
}

// ============================================================================
// Resource Discovery
// ============================================================================

int drm_get_resources(int fd, DRM_Resources* resources) {
    if (fd < 0 || !resources) return -1;
    
    drmModeResPtr res = drmModeGetResources(fd);
    if (!res) {
        return -1;
    }
    
    resources->fd = fd;
    resources->min_width = res->min_width;
    resources->max_width = res->max_width;
    resources->min_height = res->min_height;
    resources->max_height = res->max_height;
    
    // Count encoders
    resources->encoder_count = 0;
    for (int i = 0; i < res->count_encoders; i++) {
        if (res->encoders[i] != 0) resources->encoder_count++;
    }
    
    // Count connectors
    resources->connector_count = 0;
    for (int i = 0; i < res->count_connectors; i++) {
        if (res->connectors[i] != 0) resources->connector_count++;
    }
    
    // Count CRTCs
    resources->crtc_count = 0;
    for (int i = 0; i < res->count_crtcs; i++) {
        if (res->crtcs[i] != 0) resources->crtc_count++;
    }
    
    // Allocate and copy
    if (resources->encoder_count > 0) {
        resources->encoders = (uint32_t*)malloc(resources->encoder_count * sizeof(uint32_t));
        int idx = 0;
        for (int i = 0; i < res->count_encoders; i++) {
            if (res->encoders[i] != 0) {
                resources->encoders[idx++] = res->encoders[i];
            }
        }
    }
    
    if (resources->connector_count > 0) {
        resources->connectors = (uint32_t*)malloc(resources->connector_count * sizeof(uint32_t));
        int idx = 0;
        for (int i = 0; i < res->count_connectors; i++) {
            if (res->connectors[i] != 0) {
                resources->connectors[idx++] = res->connectors[i];
            }
        }
    }
    
    if (resources->crtc_count > 0) {
        resources->crtcs = (uint32_t*)malloc(resources->crtc_count * sizeof(uint32_t));
        int idx = 0;
        for (int i = 0; i < res->count_crtcs; i++) {
            if (res->crtcs[i] != 0) {
                resources->crtcs[idx++] = res->crtcs[i];
            }
        }
    }
    
    // Get planes
    drmModePlaneResPtr planes = drmModeGetPlaneResources(fd);
    if (planes) {
        resources->plane_count = planes->count_planes;
        if (resources->plane_count > 0) {
            resources->planes = (uint32_t*)malloc(resources->plane_count * sizeof(uint32_t));
            memcpy(resources->planes, planes->planes, resources->plane_count * sizeof(uint32_t));
        }
        drmModeFreePlaneResources(planes);
    }
    
    drmModeFreeResources(res);
    return 0;
}

void drm_free_resources(DRM_Resources* resources) {
    if (!resources) return;
    
    if (resources->encoders) free(resources->encoders);
    if (resources->connectors) free(resources->connectors);
    if (resources->crtcs) free(resources->crtcs);
    if (resources->planes) free(resources->planes);
    
    memset(resources, 0, sizeof(DRM_Resources));
}

int drm_get_connector(int fd, uint32_t connector_id, uint32_t* encoder_id,
                      bool* connection, DRM_Mode** modes, int* mode_count) {
    if (fd < 0 || !connector_id) return -1;
    
    drmModeConnectorPtr conn = drmModeGetConnector(fd, connector_id);
    if (!conn) return -1;
    
    if (connection) {
        *connection = (conn->connection == DRM_MODE_CONNECTED);
    }
    
    if (encoder_id && conn->count_encoders > 0) {
        *encoder_id = conn->encoder_id;
    }
    
    if (modes && mode_count && conn->count_modes > 0) {
        *mode_count = conn->count_modes;
        *modes = (DRM_Mode*)malloc(conn->count_modes * sizeof(DRM_Mode));
        
        for (int i = 0; i < conn->count_modes; i++) {
            drmModeModeInfoPtr info = &conn->modes[i];
            DRM_Mode* mode = &((*modes)[i]);
            
            mode->clock = info->clock;
            mode->h_display = info->hdisplay;
            mode->h_sync_start = info->hsync_start;
            mode->h_sync_end = info->hsync_end;
            mode->h_total = info->htotal;
            mode->h_skew = info->hskew;
            mode->v_display = info->vdisplay;
            mode->v_sync_start = info->vsync_start;
            mode->v_sync_end = info->vsync_end;
            mode->v_total = info->vtotal;
            mode->v_scan = info->vscan;
            mode->vrefresh = info->vrefresh;
            mode->interlaced = (info->flags & DRM_MODE_INTERLACE) != 0;
            mode->double_scan = (info->flags & DRM_MODE_DBLSCAN) != 0;
            
            snprintf(mode->name, sizeof(mode->name), "%dx%d@%d",
                     info->hdisplay, info->vdisplay, info->vrefresh);
        }
    }
    
    drmModeFreeConnector(conn);
    return 0;
}

int drm_get_crtc(int fd, uint32_t crtc_id, DRM_CRTCState* state) {
    if (fd < 0 || !crtc_id || !state) return -1;
    
    drmModeCrtcPtr crtc = drmModeGetCrtc(fd, crtc_id);
    if (!crtc) return -1;
    
    state->crtc_id = crtc->crtc_id;
    state->encoder_id = crtc->encoder_id;
    state->mode_valid = crtc->mode_valid;
    
    if (crtc->mode_valid) {
        state->mode.clock = crtc->mode.clock;
        state->mode.h_display = crtc->mode.hdisplay;
        state->mode.h_total = crtc->mode.htotal;
        state->mode.v_display = crtc->mode.vdisplay;
        state->mode.v_total = crtc->mode.vtotal;
        state->mode.vrefresh = crtc->mode.vrefresh;
    }
    
    drmModeFreeCrtc(crtc);
    return 0;
}

// ============================================================================
// CRTC and Plane Control
// ============================================================================

int drm_set_crtc(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t connector_id,
                 const DRM_Mode* mode) {
    if (fd < 0) return -1;
    
    drmModeModeInfo drm_mode;
    if (mode) {
        memset(&drm_mode, 0, sizeof(drm_mode));
        drm_mode.clock = mode->clock;
        drm_mode.hdisplay = mode->h_display;
        drm_mode.hsync_start = mode->h_sync_start;
        drm_mode.hsync_end = mode->h_sync_end;
        drm_mode.htotal = mode->h_total;
        drm_mode.hskew = mode->h_skew;
        drm_mode.vdisplay = mode->v_display;
        drm_mode.vsync_start = mode->v_sync_start;
        drm_mode.vsync_end = mode->v_sync_end;
        drm_mode.vtotal = mode->v_total;
        drm_mode.vscan = mode->v_scan;
        drm_mode.vrefresh = mode->vrefresh;
        
        if (mode->interlaced) drm_mode.flags |= DRM_MODE_INTERLACE;
        if (mode->double_scan) drm_mode.flags |= DRM_MODE_DBLSCAN;
    }
    
    int ret = drmModeSetCrtc(fd, crtc_id, fb_id, 0, 0,
                              mode ? &connector_id : NULL, mode ? 1 : 0,
                              mode ? &drm_mode : NULL);
    
    return (ret == 0) ? 0 : -1;
}

int drm_mode_set_plane(int fd, uint32_t plane_id, uint32_t crtc_id,
                        uint32_t fb_id, uint32_t flags,
                        int32_t crtc_x, int32_t crtc_y,
                        uint32_t crtc_w, uint32_t crtc_h,
                        uint32_t src_x, uint32_t src_y,
                        uint32_t src_w, uint32_t src_h) {
    if (fd < 0) return -1;
    
    int ret = drmModeSetPlane(fd, plane_id, crtc_id, fb_id, flags,
                               crtc_x, crtc_y, crtc_w, crtc_h,
                               src_x, src_y, src_w, src_h);
    
    return (ret == 0) ? 0 : -1;
}

int drm_page_flip(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t flags,
                  void* user_data) {
    if (fd < 0) return -1;
    
    int ret = drmModePageFlip(fd, crtc_id, fb_id, flags, user_data);
    
    return (ret == 0) ? 0 : -1;
}

// ============================================================================
// Framebuffer Management
// ============================================================================

int drm_add_fb(int fd, uint32_t width, uint32_t height, uint32_t format,
               const uint32_t* handles, const uint32_t* pitches,
               const uint32_t* offsets, uint32_t* fb_id) {
    if (fd < 0 || !fb_id) return -1;
    
    int ret = drmModeAddFB2(fd, width, height, format, handles, pitches, offsets, fb_id, 0);
    
    return (ret == 0) ? 0 : -1;
}

int drm_add_fb2(int fd, uint32_t width, uint32_t height, uint32_t format,
                 const uint32_t* handles, const uint32_t* strides,
                 const uint32_t* offsets, const uint64_t* modifiers,
                 uint32_t* fb_id, uint32_t flags) {
    if (fd < 0 || !fb_id) return -1;
    
    int ret = drmModeAddFB2WithModifiers(fd, width, height, format, handles, 
                                          strides, offsets, modifiers, fb_id, flags);
    
    return (ret == 0) ? 0 : -1;
}

int drm_rm_fb(int fd, uint32_t fb_id) {
    if (fd < 0) return -1;
    
    int ret = drmModeRmFB(fd, fb_id);
    return (ret == 0) ? 0 : -1;
}

// ============================================================================
// Gamma and Color Management
// ============================================================================

int drm_mode_gamma_set(int fd, uint32_t crtc_id, uint16_t size,
                       uint16_t* red, uint16_t* green, uint16_t* blue) {
    if (fd < 0) return -1;
    
    int ret = drmModeCrtcSetGamma(fd, crtc_id, size, red, green, blue);
    return (ret == 0) ? 0 : -1;
}

int drm_mode_get_gamma_size(int fd, uint32_t crtc_id) {
    if (fd < 0) return -1;
    
    return drmModeGetCrtcGammaSize(fd, crtc_id);
}

int drm_mode_create_gamma_lut(int fd, const uint16_t* lut_data, uint32_t lut_size,
                               uint32_t bit_depth, uint32_t* blob_id) {
    if (fd < 0 || !lut_data || !blob_id) return -1;
    
    // Calculate total size (R, G, B)
    size_t data_size = lut_size * 3 * sizeof(uint16_t);
    
    // Create blob
    int ret = drmModeCreatePropertyBlob(fd, lut_data, data_size, blob_id);
    
    return (ret == 0) ? 0 : -1;
}

int drm_mode_set_color(int fd, uint32_t crtc_id, DRM_ColorSpace colorspace,
                        uint32_t degamma_blob, uint32_t ctm_blob, uint32_t gamma_blob) {
    if (fd < 0) return -1;
    
    // Get CRTC properties
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, crtc_id, DRM_MODE_OBJECT_CRTC);
    if (!props) return -1;
    
    uint32_t degamma_prop = 0, ctm_prop = 0, gamma_prop = 0;
    bool found_degamma = false, found_ctm = false, found_gamma = false;
    
    for (int i = 0; i < props->count_props; i++) {
        drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[i]);
        if (prop) {
            if (strcmp(prop->name, "DEGAMMA_LUT") == 0) {
                degamma_prop = prop->prop_id;
                found_degamma = true;
            } else if (strcmp(prop->name, "CTM") == 0) {
                ctm_prop = prop->prop_id;
                found_ctm = true;
            } else if (strcmp(prop->name, "GAMMA_LUT") == 0) {
                gamma_prop = prop->prop_id;
                found_gamma = true;
            }
            drmModeFreeProperty(prop);
        }
    }
    
    // Set properties
    if (found_degamma && degamma_blob) {
        drmModeObjSetPropertyBlob(fd, crtc_id, DRM_MODE_OBJECT_CRTC,
                                   degamma_prop, degamma_blob);
    }
    
    if (found_ctm && ctm_blob) {
        drmModeObjSetPropertyBlob(fd, crtc_id, DRM_MODE_OBJECT_CRTC,
                                   ctm_prop, ctm_blob);
    }
    
    if (found_gamma && gamma_blob) {
        drmModeObjSetPropertyBlob(fd, crtc_id, DRM_MODE_OBJECT_CRTC,
                                   gamma_prop, gamma_blob);
    }
    
    drmModeFreeObjectProperties(props);
    return 0;
}

int drm_mode_create_ctm_blob(int fd, const float* matrix, uint32_t* blob_id) {
    if (fd < 0 || !matrix || !blob_id) return -1;
    
    // Convert float matrix to fixed point (drm_mode_color_transform uses 0.16 format)
    struct drm_color_ctm {
        int64_t matrix[9];
    } ctm;
    
    for (int i = 0; i < 9; i++) {
        // Scale to 0.16 fixed point
        double scaled = matrix[i] * (double)(1ULL << 16);
        // Clamp to int64 range
        if (scaled > (double)INT64_MAX) scaled = INT64_MAX;
        if (scaled < (double)INT64_MIN) scaled = INT64_MIN;
        ctm.matrix[i] = (int64_t)scaled;
    }
    
    int ret = drmModeCreatePropertyBlob(fd, &ctm, sizeof(ctm), blob_id);
    
    return (ret == 0) ? 0 : -1;
}

int drm_mode_destroy_blob(int fd, uint32_t blob_id) {
    if (fd < 0) return -1;
    
    int ret = drmModeDestroyPropertyBlob(fd, blob_id);
    return (ret == 0) ? 0 : -1;
}

// ============================================================================
// Sync Objects
// ============================================================================

int drm_syncobj_create(int fd, uint32_t flags, uint32_t* handle) {
    if (fd < 0 || !handle) return -1;
    
    return drmSyncobjCreate(fd, flags, handle);
}

int drm_syncobj_destroy(int fd, uint32_t handle) {
    if (fd < 0) return -1;
    return drmSyncobjDestroy(fd, handle);
}

int drm_syncobj_handle_to_fd(int fd, uint32_t handle, int* fd_out) {
    if (fd < 0 || !fd_out) return -1;
    return drmSyncobjHandleToFd(fd, handle, fd_out);
}

int drm_syncobj_fd_to_handle(int fd, int fd_in, uint32_t* handle_out) {
    if (fd < 0 || !handle_out) return -1;
    return drmSyncobjFdToHandle(fd, fd_in, handle_out);
}

int drm_syncobj_wait(int fd, uint32_t* handles, int count, int64_t timeout_ns,
                      uint32_t flags, uint32_t* signaled) {
    if (fd < 0 || !handles) return -1;
    return drmSyncobjWait(fd, handles, count, timeout_ns, flags, signaled);
}

int drm_syncobj_reset(int fd, const uint32_t* handles, int count) {
    if (fd < 0) return -1;
    return drmSyncobjReset(fd, handles, count);
}

int drm_syncobj_signal(int fd, const uint32_t* handles, int count) {
    if (fd < 0) return -1;
    return drmSyncobjSignal(fd, handles, count);
}

// ============================================================================
// VBlank
// ============================================================================

int drm_wait_vblank(int fd, uint32_t crtc_id, uint32_t flags, void* user_data) {
    if (fd < 0) return -1;
    
    drmVBlankSeq sequence;
    memset(&sequence, 0, sizeof(sequence));
    sequence.request.type = (crtc_id << DRM_VBLANK_HIGH_CRTC_SHIFT) | DRM_VBLANK_RELATIVE;
    sequence.request.sequence = 1;
    sequence.request.signal = (uint64_t)user_data;
    
    int ret = drmWaitVBlank(fd, &sequence);
    return (ret == 0) ? 0 : -1;
}

int drm_vblank_on(int fd, uint32_t crtc_id) {
    if (fd < 0) return -1;
    
    drmVBlankSeq sequence;
    memset(&sequence, 0, sizeof(sequence));
    sequence.request.type = (crtc_id << DRM_VBLANK_HIGH_CRTC_SHIFT) | DRM_VBLANK_EVENT;
    sequence.request.sequence = 0;
    
    return drmWaitVBlank(fd, &sequence);
}

int drm_vblank_off(int fd, uint32_t crtc_id) {
    (void)fd;
    (void)crtc_id;
    // Disable vblank events
    return 0;
}

// ============================================================================
// Dumb Buffer
// ============================================================================

int drm_set_dumb_buffer(int fd, uint32_t width, uint32_t height, uint32_t bpp,
                        uint32_t* handle, uint32_t* pitch, uint32_t* size, void** ptr) {
    if (fd < 0 || !handle) return -1;
    
    struct drm_mode_create_dumb create;
    memset(&create, 0, sizeof(create));
    create.width = width;
    create.height = height;
    create.bpp = bpp;
    
    int ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
    if (ret < 0) return -1;
    
    *handle = create.handle;
    *pitch = create.pitch;
    *size = create.size;
    
    // Map buffer if ptr is provided
    if (ptr) {
        struct drm_mode_map_dumb map;
        memset(&map, 0, sizeof(map));
        map.handle = create.handle;
        
        ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
        if (ret < 0) {
            drmModeRmFB(fd, *handle);
            return -1;
        }
        
        *ptr = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
        if (*ptr == MAP_FAILED) {
            *ptr = NULL;
        }
    }
    
    return 0;
}

int drm_destroy_dumb_buffer(int fd, uint32_t handle) {
    if (fd < 0) return -1;
    
    struct drm_mode_destroy_dumb destroy;
    memset(&destroy, 0, sizeof(destroy));
    destroy.handle = handle;
    
    int ret = drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    return (ret == 0) ? 0 : -1;
}

// ============================================================================
// Format Utilities
// ============================================================================

uint32_t drm_format_from_string(const char* str) {
    if (!str) return 0;
    
    if (strcmp(str, "XR24") == 0 || strcmp(str, "XRGB8888") == 0) return 0x34324258;
    if (strcmp(str, "XB24") == 0 || strcmp(str, "XBGR8888") == 0) return 0x34324258; // Actually GR24
    if (strcmp(str, "RGBA8888") == 0) return 0x34324252;
    if (strcmp(str, "BGRA8888") == 0) return 0x41424742;
    if (strcmp(str, "RGB565") == 0) return 0x36315247;
    if (strcmp(str, "NV12") == 0) return 0x3231564E;
    if (strcmp(str, "YUYV") == 0) return 0x56595559;
    if (strcmp(str, "YVYU") == 0) return 0x55595659;
    
    return 0;
}

const char* drm_format_to_string(uint32_t format) {
    switch (format) {
        case 0x34324258: return "XR24";
        case 0x34324252: return "RGBA8888";
        case 0x41424742: return "BGRA8888";
        case 0x36315247: return "RGB565";
        case 0x3231564E: return "NV12";
        case 0x56595559: return "YUYV";
        default: return "UNKNOWN";
    }
}

#ifdef __cplusplus
}
#endif
