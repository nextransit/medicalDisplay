/**
 * @file drm_display.h
 * @brief Linux DRM/KMS Display Interface
 * 
 * Low-level DRM/KMS interface for medical display control
 */

#ifndef DRM_DISPLAY_H
#define DRM_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DRM File Descriptor
// ============================================================================
typedef int DRM_FileDescriptor;

// ============================================================================
// DRM Resources
// ============================================================================
typedef struct {
    int fd;
    
    uint32_t* encoders;
    int encoder_count;
    
    uint32_t* connectors;
    int connector_count;
    
    uint32_t* crtcs;
    int crtc_count;
    
    uint32_t* planes;
    int plane_count;
    
    uint32_t* fb_ids;
    int fb_count;
    
    uint32_t min_width, max_width;
    uint32_t min_height, max_height;
} DRM_Resources;

// ============================================================================
// Display Mode
// ============================================================================
typedef struct {
    uint32_t clock;            // Pixel clock in kHz
    uint16_t h_display;        // Horizontal display size
    uint16_t h_sync_start;
    uint16_t h_sync_end;
    uint16_t h_total;
    uint16_t h_skew;
    
    uint16_t v_display;        // Vertical display size
    uint16_t v_sync_start;
    uint16_t v_sync_end;
    uint16_t v_total;
    uint16_t v_scan;          // Scan mode
    uint32_t vrefresh;        // Vertical refresh rate
    
    bool interlaced;
    bool double_scan;
    
    char name[32];
} DRM_Mode;

// ============================================================================
// Framebuffer
// ============================================================================
typedef struct {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t handle;          // GEM handle
    uint32_t format;          // FourCC format
    size_t size;
    
    void* mapped_ptr;         // Mapped CPU pointer
} DRM_FrameBuffer;

// ============================================================================
// Color Management Properties
// ============================================================================

// DRM color space (from kernel 6.1+)
typedef enum {
    DRM_COLORSPACE_DEFAULT = 0,
    DRM_COLORSPACE_SRGB,
    DRM_COLORSPACE_BT2020,
    DRM_COLORSPACE_DCI_P3,
    DRM_COLORSPACE_SCRGB,
    DRM_COLORSPACE_ADOBERGB,
    DRM_COLORSPACE_BT2020_YUV,
    // Medical-specific
    DRM_COLORSPACE_DICOM_GSDF,
} DRM_ColorSpace;

// Gamma ramp (for legacy and GSDF)
typedef struct {
    uint16_t red[256];        // 8-bit or 10-bit
    uint16_t green[256];
    uint16_t blue[256];
    uint16_t size;            // Number of entries
} DRM_GammaRamp;

// LUT blob (for 12-bit GSDF)
typedef struct {
    uint32_t blob_id;         // DRM blob ID
    uint32_t size;            // LUT size
    uint32_t bit_depth;        // 8, 10, or 12
} DRM_LUTBlob;

// ============================================================================
// CRTC State
// ============================================================================
typedef struct {
    uint32_t crtc_id;
    uint32_t encoder_id;
    uint32_t mode_id;         // Mode blob ID
    
    uint32_t* plane_ids;
    int plane_count;
    
    DRM_ColorSpace colorspace;
    
    // Gamma/degamma LUTs
    uint32_t degamma_blob_id;
    uint32_t ctm_blob_id;     // Color transform matrix
    uint32_t gamma_blob_id;
    
    bool mode_valid;
    DRM_Mode mode;
} DRM_CRTCState;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Open DRM device
 * @param device_path Device path (e.g., "/dev/dri/card0")
 * @return DRM file descriptor, -1 on failure
 */
int drm_open(const char* device_path);

/**
 * @brief Close DRM device
 * @param fd DRM file descriptor
 */
void drm_close(int fd);

/**
 * @brief Get DRM resources
 * @param fd DRM file descriptor
 * @param resources Output resources structure
 * @return 0 on success
 */
int drm_get_resources(int fd, DRM_Resources* resources);

/**
 * @brief Free DRM resources
 */
void drm_free_resources(DRM_Resources* resources);

/**
 * @brief Get connector info
 * @param fd DRM file descriptor
 * @param connector_id Connector ID
 * @param encoder_id Output current encoder
 * @param connection Output connection status
 * @param modes Output mode list
 * @param mode_count Output mode count
 * @return 0 on success
 */
int drm_get_connector(int fd, uint32_t connector_id, uint32_t* encoder_id,
                      bool* connection, DRM_Mode** modes, int* mode_count);

/**
 * @brief Get CRTC info
 */
int drm_get_crtc(int fd, uint32_t crtc_id, DRM_CRTCState* state);

/**
 * @brief Set CRTC
 * @param fd DRM file descriptor
 * @param crtc_id CRTC ID
 * @param fb_id Framebuffer ID
 * @param connector_id Connector ID
 * @param mode Mode to set (NULL to disable)
 * @return 0 on success
 */
int drm_set_crtc(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t connector_id,
                 const DRM_Mode* mode);

/**
 * @brief Create framebuffer
 * @param fd DRM file descriptor
 * @param width Width in pixels
 * @param height Height in pixels
 * @param format FourCC format (e.g., DRM_FORMAT_XRGB8888)
 * @param handles GEM handles
 * @param pitches Line pitches
 * @param offsets Per-plane offsets
 * @param fb_id Output framebuffer ID
 * @return 0 on success
 */
int drm_add_fb(int fd, uint32_t width, uint32_t height, uint32_t format,
               const uint32_t* handles, const uint32_t* pitches,
               const uint32_t* offsets, uint32_t* fb_id);

/**
 * @brief Create framebuffer with modifiers (for NV12, etc.)
 */
