#ifndef TESTS_SUPPORT_DRM_MOCK_H
#define TESTS_SUPPORT_DRM_MOCK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t last_page_flip_crtc;
    uint32_t last_page_flip_fb;
    uintptr_t last_page_flip_user_data;
    uint32_t last_set_color_degamma_blob;
    uint32_t last_set_color_ctm_blob;
    uint32_t last_set_color_gamma_blob;
    uint32_t last_created_blob_id;
    size_t last_created_blob_size;
    uint16_t last_gamma_size;
} DrmMockState;

void drm_mock_reset(void);
const DrmMockState* drm_mock_state(void);

#ifdef __cplusplus
}
#endif

#endif
