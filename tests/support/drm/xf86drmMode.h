#ifndef TESTS_SUPPORT_XF86DRMMODE_H
#define TESTS_SUPPORT_XF86DRMMODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DRM_MODE_CONNECTED 1
#define DRM_MODE_INTERLACE 0x1
#define DRM_MODE_DBLSCAN 0x2
#define DRM_MODE_OBJECT_CRTC 0xcccc

typedef struct _drmModeModeInfo {
    uint32_t clock;
    uint16_t hdisplay;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t hskew;
    uint16_t vdisplay;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    uint16_t vscan;
    uint32_t vrefresh;
    uint32_t flags;
} drmModeModeInfo, *drmModeModeInfoPtr;

typedef struct _drmModeRes {
    int count_encoders;
    uint32_t* encoders;
    int count_connectors;
    uint32_t* connectors;
    int count_crtcs;
    uint32_t* crtcs;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
} drmModeRes, *drmModeResPtr;

typedef struct _drmModePlaneRes {
    uint32_t count_planes;
    uint32_t* planes;
} drmModePlaneRes, *drmModePlaneResPtr;

typedef struct _drmModeConnector {
    uint32_t connection;
    int count_encoders;
    uint32_t encoder_id;
    int count_modes;
    drmModeModeInfo* modes;
} drmModeConnector, *drmModeConnectorPtr;

typedef struct _drmModeCrtc {
    uint32_t crtc_id;
    uint32_t encoder_id;
    int mode_valid;
    drmModeModeInfo mode;
} drmModeCrtc, *drmModeCrtcPtr;

typedef struct _drmModeObjectProperties {
    uint32_t count_props;
    uint32_t* props;
    uint64_t* prop_values;
} drmModeObjectProperties, *drmModeObjectPropertiesPtr;

typedef struct _drmModePropertyRes {
    uint32_t prop_id;
    char name[32];
} drmModePropertyRes, *drmModePropertyPtr;

drmModeResPtr drmModeGetResources(int fd);
void drmModeFreeResources(drmModeResPtr ptr);
drmModePlaneResPtr drmModeGetPlaneResources(int fd);
void drmModeFreePlaneResources(drmModePlaneResPtr ptr);
drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connector_id);
void drmModeFreeConnector(drmModeConnectorPtr ptr);
drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t crtc_id);
void drmModeFreeCrtc(drmModeCrtcPtr ptr);
int drmModeSetCrtc(int fd, uint32_t crtc_id, uint32_t buffer_id, uint32_t x, uint32_t y, const uint32_t* connectors, int count, const drmModeModeInfo* mode);
int drmModeSetPlane(int fd, uint32_t plane_id, uint32_t crtc_id, uint32_t fb_id, uint32_t flags, int32_t crtc_x, int32_t crtc_y, uint32_t crtc_w, uint32_t crtc_h, uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h);
int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t flags, void* user_data);
int drmModeAddFB2(int fd, uint32_t width, uint32_t height, uint32_t pixel_format, const uint32_t* bo_handles, const uint32_t* pitches, const uint32_t* offsets, uint32_t* buf_id, uint32_t flags);
int drmModeAddFB2WithModifiers(int fd, uint32_t width, uint32_t height, uint32_t pixel_format, const uint32_t* bo_handles, const uint32_t* pitches, const uint32_t* offsets, const uint64_t* modifier, uint32_t* buf_id, uint32_t flags);
int drmModeRmFB(int fd, uint32_t buffer_id);
int drmModeCrtcSetGamma(int fd, uint32_t crtc_id, uint32_t size, uint16_t* red, uint16_t* green, uint16_t* blue);
int drmModeGetCrtcGammaSize(int fd, uint32_t crtc_id);
int drmModeCreatePropertyBlob(int fd, const void* data, size_t size, uint32_t* id);
int drmModeDestroyPropertyBlob(int fd, uint32_t id);
drmModeObjectPropertiesPtr drmModeObjectGetProperties(int fd, uint32_t object_id, uint32_t object_type);
drmModePropertyPtr drmModeGetProperty(int fd, uint32_t property_id);
void drmModeFreeProperty(drmModePropertyPtr ptr);
void drmModeFreeObjectProperties(drmModeObjectPropertiesPtr ptr);
int drmModeObjSetPropertyBlob(int fd, uint32_t object_id, uint32_t object_type, uint32_t property_id, uint32_t blob_id);

#ifdef __cplusplus
}
#endif

#endif
