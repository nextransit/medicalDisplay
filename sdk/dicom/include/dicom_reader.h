/**
 * @file dicom_reader.h
 * @brief DICOM Medical Image Reader
 * 
 * Supports DICOM Part 3, 4, 10, 14 specifications
 */

#ifndef DICOM_READER_H
#define DICOM_READER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DICOM Transfer Syntaxes
// ============================================================================
typedef enum {
    DICOM_TRANSFER_IMPLICIT_VR_LITTLE_ENDIAN = 0x00000001,
    DICOM_TRANSFER_EXPLICIT_VR_LITTLE_ENDIAN = 0x00000010,
    DICOM_TRANSFER_EXPLICIT_VR_BIG_ENDIAN    = 0x00000011,
    
    // JPEG
    DICOM_TRANSFER_JPEG_BASELINE_1           = 0x00000020,
    DICOM_TRANSFER_JPEG_EXTENDED_2_4        = 0x00000021,
    DICOM_TRANSFER_JPEG_LOSSLESS            = 0x00000022,
    DICOM_TRANSFER_JPEG_LS_LOSSLESS        = 0x00000024,
    DICOM_TRANSFER_JPEG_LS_LOSSY            = 0x00000025,
    
    // JPEG 2000
    DICOM_TRANSFER_JPEG2000_LOSSLESS        = 0x00000030,
    DICOM_TRANSFER_JPEG2000_LOSSY           = 0x00000031,
    
    // RLE
    DICOM_TRANSFER_RLE_LOSSLESS             = 0x00000101,
    
    // MPEG
    DICOM_TRANSFER_MPEG2_MAIN_PROFILE       = 0x00000040,
    DICOM_TRANSFER_MPEG4_AVC_H264_HIGH_PROFILE = 0x00000041,
} DICOM_TransferSyntax;

// ============================================================================
// DICOM Dataset Handle
// ============================================================================
typedef void* DICOM_Dataset;

// ============================================================================
// DICOM Tags (Group, Element)
// ============================================================================
#define DICOM_TAG(group, elem) (((uint32_t)(group) << 16) | (uint16_t)(elem))

// Common Tags
#define DICOM_TAG_MODALITY                DICOM_TAG(0x0008, 0x0060)
#define DICOM_TAG_SOP_CLASS_UID           DICOM_TAG(0x0008, 0x0016)
#define DICOM_TAG_SOP_INSTANCE_UID        DICOM_TAG(0x0008, 0x0018)
#define DICOM_TAG_STUDY_DATE               DICOM_TAG(0x0008, 0x0020)
#define DICOM_TAG_SERIES_DATE              DICOM_TAG(0x0008, 0x0021)
#define DICOM_TAG_STUDY_TIME               DICOM_TAG(0x0008, 0x0030)
#define DICOM_TAG_STUDY_DESCRIPTION        DICOM_TAG(0x0008, 0x1030)
#define DICOM_TAG_SERIES_DESCRIPTION       DICOM_TAG(0x0008, 0x103E)
#define DICOM_TAG_INSTITUTION_NAME         DICOM_TAG(0x0008, 0x0080)
#define DICOM_TAG_REF_PHYSICIAN           DICOM_TAG(0x0008, 0x0090)
#define DICOM_TAG_PATIENT_NAME             DICOM_TAG(0x0010, 0x0010)
#define DICOM_TAG_PATIENT_ID               DICOM_TAG(0x0010, 0x0020)
#define DICOM_TAG_PATIENT_BIRTH_DATE       DICOM_TAG(0x0010, 0x0030)
#define DICOM_TAG_PATIENT_SEX              DICOM_TAG(0x0010, 0x0040)
#define DICOM_TAG_BODY_PART                DICOM_TAG(0x0018, 0x0015)
#define DICOM_TAG_PROTOCOL_NAME           DICOM_TAG(0x0018, 0x1030)
#define DICOM_TAG_SEQUENCE_NAME           DICOM_TAG(0x0018, 0x6011)
#define DICOM_TAG_SLICE_THICKNESS         DICOM_TAG(0x0018, 0x0050)
#define DICOM_TAG_KVP                      DICOM_TAG(0x0018, 0x0060)
#define DICOM_TAG_SPATIAL_RESOLUTION       DICOM_TAG(0x0018, 0x0050)

