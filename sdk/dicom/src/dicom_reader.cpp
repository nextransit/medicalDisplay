/**
 * @file dicom_reader.cpp
 * @brief DICOM Medical Image Reader Implementation
 */

#include "dicom_reader.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <zlib.h>

#ifdef _WIN32
    #include <fcntl.h>
    #include <io.h>
#endif

// ============================================================================
// Internal Structures (Forward Declaration)
// ============================================================================

#define MAX_METADATA_ELEMENTS 256

// [P0-FIX] 添加安全常量限制
static const size_t MAX_ELEMENT_SIZE = 10 * 1024 * 1024;     // 10MB 元数据元素上限
static const size_t MAX_PIXEL_DATA_SIZE = 500 * 1024 * 1024; // 500MB 像素数据上限

typedef struct {
    uint32_t tag;
    uint8_t vr[2];
    uint32_t length;
    uint8_t* data;
} DICOM_Element;

struct DICOM_Context; // Forward declaration

// ============================================================================
// DICOM_Context Full Definition
// ============================================================================

struct DICOM_Context {
    FILE* file;
    
    // File metadata
    DICOM_TransferSyntax transfer_syntax;
    bool implicit_vr;
    bool big_endian;
    
    // Pixel data
    uint8_t* pixel_buffer;
    size_t pixel_buffer_size;
    size_t pixel_data_offset;
    size_t pixel_data_length;
    int bits_allocated;
    int bits_stored;
    int high_bit;
    int pixel_representation;
    int samples_per_pixel;
    int rows;
    int columns;
    int number_of_frames;
    
    // Metadata
    char modality[16];
    char sop_class_uid[128];
    float window_center;
    float window_width;
    float rescale_slope;
    float rescale_intercept;
    
    // LUT data
    uint16_t* modality_lut;
    int modality_lut_entries;
    int modality_lut_first_input;
    int modality_lut_bits;
    
    uint16_t* voi_lut;
    int voi_lut_entries;
    
    // Metadata cache
    DICOM_Element* elements[MAX_METADATA_ELEMENTS];
    int element_count;
    bool metadata_parsed;
};

// ============================================================================
// Tag Parsing Helpers
// ============================================================================

static inline uint32_t make_tag(uint16_t group, uint16_t element) {
    return ((uint32_t)group << 16) | element;
}

static inline void split_tag(uint32_t tag, uint16_t* group, uint16_t* element) {
    *group = (uint16_t)(tag >> 16);
    *element = (uint16_t)(tag & 0xFFFF);
}

static int read_explicit_vr_element(DICOM_Context* ctx, uint32_t* tag, uint8_t* vr, uint32_t* length, uint8_t** data) {
    // [P0-FIX] Explicit VR 格式: 4字节tag + 2字节VR + 2字节reserved + 4字节length (或2字节length)
    // 需要至少8字节buffer
    uint8_t tag_bytes[8];
    if (fread(tag_bytes, 1, 8, ctx->file) != 8) return -1;
    
    if (ctx->big_endian) {
        *tag = ((uint32_t)tag_bytes[0] << 24) | ((uint32_t)tag_bytes[1] << 16) | 
               ((uint32_t)tag_bytes[2] << 8) | tag_bytes[3];
    } else {
        *tag = ((uint32_t)tag_bytes[2] << 24) | ((uint32_t)tag_bytes[3] << 16) | 
               ((uint32_t)tag_bytes[0] << 8) | tag_bytes[1];
        uint16_t g = ((uint16_t)tag_bytes[0] << 8) | tag_bytes[1];
        uint16_t e = ((uint16_t)tag_bytes[2] << 8) | tag_bytes[3];
        *tag = ((uint32_t)g << 16) | e;
    }
    
    vr[0] = tag_bytes[4];
    vr[1] = tag_bytes[5];
    
    if (vr[0] == 'O' && (vr[1] == 'B' || vr[1] == 'O' || vr[1] == 'W' || vr[1] == 'X')) {
        // Skip 2 bytes reserved, read 4-byte length
        fseek(ctx->file, 2, SEEK_CUR);
        uint8_t len_bytes[4];
        fread(len_bytes, 1, 4, ctx->file);
        *length = ((uint32_t)len_bytes[0] << 24) | ((uint32_t)len_bytes[1] << 16) |
                  ((uint32_t)len_bytes[2] << 8) | len_bytes[3];
    } else {
        *length = ((uint16_t)tag_bytes[6] << 8) | tag_bytes[7];
    }
    
    if (*length == 0xFFFFFFFF) {
        // Undefined length - not supported for now
        return -1;
    }
    
    if (*length > 0 && *length < 0xFFFFFFFF) {
        *data = (uint8_t*)malloc(*length);
        if (*data) {
            fread(*data, 1, *length, ctx->file);
        }
    } else {
        *data = NULL;
    }
    return 0;
}

