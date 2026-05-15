#ifndef MEDICALDISPLAY_DISPLAY_ENGINE_INTERNAL_H
#define MEDICALDISPLAY_DISPLAY_ENGINE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

typedef void* Display_Device;
typedef void* Display_Frame;

typedef enum {
    DISPLAY_COLORSPACE_sRGB = 0,
    DISPLAY_COLORSPACE_DCI_P3 = 1,
    DISPLAY_COLORSPACE_Rec709 = 2,
    DISPLAY_COLORSPACE_Rec2020 = 3,
    DISPLAY_COLORSPACE_AdobeRGB = 4,
    DISPLAY_COLORSPACE_DICOM_GSDF = 5,
    DISPLAY_COLORSPACE_NATIVE = 6
} Display_ColorSpace;

typedef enum {
    DISPLAY_GSDF_CT = 0,
    DISPLAY_GSDF_MR,
    DISPLAY_GSDF_DR,
    DISPLAY_GSDF_US,
    DISPLAY_GSDF_PATHOLOGY,
    DISPLAY_GSDF_SURGICAL
} Display_GSDFProfile;

typedef enum {
    DISPLAY_HDR_OFF = 0,
    DISPLAY_HDR_HDR10,
    DISPLAY_HDR_HLG,
    DISPLAY_HDR_DOLBY_VISION,
    DISPLAY_HDR_LOCAL_DIMMING
} Display_HDRMode;

typedef enum {
    DISPLAY_ENHANCE_OFF = 0,
    DISPLAY_ENHANCE_BONE,
    DISPLAY_ENHANCE_LUNG,
    DISPLAY_ENHANCE_VASCULAR,
    DISPLAY_ENHANCE_CELL,
    DISPLAY_ENHANCE_ENDO
} Display_EnhanceType;

typedef struct {
    Display_ColorSpace color_space;
    Display_GSDFProfile gsdf_profile;
    float gamma;
    bool enable_gsdf;
    bool enable_local_enhance;
    float target_luminance;
    float ambient_light_sensor;
    Display_HDRMode hdr_mode;
    float window_width;
    float window_center;
} Display_Config;

typedef struct {
    Display_ColorSpace color_space;
    Display_GSDFProfile gsdf_profile;
    float gamma;
    Display_HDRMode hdr_mode;
    float hdr_max_luminance;
    float hdr_avg_luminance;
    float hdr_min_luminance;
    float window_width;
    float window_center;
    Display_EnhanceType enhance_type;
} Display_State;

typedef struct {
    uint32_t max_width;
    uint32_t max_height;
    int max_bit_depth;
    bool supports_hdr;
    int max_display_count;
} Display_Capabilities;

typedef struct {
    int lut_size;
    const uint16_t* lut_data;
} Display_LUT;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    const uint16_t* data;
} Display_3DLUT;

#define DISPLAY_GAMMA_2_2 2.2f
#define DISPLAY_GAMMA_DICOM 2.0f

float display_luminance_to_jnd(float luminance);
float display_jnd_to_luminance(float jnd);
void display_generate_gsdf_lut(float ambient_luminance, float target_luminance, int bit_depth, uint16_t* output);
Display_GSDFProfile display_get_recommended_gsdf(int modality);
int display_enumerate_devices(Display_Device* device_array, int max_devices);
Display_Device display_open(const char* device_path);
void display_close(Display_Device device);
void display_get_capabilities(Display_Device device, Display_Capabilities* caps);
int display_apply_config(Display_Device device, const Display_Config* config);
void display_get_state(Display_Device device, Display_State* state);
int display_load_lut(Display_Device device, const Display_LUT* lut);
int display_load_3d_lut(Display_Device device, const Display_3DLUT* lut_3d);
void display_switch_gsdf(Display_Device device, Display_GSDFProfile profile);
void display_switch_colorspace(Display_Device device, Display_ColorSpace colorspace);
void display_switch_hdr(Display_Device device, Display_HDRMode mode, const void* metadata);
void display_set_window_level(Display_Device device, float window_width, float window_center);
void display_set_local_enhancement(Display_Device device, Display_EnhanceType enhance_type, bool enabled);
Display_Frame display_frame_create(Display_Device device, uint32_t width, uint32_t height, uint32_t format, const void* data);
void display_frame_destroy(Display_Frame frame);
int display_present(Display_Device device, Display_Frame frame);
void display_present_async(Display_Device device, Display_Frame frame, void* sync_point);
void display_wait(Display_Device device, const void* sync_point);
int display_calibrate(Display_Device device, void* report);
void display_measure_uniformity(Display_Device device, int grid_size, float* output);
const char* display_engine_version(void);

int display_linux_enumerate(Display_Device* device_array, int max_devices);
int display_linux_open(const char* device_path);
void display_linux_close(int fd);
void display_linux_get_caps(int fd, Display_Capabilities* caps);
void display_linux_apply_config(int fd, const Display_Config* config);
void display_linux_load_degamma_lut(int fd, const uint16_t* lut, int size);
void display_linux_set_colorspace(int fd, Display_ColorSpace colorspace);
void display_linux_set_hdr_mode(int fd, Display_HDRMode mode, const void* metadata);
Display_Frame display_linux_create_frame(uint32_t width, uint32_t height, uint32_t format, const void* data);
void display_linux_destroy_frame(Display_Frame frame);
int display_linux_present(int fd, uint32_t crtc_id, Display_Frame frame, const Display_State* state);
void display_linux_present_async(int fd, uint32_t crtc_id, Display_Frame frame, void* sync_point);
void display_linux_wait_sync(const void* sync_point);

#endif