// Image Pixel Tags
#define DICOM_TAG_ROWS                     DICOM_TAG(0x0028, 0x0010)
#define DICOM_TAG_COLUMNS                  DICOM_TAG(0x0028, 0x0011)
#define DICOM_TAG_BITS_ALLOCATED           DICOM_TAG(0x0028, 0x0100)
#define DICOM_TAG_BITS_STORED             DICOM_TAG(0x0028, 0x0101)
#define DICOM_TAG_HIGH_BIT                 DICOM_TAG(0x0028, 0x0102)
#define DICOM_TAG_PIXEL_REPRESENTATION      DICOM_TAG(0x0028, 0x0103)
#define DICOM_TAG_PHOTOMETRIC_INTERPRETATION DICOM_TAG(0x0028, 0x0004)
#define DICOM_TAG_PIXEL_DATA               DICOM_TAG(0x7FE0, 0x0010)

// Windowing Tags
#define DICOM_TAG_WINDOW_CENTER            DICOM_TAG(0x0028, 0x1050)
#define DICOM_TAG_WINDOW_WIDTH             DICOM_TAG(0x0028, 0x1051)
#define DICOM_TAG_WINDOW_CENTER_EXPLAIN    DICOM_TAG(0x0028, 0x1055)

// LUT Tags
#define DICOM_TAG_MODALITY_LUT_SEQUENCE    DICOM_TAG(0x0028, 0x3000)
#define DICOM_TAG_VOI_LUT_SEQUENCE         DICOM_TAG(0x0028, 0x3010)
#define DICOM_TAG_LUT_DESCRIPTOR           DICOM_TAG(0x0028, 0x3002)
#define DICOM_TAG_LUT_DATA                 DICOM_TAG(0x0028, 0x3006)
#define DICOM_TAG_LUT_EXPLAIN              DICOM_TAG(0x0028, 0x3004)

// Presentation Tags (Part 14)
#define DICOM_TAG_PRESENTATION_LUT_SEQUENCE DICOM_TAG(0x2050, 0x0010)
#define DICOM_TAG_INVERSE_TABLE            DICOM_TAG(0x2050, 0x0020)

// ============================================================================
// DICOM Value Representations
// ============================================================================
typedef enum {
    DICOM_VR_AE, DICOM_VR_AS, DICOM_VR_AT, DICOM_VR_CS, DICOM_VR_DA,
    DICOM_VR_DS, DICOM_VR_DT, DICOM_VR_FL, DICOM_VR_FD, DICOM_VR_IS,
    DICOM_VR_LO, DICOM_VR_LT, DICOM_VR_OB, DICOM_VR_OD, DICOM_VR_OF,
    DICOM_VR_OW, DICOM_VR_PN, DICOM_VR_SH, DICOM_VR_SL, DICOM_VR_SS,
    DICOM_VR_ST, DICOM_VR_TM, DICOM_VR_UC, DICOM_VR_UI, DICOM_VR_UL,
    DICOM_VR_UN, DICOM_VR_UR, DICOM_VR_US, DICOM_VR_UT,
    DICOM_VR_SQ  // Sequence
} DICOM_ValueRepresentation;

// ============================================================================
// Pixel Data Info
// ============================================================================
typedef struct {
    uint32_t rows;
    uint32_t columns;
    uint32_t bits_allocated;
    uint32_t bits_stored;
    uint32_t high_bit;
    uint32_t pixel_representation;  // 0=unsigned, 1=signed
    uint32_t samples_per_pixel;     // 1=monochrome, 3=RGB
    const char* photometric_interp;  // MONOCHROME1, MONOCHROME2, RGB, etc.
    
    // Actual pixel data
    const void* pixel_data;
    size_t pixel_data_size;
    
    // Decoded (if applicable)
    float* float_data;  // Normalized to [0, 1]
} DICOM_PixelData;