static int read_implicit_vr_element(DICOM_Context* ctx, uint32_t* tag, uint32_t* length, uint8_t** data) {
    uint8_t tag_bytes[8];
    if (fread(tag_bytes, 1, 8, ctx->file) != 8) return -1;
    
    *tag = ((uint32_t)tag_bytes[0] << 24) | ((uint32_t)tag_bytes[1] << 16) |
           ((uint32_t)tag_bytes[2] << 8) | tag_bytes[3];
    *length = ((uint32_t)tag_bytes[4] << 24) | ((uint32_t)tag_bytes[5] << 16) |
              ((uint32_t)tag_bytes[6] << 8) | tag_bytes[7];
    
    if (*length > 0 && *length < 0xFFFFFFFF) {
        *data = (uint8_t*)malloc(*length);
        if (*data) {
            fread(*data, 1, *length, ctx->file);
        }
    } else {
        *data = NULL;
    }
    return 0;
}

static void add_metadata_element(DICOM_Context* ctx, uint32_t tag, uint8_t* data, uint32_t length) {
    if (ctx->element_count >= MAX_METADATA_ELEMENTS - 1) return;
    
    DICOM_Element* elem = (DICOM_Element*)malloc(sizeof(DICOM_Element));
    if (!elem) return;
    
    elem->tag = tag;
    elem->length = length;
    elem->data = data;
    
    ctx->elements[ctx->element_count++] = elem;
}

static DICOM_Element* find_metadata_element(DICOM_Context* ctx, uint32_t tag) {
    for (int i = 0; i < ctx->element_count; i++) {
        if (ctx->elements[i]->tag == tag) {
            return ctx->elements[i];
        }
    }
    return NULL;
}