int drm_add_fb2(int fd, uint32_t width, uint32_t height, uint32_t format,
                const uint32_t* handles, const uint32_t* strides,
                const uint32_t* offsets, const uint64_t* modifiers,
                uint32_t* fb_id, uint32_t flags);

/**
 * @brief Remove framebuffer
 */
int drm_rm_fb(int fd, uint32_t fb_id);

/**
 * @brief Page flip (async)
 * @param fd DRM file descriptor
 * @param crtc_id CRTC ID
 * @param fb_id Framebuffer ID
 * @param flags Flags (e.g., DRM_MODE_PAGE_FLIP_EVENT)
 * @param user_data User data returned in event
 * @return 0 on success
 */
int drm_mode_set_plane(int fd, uint32_t plane_id, uint32_t crtc_id,
                        uint32_t fb_id, uint32_t flags,
                        int32_t crtc_x, int32_t crtc_y,
                        uint32_t crtc_w, uint32_t crtc_h,
                        uint32_t src_x, uint32_t src_y,
                        uint32_t src_w, uint32_t src_h);

int drm_page_flip(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t flags,
                  void* user_data);

/**
 * @brief Set gamma ramp (legacy)
 */
int drm_mode_gamma_set(int fd, uint32_t crtc_id, uint16_t size,
                       uint16_t* red, uint16_t* green, uint16_t* blue);

/**
 * @brief Get gamma ramp size
 */
int drm_mode_get_gamma_size(int fd, uint32_t crtc_id);

/**
 * @brief Create gamma LUT blob (kernel 6.1+)
 * @param lut_data LUT data (size = lut_size * 3)
 * @param lut_size Number of entries
 * @param bit_depth Bits per channel (8, 10, 12)
 * @param blob_id Output blob ID
 * @return 0 on success
 */
int drm_mode_create_gamma_lut(int fd, const uint16_t* lut_data, uint32_t lut_size,
                              uint32_t bit_depth, uint32_t* blob_id);

/**
 * @brief Set CRTC color properties (kernel 6.1+)
 * @param colorspace DRM color space
 * @param degamma_blob Degamma LUT blob ID (0 to disable)
 * @param ctm_blob Color transform matrix blob ID (0 to disable)
 * @param gamma_blob Gamma LUT blob ID (0 to disable)
 */
int drm_mode_set_color(int fd, uint32_t crtc_id, DRM_ColorSpace colorspace,
                        uint32_t degamma_blob, uint32_t ctm_blob, uint32_t gamma_blob);

/**
 * @brief Create color transform matrix blob
 * @param matrix 3x3 color transform matrix (row-major)
 * @param blob_id Output blob ID
 */
int drm_mode_create_ctm_blob(int fd, const float* matrix, uint32_t* blob_id);

/**
 * @brief Destroy blob
 */
int drm_mode_destroy_blob(int fd, uint32_t blob_id);

/**
 * @brief Get plane info
 */
int drm_get_plane(int fd, uint32_t plane_id, uint32_t* crtc_id, uint32_t* fb_id,
                  uint32_t* possible_crtcs, uint32_t* formats, int* format_count);

/**
 * @brief Allocate DMA-BUF from DRM framebuffer
 */
int drm_prime_fd_to_handle(int fd, int prime_fd, uint32_t* handle);

/**
 * @brief Export DRM handle as DMA-BUF fd
 */
int drm_prime_handle_to_fd(int fd, uint32_t handle, uint32_t flags, int* prime_fd);

/**
 * @brief Wait for flip event
 * @param fd DRM file descriptor
 * @param timeout_ms Timeout in milliseconds
 * @param user_data Output user data from event
 * @return 0 on success, -1 on timeout
 */
int drm_wait_vblank(int fd, uint32_t crtc_id, uint32_t flags, void* user_data);

/**
 * @brief Enable vblank events
 */
int drm_vblank_on(int fd, uint32_t crtc_id);

/**
 * @brief Disable vblank events
 */
int drm_vblank_off(int fd, uint32_t crtc_id);

/**
 * @brief Sync file for explicit fencing
 */
int drm_syncobj_create(int fd, uint32_t flags, uint32_t* handle);
int drm_syncobj_destroy(int fd, uint32_t handle);
int drm_syncobj_handle_to_fd(int fd, uint32_t handle, int* fd_out);
int drm_syncobj_fd_to_handle(int fd, int fd_in, uint32_t* handle_out);
int drm_syncobj_wait(int fd, uint32_t* handles, int count, int64_t timeout_ns, 
                      uint32_t flags, uint32_t* signaled);
int drm_syncobj_reset(int fd, const uint32_t* handles, int count);
int drm_syncobj_signal(int fd, const uint32_t* handles, int count);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Get fourcc format from string
 */
uint32_t drm_format_from_string(const char* str);

/**
 * @brief Get format string from fourcc
 */
const char* drm_format_to_string(uint32_t format);

/**
 * @brief Check if format supports modifiers
 */
bool drm_format_supports_modifier(uint32_t format, uint64_t modifier);

/**
 * @brief Set dumb buffer (simple framebuffer allocation)
 */
int drm_set_dumb_buffer(int fd, uint32_t width, uint32_t height, uint32_t bpp,
                        uint32_t* handle, uint32_t* pitch, uint32_t* size, void** ptr);

/**
 * @brief Destroy dumb buffer
 */
int drm_destroy_dumb_buffer(int fd, uint32_t handle);

/**
 * @brief Get device name (for logging/debugging)
 */
const char* drm_get_device_name(int fd);

#ifdef __cplusplus
}
#endif

#endif // DRM_DISPLAY_H
