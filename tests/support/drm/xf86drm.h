#ifndef TESTS_SUPPORT_XF86DRM_H
#define TESTS_SUPPORT_XF86DRM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DRM_VBLANK_HIGH_CRTC_SHIFT 1
#define DRM_VBLANK_RELATIVE 0x1
#define DRM_VBLANK_EVENT 0x2

#define DRM_IOCTL_MODE_CREATE_DUMB 0x100
#define DRM_IOCTL_MODE_MAP_DUMB 0x101
#define DRM_IOCTL_MODE_DESTROY_DUMB 0x102

typedef struct _drmVersion {
    char* name;
    char* date;
} drmVersion;

typedef struct {
    struct {
        uint32_t type;
        uint32_t sequence;
        uint64_t signal;
    } request;
} drmVBlankSeq;

struct drm_mode_create_dumb {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
};

struct drm_mode_map_dumb {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
};

struct drm_mode_destroy_dumb {
    uint32_t handle;
};

struct drm_color_ctm {
    int64_t matrix[9];
};

drmVersion* drmGetVersion(int fd);
void drmFreeVersion(drmVersion* version);
int drmIoctl(int fd, unsigned long request, void* arg);
int drmWaitVBlank(int fd, drmVBlankSeq* sequence);
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t* handle);
int drmSyncobjDestroy(int fd, uint32_t handle);
int drmSyncobjHandleToFd(int fd, uint32_t handle, int* fd_out);
int drmSyncobjFdToHandle(int fd, int fd_in, uint32_t* handle_out);
int drmSyncobjWait(int fd, uint32_t* handles, int count, int64_t timeout_ns, uint32_t flags, uint32_t* signaled);
int drmSyncobjReset(int fd, const uint32_t* handles, int count);
int drmSyncobjSignal(int fd, const uint32_t* handles, int count);

#ifdef __cplusplus
}
#endif

#endif