static void parse_dicom_metadata(DICOM_Context* ctx) {
    if (ctx->metadata_parsed || !ctx->file) return;
    
    rewind(ctx->file);
    
    // Skip preamble and DICM magic
    uint8_t preamble[132];
    if (fread(preamble, 1, 132, ctx->file) == 132) {
        if (memcmp(preamble + 128, "DICM", 4) != 0) {
            rewind(ctx->file);
        }
    }
    
    // Parse elements until we hit pixel data or end of file
    while (!feof(ctx->file)) {
        uint32_t tag, length;
        uint8_t* data = NULL;
        uint8_t vr[2] = {0, 0};
        
        if (ctx->implicit_vr) {
            if (read_implicit_vr_element(ctx, &tag, &length, &data) != 0) break;
        } else {
            if (read_explicit_vr_element(ctx, &tag, vr, &length, &data) != 0) break;
        }
        
        if (tag == 0x7FE00010) {
            // Pixel Data - store offset and stop parsing
            ctx->pixel_data_offset = ftell(ctx->file);
            ctx->pixel_data_length = length;
            if (data) free(data);
            break;
        }
        
        if (tag == 0xFFFEE00D || tag == 0xFFFEE0DD) {
            // Sequence delimitation items - skip
            if (data) free(data);
            continue;
        }
        
        // Store important metadata elements
        // [P0-FIX] 使用常量限制替代硬上限
        if (length > 0 && length < MAX_ELEMENT_SIZE) {
            add_metadata_element(ctx, tag, data, length);
        } else if (data) {
            free(data);
        }
        
        // Extract commonly used values directly
        uint16_t group = tag >> 16;
        uint16_t element = tag & 0xFFFF;
        
        if (group == 0x0008 && length > 0 && data) {
            if (element == 0x0060) { // Modality
                strncpy(ctx->modality, (char*)data, length < 15 ? length : 15);
                ctx->modality[length < 15 ? length : 15] = '\0';
            } else if (element == 0x0060 && ctx->modality[0] == '\0') {
                // Default
                strcpy(ctx->modality, "OT");
            }
        }
        else if (group == 0x0028 && length > 0 && data) {
            if (element == 0x0100) ctx->bits_allocated = data[0] << 8 | data[1];
            else if (element == 0x0101) ctx->bits_stored = data[0] << 8 | data[1];
            else if (element == 0x0102) ctx->high_bit = data[0] << 8 | data[1];
            else if (element == 0x0103) ctx->pixel_representation = data[0] << 8 | data[1];
            else if (element == 0x0002) ctx->samples_per_pixel = data[0] << 8 | data[1];
            else if (element == 0x0010) ctx->rows = data[0] << 8 | data[1];
            else if (element == 0x0011) ctx->columns = data[0] << 8 | data[1];
            else if (element == 0x0008) ctx->number_of_frames = data[0] << 8 | data[1];
            else if (element == 0x1050 && length >= 4) { // Window Center
                // [P0-FIX] data 可能不是 NUL 终止的，需要复制到临时缓冲区
                char temp_buf[32];
                size_t copy_len = length < sizeof(temp_buf) - 1 ? length : sizeof(temp_buf) - 1;
                memcpy(temp_buf, data, copy_len);
                temp_buf[copy_len] = '\0';
                ctx->window_center = atof(temp_buf);
            } else if (element == 0x1051 && length >= 4) { // Window Width
                // [P0-FIX] data 可能不是 NUL 终止的，需要复制到临时缓冲区
                char temp_buf[32];
                size_t copy_len = length < sizeof(temp_buf) - 1 ? length : sizeof(temp_buf) - 1;
                memcpy(temp_buf, data, copy_len);
                temp_buf[copy_len] = '\0';
                ctx->window_width = atof(temp_buf);
            }
        }
        else if (group == 0x0020 && length > 0 && data) {
            if (element == 0x0010) { // Patient ID
                // Could store in context if needed
            }
        }
    }
    
    ctx->metadata_parsed = true;
}

// ============================================================================
// Constants
// ============================================================================

// Value Representations
static __attribute__((unused)) const char* VR_NAMES[] = {
    "AE", "AS", "AT", "CS", "DA", "DS", "DT", "FL", "FD", "IS",
    "LO", "LT", "OB", "OD", "OF", "OW", "PN", "SH", "SL", "SS",
    "ST", "TM", "UC", "UI", "UL", "UN", "UR", "US", "UT", "SQ"
};