// ============================================================================
// DICOM Metadata
// ============================================================================
typedef struct {
    // Patient
    char patient_name[64];
    char patient_id[64];
    char patient_birth_date[16];
    char patient_sex[8];
    
    // Study
    char study_instance_uid[128];
    char study_date[16];
    char study_time[16];
    char study_description[128];
    
    // Series
    char series_instance_uid[128];
    char modality[16];        // CT, MR, US, DX, etc.
    char series_description[128];
    int series_number;
    
    // Instance
    char sop_instance_uid[128];
    char sop_class_uid[128];
    int instance_number;
    
    // Image Position
    float image_position[3];  // Patient coordinate system
    float image_orientation[6];
    float slice_location;
    float slice_thickness;
    
    // Pixel Intensity
    float rescale_slope;
    float rescale_intercept;
    float window_center;
    float window_width;
    
    // Body Part
    char body_part[32];
    
    // Technical
    float kvp;
    int number_of_frames;
} DICOM_Metadata;

// ============================================================================
// DICOM Reader API
// ============================================================================

/**
 * @brief Open DICOM file
 * @param file_path Path to DICOM file
 * @return DICOM dataset handle, NULL on failure
 */
DICOM_Dataset dicom_open(const char* file_path);

/**
 * @brief Close DICOM dataset
 * @param dataset Dataset handle
 */
void dicom_close(DICOM_Dataset dataset);

/**
 * @brief Get transfer syntax of dataset
 */
DICOM_TransferSyntax dicom_get_transfer_syntax(DICOM_Dataset dataset);

/**
 * @brief Read string tag value
 * @param dataset Dataset handle
 * @param tag DICOM tag
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return 0 on success
 */
int dicom_read_string(DICOM_Dataset dataset, uint32_t tag, char* buffer, size_t buffer_size);

/**
 * @brief Read uint16 tag value
 */
int dicom_read_uint16(DICOM_Dataset dataset, uint32_t tag, uint16_t* value);

/**
 * @brief Read uint32 tag value
 */
int dicom_read_uint32(DICOM_Dataset dataset, uint32_t tag, uint32_t* value);

/**
 * @brief Read float tag value
 */
int dicom_read_float(DICOM_Dataset dataset, uint32_t tag, float* value);

/**
 * @brief Read double tag value
 */
int dicom_read_double(DICOM_Dataset dataset, uint32_t tag, double* value);

/**
 * @brief Read window/level information
 * @param dataset Dataset handle
 * @param center Output window center (may be NULL)
 * @param width Output window width (may be NULL)
 */
void dicom_read_window_level(DICOM_Dataset dataset, float* center, float* width);

/**
 * @brief Read pixel data
 * @param dataset Dataset handle
 * @param pixel_info Output pixel information
 * @return 0 on success
 */
int dicom_read_pixels(DICOM_Dataset dataset, DICOM_PixelData* pixel_info);

/**
 * @brief Get HU (Hounsfield Unit) value from raw pixel
 * @param raw_pixel Raw pixel value
 * @param slope Rescale slope
 * @param intercept Rescale intercept
 * @return HU value
 */
float dicom_pixel_to_hu(int raw_pixel, float slope, float intercept);

/**
 * @brief Apply modality LUT to raw pixel
 * @param raw_pixel Raw pixel value
 * @param lut_data Modality LUT data
 * @param lut_entries Number of LUT entries
 * @return Modality LUT output value
 */
int dicom_apply_modality_lut(int raw_pixel, const uint16_t* lut_data, int lut_entries);

/**
 * @brief Extract metadata
 * @param dataset Dataset handle
 * @param metadata Output metadata structure
 */
void dicom_extract_metadata(DICOM_Dataset dataset, DICOM_Metadata* metadata);

/**
 * @brief Get modality from SOP Class UID
 * @param sop_class_uid SOP Class UID string
 * @return Modality string (e.g., "CT", "MR")
 */
const char* dicom_sop_class_to_modality(const char* sop_class_uid);

/**
 * @brief Check if dataset is monochrome
 */
bool dicom_is_monochrome(DICOM_Dataset dataset);

/**
 * @brief Check if dataset needs inversion (MONOCHROME1)
 */
bool dicom_needs_inversion(DICOM_Dataset dataset);

/**
 * @brief Read multi-frame DICOM
 * @param dataset Dataset handle
 * @param frame_index Frame index (0-based)
 * @param pixel_info Output pixel information for single frame
 */
int dicom_read_frame(DICOM_Dataset dataset, int frame_index, DICOM_PixelData* pixel_info);

/**
 * @brief Get number of frames
 */
int dicom_get_frame_count(DICOM_Dataset dataset);

#ifdef __cplusplus
}
#endif

#endif // DICOM_READER_H