// SOP Class UID to Modality mapping
static const struct {
    const char* uid;
    const char* modality;
} SOP_CLASS_MAP[] = {
    {"1.2.840.10008.5.1.4.1.1.2",     "CT"},    // CT Image Storage
    {"1.2.840.10008.5.1.4.1.1.4",     "MR"},    // MR Image Storage
    {"1.2.840.10008.5.1.4.1.1.1",      "CR"},    // CR Image Storage
    {"1.2.840.10008.5.1.4.1.1.6.1",    "US"},    // Ultrasound
    {"1.2.840.10008.5.1.4.1.1.66",     "OT"},    // Other
    {"1.2.840.10008.5.1.4.1.1.66.1",   "US"},    // Ultrasound
    {"1.2.840.10008.5.1.4.1.1.66.3",   "OT"},    // Optical
    {"1.2.840.10008.5.1.4.1.1.7",      "OT"},    // Secondary Capture
    {"1.2.840.10008.5.1.4.1.1.12.1",   "XA"},    // X-Ray Angiography
    {"1.2.840.10008.5.1.4.1.1.12.2",   "XA"},    // X-Ray Radio Fluoroscopic
    {"1.2.840.10008.5.1.4.1.1.20",     "OP"},    // Nuclear Medicine
    {"1.2.840.10008.5.1.4.1.1.4.1",    "MR"},    // MR Spectrscopy
    {"1.2.840.10008.5.1.4.1.1.128",    "PT"},    // PET Image Storage
    {"1.2.840.10008.5.1.4.1.1.481.1",  "RF"},    // RT Image
    {"1.2.840.10008.5.1.4.1.1.481.2",  "RT"},    // RT Dose
    {"1.2.840.10008.5.1.4.1.1.481.3",  "RT"},    // RT Structure Set
    {"1.2.840.10008.5.1.4.1.1.481.4",  "RT"},    // RT Beams Treatment Record
    {"1.2.840.10008.5.1.4.1.1.481.5",  "RT"},    // RT Brachy Treatment Record
    {"1.2.840.10008.5.1.4.1.1.481.6",  "RT"},    // RT Treatment Summary Record
    {"1.2.840.10008.5.1.4.1.1.66.4",    "SM"},    // Slide Microscopy
    {"1.2.840.10008.5.1.4.1.1.77.1.1", "DX"},    // VL Photographic
    {"1.2.840.10008.5.1.4.1.1.77.1.2", "DX"},    // VL Endoscopic
    {"1.2.840.10008.5.1.4.1.1.77.1.3", "DX"},    // VL Microscopic
    {"1.2.840.10008.5.1.4.1.1.77.1.4", "OT"},    // External摄影
    {"1.2.840.10008.5.1.4.1.1.88.11",  "SR"},    // Basic Text SR
    {"1.2.840.10008.5.1.4.1.1.88.22",  "SR"},    // Enhanced SR
    {"1.2.840.10008.5.1.4.1.1.88.33",  "SR"},    // Comprehensive SR
    {NULL, NULL}
};

// ============================================================================
// Internal Structures
// ============================================================================



// ============================================================================
// Utility Functions
// ============================================================================

static uint16_t swap_uint16(uint16_t val) {
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
}

static uint32_t swap_uint32(uint32_t val) {
    return ((val & 0xFF) << 24) | 
           ((val & 0xFF00) << 8) |
           ((val >> 8) & 0xFF00) |
           ((val >> 24) & 0xFF);
}

static __attribute__((unused)) void trim_trailing_spaces(char* str) {
    size_t len = strlen(str);
    while (len > 0 && str[len-1] == ' ') {
        str[--len] = '\0';
    }
}

// ============================================================================
// File I/O
// ============================================================================

DICOM_Dataset dicom_open(const char* file_path) {
    if (!file_path) return NULL;
    
    auto* ctx = new DICOM_Context;
    if (!ctx) return NULL;
    
    memset(ctx, 0, sizeof(DICOM_Context));
    
#ifdef _WIN32
    fopen_s(&ctx->file, file_path, "rb");
#else
    ctx->file = fopen(file_path, "rb");
#endif
    
    if (!ctx->file) {
        delete ctx;
        return NULL;
    }
    
    // Check DICOM preamble
    uint8_t preamble[128];
    if (fread(preamble, 1, 128, ctx->file) != 128) {
        fclose(ctx->file);
        delete ctx;
        return NULL;
    }
    
    char magic[4];
    if (fread(magic, 1, sizeof(magic), ctx->file) != sizeof(magic)) {
        fclose(ctx->file);
        delete ctx;
        return NULL;
    }
    
    // Check "DICM" magic number
    if (memcmp(magic, "DICM", sizeof(magic)) != 0) {
        // Not a standard DICOM file, try to read anyway
        rewind(ctx->file);
    }
    
    // Set default transfer syntax
    ctx->transfer_syntax = DICOM_TRANSFER_IMPLICIT_VR_LITTLE_ENDIAN;
    ctx->implicit_vr = true;
    ctx->big_endian = false;
    
    // Skip to pixel data (simplified parsing)
    // In real implementation, would parse all elements
    
    return ctx;
}

void dicom_close(DICOM_Dataset dataset) {
    if (!dataset) return;
    
    auto* ctx = static_cast<DICOM_Context*>(dataset);
    
    if (ctx->file) {
        fclose(ctx->file);
    }
    
    if (ctx->pixel_buffer) {
        free(ctx->pixel_buffer);
    }
    
    // Free metadata elements
    for (int i = 0; i < ctx->element_count; i++) {
        if (ctx->elements[i]->data) {
            free(ctx->elements[i]->data);
        }
        free(ctx->elements[i]);
    }
    
    delete ctx;
}

DICOM_TransferSyntax dicom_get_transfer_syntax(DICOM_Dataset dataset) {
    if (!dataset) return DICOM_TRANSFER_IMPLICIT_VR_LITTLE_ENDIAN;
    auto* ctx = static_cast<DICOM_Context*>(dataset);
    return ctx->transfer_syntax;
}

// ============================================================================
// Tag Reading
// ============================================================================

static __attribute__((unused)) int read_tag_raw(DICOM_Context* ctx, uint32_t* tag, uint8_t* vr[2], 
                        uint32_t* length, void** data) {
    uint8_t raw_tag[4];
    if (fread(raw_tag, 1, 4, ctx->file) != 4) {
        return -1;
    }
    
    uint32_t tmp_tag;
    memcpy(&tmp_tag, raw_tag, sizeof(tmp_tag));
    *tag = ctx->big_endian ? swap_uint32(tmp_tag) : tmp_tag;
    
    if (ctx->implicit_vr) {
        // Implicit VR: 4-byte length follows tag
        uint8_t raw_len[4];
        if (fread(raw_len, 1, 4, ctx->file) != 4) {
            return -1;
        }
        uint32_t tmp32;
        memcpy(&tmp32, raw_len, sizeof(tmp32));
        *length = ctx->big_endian ? swap_uint32(tmp32) : tmp32;
        *data = malloc(*length);
        if (*data && *length > 0) {
            if (fread(*data, 1, *length, ctx->file) != *length) {
                free(*data);
                return -1;
            }
        }
    } else {
        // Explicit VR: 2-byte VR + reserved + 4-byte length
        if (fread(vr[0], 1, 1, ctx->file) != 1) return -1;
        if (fread(vr[1], 1, 1, ctx->file) != 1) return -1;
        
        // Check if VR has 32-bit length
        bool vr_has_32bit_len = false;
        if (strncmp((char*)vr[0], "OB", 2) == 0 ||
            strncmp((char*)vr[0], "OW", 2) == 0 ||
            strncmp((char*)vr[0], "OF", 2) == 0 ||
            strncmp((char*)vr[0], "SQ", 2) == 0 ||
            strncmp((char*)vr[0], "UN", 2) == 0 ||
            strncmp((char*)vr[0], "UC", 2) == 0 ||
            strncmp((char*)vr[0], "UR", 2) == 0 ||
            strncmp((char*)vr[0], "UT", 2) == 0) {
            vr_has_32bit_len = true;
            fseek(ctx->file, 4, SEEK_CUR);  // Skip reserved
        }
        
        uint8_t raw_len[4];
        if (fread(raw_len, 1, 4, ctx->file) != 4) return -1;
        
        if (vr_has_32bit_len) {
            uint32_t tmp32;
        memcpy(&tmp32, raw_len, sizeof(tmp32));
        *length = ctx->big_endian ? swap_uint32(tmp32) : tmp32;
        } else {
            uint16_t tmp16;
            memcpy(&tmp16, raw_len, 2);
            *length = ctx->big_endian ? swap_uint16(tmp16) : tmp16;
        }
        
        *data = malloc(*length);
        if (*data && *length > 0) {
            if (fread(*data, 1, *length, ctx->file) != *length) {
                free(*data);
                return -1;
            }
        }
    }
    
    return 0;
}

int dicom_read_string(DICOM_Dataset dataset, uint32_t tag, char* buffer, size_t buffer_size) {
    if (!dataset || !buffer || buffer_size == 0) return -1;
    
    (void)dataset; (void)tag;
    // In real implementation, would parse file
    
    buffer[0] = '\0';
    return 0;
}

int dicom_read_uint16(DICOM_Dataset dataset, uint32_t tag, uint16_t* value) {
    if (!dataset || !value) return -1;
    
    auto* ctx = static_cast<DICOM_Context*>(dataset);
    
    if (!ctx->metadata_parsed) {
        parse_dicom_metadata(ctx);
    }
    
    DICOM_Element* elem = find_metadata_element(ctx, tag);
    if (elem && elem->data && elem->length >= 2) {
        *value = (uint16_t)(elem->data[0] << 8 | elem->data[1]);
        return 0;
    }
    
    *value = 0;
    return -1;
}

int dicom_read_uint32(DICOM_Dataset dataset, uint32_t tag, uint32_t* value) {
    if (!dataset || !value) return -1;
    
    auto* ctx = static_cast<DICOM_Context*>(dataset);
    
    if (!ctx->metadata_parsed) {
        parse_dicom_metadata(ctx);
    }
    
    DICOM_Element* elem = find_metadata_element(ctx, tag);
    if (elem && elem->data && elem->length >= 4) {
        *value = ((uint32_t)elem->data[0] << 24) | ((uint32_t)elem->data[1] << 16) |
                 ((uint32_t)elem->data[2] << 8) | elem->data[3];
        return 0;
    }
    
    *value = 0;
    return -1;
}

int dicom_read_float(DICOM_Dataset dataset, uint32_t tag, float* value) {
    if (!dataset || !value) return -1;
    
    auto* ctx = static_cast<DICOM_Context*>(dataset);
    
    if (!ctx->metadata_parsed) {
        parse_dicom_metadata(ctx);
    }
    
    DICOM_Element* elem = find_metadata_element(ctx, tag);
    if (elem && elem->data && elem->length >= 4) {
        uint32_t bits = ((uint32_t)elem->data[0] << 24) | ((uint32_t)elem->data[1] << 16) |
                        ((uint32_t)elem->data[2] << 8) | elem->data[3];
        memcpy(value, &bits, sizeof(float));  // [P0-FIX] 安全的 float 转换
        return 0;
    }
    
    *value = 0.0f;
    return 0;
}

int dicom_read_double(DICOM_Dataset dataset, uint32_t tag, double* value) {
    if (!dataset || !value) return -1;
    
    auto* ctx = static_cast<DICOM_Context*>(dataset);
    
    if (!ctx->metadata_parsed) {
        parse_dicom_metadata(ctx);
    }
    
    DICOM_Element* elem = find_metadata_element(ctx, tag);
    if (elem && elem->data && elem->length >= 8) {
        uint64_t bits = 0;
        for (int i = 0; i < 8; i++) {
            bits = (bits << 8) | elem->data[i];
        }
        memcpy(value, &bits, sizeof(double));  // [P0-FIX] 安全的 double 转换
        return 0;
    }
    
    *value = 0.0;
    return 0;
}

// ============================================================================
// Window/Level
// ============================================================================

void dicom_read_window_level(DICOM_Dataset dataset, float* center, float* width) {
    if (!dataset) return;
    
    auto* ctx = static_cast<DICOM_Context*>(dataset);
    
    if (!ctx->metadata_parsed) {
        parse_dicom_metadata(ctx);
    }
    
    if (center) *center = ctx->window_center > 0 ? ctx->window_center : 40.0f;
    if (width) *width = ctx->window_width > 0 ? ctx->window_width : 400.0f;
}

// ============================================================================
// Pixel Data
// ============================================================================

static __attribute__((unused)) int decode_rle(DICOM_Context* ctx, const uint8_t* compressed, size_t comp_size,
                      uint8_t* decompressed, size_t decomp_size) {
    (void)ctx; (void)compressed; (void)comp_size; (void)decompressed; (void)decomp_size;
    size_t src_pos = 0;
    size_t dst_pos = 0;
    
    while (src_pos < comp_size && dst_pos < decomp_size) {
        uint8_t header = compressed[src_pos++];
        
        if (header == 128) {
            // End of segment
            break;
        } else if (header < 128) {
            // Copy literal
            size_t count = header + 1;
            if (dst_pos + count > decomp_size) break;
            memcpy(decompressed + dst_pos, compressed + src_pos, count);
            src_pos += count;
            dst_pos += count;
        } else {
            // Repeat
            size_t count = 257 - header;
            if (dst_pos + count > decomp_size) break;
            uint8_t value = compressed[src_pos++];
            memset(decompressed + dst_pos, value, count);
            dst_pos += count;
        }
    }
    
    return (int)dst_pos;
}

static __attribute__((unused)) int decode_jpeg_baseline(DICOM_Context* ctx, const uint8_t* compressed, 
                                 size_t comp_size, uint8_t* decompressed, size_t decomp_size) {
    // Simplified JPEG decoder placeholder
    // In real implementation, would use libjpeg-turbo or similar
    (void)ctx;
    (void)compressed;
    (void)comp_size;
    (void)decompressed;
    (void)decomp_size;
    
    // Return 0 to indicate decoding not implemented
    return 0;
}

int dicom_read_pixels(DICOM_Dataset dataset, DICOM_PixelData* pixel_info) {
    if (!dataset || !pixel_info) return -1;
    
    auto* ctx = static_cast<DICOM_Context*>(dataset);
    (void)ctx; // ctx reserved for future parsing
    // In real implementation, would parse pixel data element
    
    memset(pixel_info, 0, sizeof(DICOM_PixelData));
    
    // Return placeholder data
    pixel_info->rows = 512;
    pixel_info->columns = 512;
    pixel_info->bits_allocated = 16;
    pixel_info->bits_stored = 12;
    pixel_info->high_bit = 11;
    pixel_info->pixel_representation = 1;  // Signed
    pixel_info->samples_per_pixel = 1;
    pixel_info->photometric_interp = "MONOCHROME2";
    
    return 0;
}

// ============================================================================
// HU Calculation
// ============================================================================

float dicom_pixel_to_hu(int raw_pixel, float slope, float intercept) {
    return slope * (float)raw_pixel + intercept;
}

int dicom_apply_modality_lut(int raw_pixel, const uint16_t* lut_data, int lut_entries) {
    if (!lut_data || lut_entries <= 0) {
        return raw_pixel;
    }
    
    if (raw_pixel < 0) {
        raw_pixel = 0;
    } else if (raw_pixel >= lut_entries) {
        raw_pixel = lut_entries - 1;
    }
    
    return lut_data[raw_pixel];
}

// ============================================================================
// Metadata
// ============================================================================

const char* dicom_sop_class_to_modality(const char* sop_class_uid) {
    if (!sop_class_uid) return "OT";
    
    for (int i = 0; SOP_CLASS_MAP[i].uid != NULL; i++) {
        if (strcmp(sop_class_uid, SOP_CLASS_MAP[i].uid) == 0) {
            return SOP_CLASS_MAP[i].modality;
        }
    }
    
    return "OT";  // Default to Other
}

void dicom_extract_metadata(DICOM_Dataset dataset, DICOM_Metadata* metadata) {
    if (!dataset || !metadata) return;

    auto* ctx = static_cast<DICOM_Context*>(dataset);

    // [P0-FIX] 从 ctx 读取实际的 modality，而不是硬编码 "CT"
    if (!ctx->metadata_parsed) {
        parse_dicom_metadata(ctx);
    }

    // Set default values
    strcpy(metadata->modality, ctx->modality[0] ? ctx->modality : "OT");
    metadata->rescale_slope = 1.0f;
    metadata->rescale_intercept = -1024.0f;
    metadata->window_center = 40.0f;
    metadata->window_width = 400.0f;
    metadata->body_part[0] = '\0';
}

bool dicom_is_monochrome(DICOM_Dataset dataset) {
    (void)dataset;
    // [P0-FIX] 删除死代码，保留单一 return 语句
    return true;
}

bool dicom_needs_inversion(DICOM_Dataset dataset) {
    (void)dataset;
    // [P0-FIX] 删除死代码，保留单一 return 语句
    // Would check actual tag value
    return false;
}

int dicom_get_frame_count(DICOM_Dataset dataset) {
    if (!dataset) return 0;
    
    // Would check NumberOfFrames tag
    return 1;
}

int dicom_read_frame(DICOM_Dataset dataset, int frame_index, DICOM_PixelData* pixel_info) {
    if (!dataset || !pixel_info) return -1;
    
    if (frame_index != 0) {
        // Only support single frame for now
        return -1;
    }
    
    return dicom_read_pixels(dataset, pixel_info);
}
