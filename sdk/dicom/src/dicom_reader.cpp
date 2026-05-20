/**
 * @file dicom_reader.cpp
 * @brief DICOM Medical Image Reader Implementation
 */

#include "dicom_reader.h"
#include "simd_utils.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
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
    bool encapsulated_pixel_data;

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

    // [P2-OPT] Last accessed element cache for O(1) repeated lookups
    uint32_t last_tag;
    DICOM_Element* last_element;
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

static uint16_t read_u16_value(const uint8_t* data, bool big_endian) {
    if (!data) return 0;
    if (big_endian) {
        return static_cast<uint16_t>((data[0] << 8) | data[1]);
    }
    return static_cast<uint16_t>((data[1] << 8) | data[0]);
}

static uint32_t read_u32_value(const uint8_t* data, bool big_endian) {
    if (!data) return 0;
    if (big_endian) {
        return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
               ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    }
    return ((uint32_t)data[3] << 24) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[1] << 8) | (uint32_t)data[0];
}

static float parse_float_ascii(const uint8_t* data, uint32_t length, float fallback) {
    if (!data || length == 0) return fallback;
    char temp_buf[64];
    size_t copy_len = length < sizeof(temp_buf) - 1 ? length : sizeof(temp_buf) - 1;
    memcpy(temp_buf, data, copy_len);
    temp_buf[copy_len] = '\0';
    return std::strtof(temp_buf, nullptr);
}

static std::string parse_string_ascii(const uint8_t* data, uint32_t length) {
    if (!data || length == 0) return {};
    std::string value(reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + length);
    while (!value.empty() && (value.back() == '\0' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

static bool is_encapsulated_transfer_syntax(DICOM_TransferSyntax syntax) {
    switch (syntax) {
        case DICOM_TRANSFER_JPEG_BASELINE_1:
        case DICOM_TRANSFER_JPEG_EXTENDED_2_4:
        case DICOM_TRANSFER_JPEG_LOSSLESS:
        case DICOM_TRANSFER_JPEG_LS_LOSSLESS:
        case DICOM_TRANSFER_JPEG_LS_LOSSY:
        case DICOM_TRANSFER_JPEG2000_LOSSLESS:
        case DICOM_TRANSFER_JPEG2000_LOSSY:
        case DICOM_TRANSFER_RLE_LOSSLESS:
        case DICOM_TRANSFER_MPEG2_MAIN_PROFILE:
        case DICOM_TRANSFER_MPEG4_AVC_H264_HIGH_PROFILE:
            return true;
        default:
            return false;
    }
}

static bool map_transfer_syntax_uid(const char* uid, DICOM_TransferSyntax* syntax, bool* implicit_vr, bool* big_endian) {
    if (!uid || !syntax || !implicit_vr || !big_endian) return false;

    if (strcmp(uid, "1.2.840.10008.1.2") == 0) {
        *syntax = DICOM_TRANSFER_IMPLICIT_VR_LITTLE_ENDIAN;
        *implicit_vr = true;
        *big_endian = false;
        return true;
    }
    if (strcmp(uid, "1.2.840.10008.1.2.1") == 0) {
        *syntax = DICOM_TRANSFER_EXPLICIT_VR_LITTLE_ENDIAN;
        *implicit_vr = false;
        *big_endian = false;
        return true;
    }
    if (strcmp(uid, "1.2.840.10008.1.2.2") == 0) {
        *syntax = DICOM_TRANSFER_EXPLICIT_VR_BIG_ENDIAN;
        *implicit_vr = false;
        *big_endian = true;
        return true;
    }
    if (strcmp(uid, "1.2.840.10008.1.2.5") == 0) {
        *syntax = DICOM_TRANSFER_RLE_LOSSLESS;
        *implicit_vr = false;
        *big_endian = false;
        return true;
    }
    if (strcmp(uid, "1.2.840.10008.1.2.4.50") == 0) {
        *syntax = DICOM_TRANSFER_JPEG_BASELINE_1;
        *implicit_vr = false;
        *big_endian = false;
        return true;
    }
    if (strcmp(uid, "1.2.840.10008.1.2.4.90") == 0) {
        *syntax = DICOM_TRANSFER_JPEG2000_LOSSLESS;
        *implicit_vr = false;
        *big_endian = false;
        return true;
    }
    return false;
}

static int parse_rle_header(const uint8_t* data, size_t data_len, uint32_t* offsets, uint32_t* segment_count) {
    if (!data || data_len < 64 || !offsets || !segment_count) return -1;
    *segment_count = read_u32_value(data, false);
    if (*segment_count == 0 || *segment_count > 15) return -1;
    for (uint32_t idx = 0; idx < *segment_count; ++idx) {
        offsets[idx] = read_u32_value(data + 4 + idx * 4, false);
        if (offsets[idx] >= data_len) {
            return -1;
        }
    }
    return 0;
}

static int decode_rle_segment(const uint8_t* src, size_t src_size, uint8_t* dst, size_t dst_size) {
    if (!src || !dst) return -1;
    size_t src_pos = 0;
    size_t dst_pos = 0;
    while (src_pos < src_size && dst_pos < dst_size) {
        const int8_t n = static_cast<int8_t>(src[src_pos++]);
        if (n >= 0) {
            const size_t count = static_cast<size_t>(n) + 1;
            if (src_pos + count > src_size) return -1;
            const size_t writable = std::min(count, dst_size - dst_pos);
            memcpy(dst + dst_pos, src + src_pos, writable);
            src_pos += count;
            dst_pos += writable;
        } else if (n > -128) {
            if (src_pos >= src_size) return -1;
            const uint8_t value = src[src_pos++];
            const size_t count = static_cast<size_t>(1 - n);
            const size_t writable = std::min(count, dst_size - dst_pos);
            memset(dst + dst_pos, value, writable);
            dst_pos += writable;
        }
    }
    return dst_pos == dst_size ? 0 : -1;
}

static int decode_rle_frame(const uint8_t* data, size_t data_len, int rows, int columns,
                            int samples_per_pixel, int bits_allocated,
                            uint8_t* output, size_t output_size) {
    if (!data || !output || rows <= 0 || columns <= 0 || bits_allocated % 8 != 0) return -1;
    const int bytes_per_sample = bits_allocated / 8;
    const size_t pixel_count = static_cast<size_t>(rows) * static_cast<size_t>(columns);
    const size_t expected_size = pixel_count * static_cast<size_t>(samples_per_pixel) * static_cast<size_t>(bytes_per_sample);
    if (output_size < expected_size) return -1;

    uint32_t offsets[15] = {};
    uint32_t segment_count = 0;
    if (parse_rle_header(data, data_len, offsets, &segment_count) != 0) return -1;
    if (segment_count != static_cast<uint32_t>(samples_per_pixel * bytes_per_sample)) return -1;

    std::vector<uint32_t> segment_ends(segment_count, static_cast<uint32_t>(data_len));
    for (uint32_t idx = 0; idx + 1 < segment_count; ++idx) {
        segment_ends[idx] = offsets[idx + 1];
    }

    std::vector<uint8_t> decoded_plane(pixel_count);
    memset(output, 0, expected_size);

    for (int sample = 0; sample < samples_per_pixel; ++sample) {
        for (int byte_offset = 0; byte_offset < bytes_per_sample; ++byte_offset) {
            const int segment_index = sample * bytes_per_sample + byte_offset;
            const uint32_t start = offsets[segment_index];
            const uint32_t end = segment_ends[segment_index];
            if (end <= start || end > data_len) return -1;
            if (decode_rle_segment(data + start, end - start, decoded_plane.data(), pixel_count) != 0) return -1;

            const int output_byte_index = bytes_per_sample - byte_offset - 1;
            for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
                const size_t base = (pixel * static_cast<size_t>(samples_per_pixel) + static_cast<size_t>(sample)) * static_cast<size_t>(bytes_per_sample);
                output[base + output_byte_index] = decoded_plane[pixel];
            }
        }
    }
    return 0;
}

static int read_explicit_vr_element(DICOM_Context* ctx, uint32_t* tag, uint8_t* vr, uint32_t* length, uint8_t** data) {
    uint8_t header[8];
    if (fread(header, 1, sizeof(header), ctx->file) != sizeof(header)) return -1;

    const uint16_t group = read_u16_value(header, ctx->big_endian);
    const uint16_t element = read_u16_value(header + 2, ctx->big_endian);
    *tag = make_tag(group, element);

    vr[0] = header[4];
    vr[1] = header[5];

    const bool long_length_vr =
        (vr[0] == 'O' && (vr[1] == 'B' || vr[1] == 'W' || vr[1] == 'F')) ||
        (vr[0] == 'S' && vr[1] == 'Q') ||
        (vr[0] == 'U' && (vr[1] == 'N' || vr[1] == 'T' || vr[1] == 'C' || vr[1] == 'R')) ||
        (vr[0] == 'O' && vr[1] == 'D');

    if (long_length_vr) {
        uint8_t len_bytes[4];
        if (fread(len_bytes, 1, sizeof(len_bytes), ctx->file) != sizeof(len_bytes)) return -1;
        *length = read_u32_value(len_bytes, ctx->big_endian);
    } else {
        *length = read_u16_value(header + 6, ctx->big_endian);
    }
    
    if (*length == 0xFFFFFFFF) {
        // Undefined length - not supported for now
        return -1;
    }
    
    if (*length > 0 && *length < 0xFFFFFFFF && *length <= MAX_ELEMENT_SIZE) {
        *data = (uint8_t*)malloc(*length);
        if (*data) {
            if (fread(*data, 1, *length, ctx->file) != *length) {
                free(*data);
                *data = nullptr;
                return -1;
            }
        }
    } else {
        *data = nullptr;
        if (*length > MAX_ELEMENT_SIZE) {
            if (fseek(ctx->file, static_cast<long>(*length), SEEK_CUR) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int read_implicit_vr_element(DICOM_Context* ctx, uint32_t* tag, uint32_t* length, uint8_t** data) {
    uint8_t tag_bytes[8];
    if (fread(tag_bytes, 1, 8, ctx->file) != 8) return -1;
    
    const uint16_t group = read_u16_value(tag_bytes, ctx->big_endian);
    const uint16_t element = read_u16_value(tag_bytes + 2, ctx->big_endian);
    *tag = make_tag(group, element);
    *length = read_u32_value(tag_bytes + 4, ctx->big_endian);
    
    if (*length == 0xFFFFFFFF) {
        return -1;
    }

    if (*length > 0 && *length <= MAX_ELEMENT_SIZE) {
        *data = (uint8_t*)malloc(*length);
        if (*data) {
            if (fread(*data, 1, *length, ctx->file) != *length) {
                free(*data);
                *data = nullptr;
                return -1;
            }
        }
    } else {
        *data = nullptr;
        if (*length > MAX_ELEMENT_SIZE) {
            if (fseek(ctx->file, static_cast<long>(*length), SEEK_CUR) != 0) {
                return -1;
            }
        }
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

// [P2-OPT] Optimized element lookup with last-accessed cache
static DICOM_Element* find_metadata_element(DICOM_Context* ctx, uint32_t tag) {
    // [P2-OPT] Check cache first (most calls are sequential/repeated)
    if (ctx->last_element && ctx->last_tag == tag) {
        return ctx->last_element;
    }

    // Linear search
    for (int i = 0; i < ctx->element_count; i++) {
        if (ctx->elements[i]->tag == tag) {
            // [P2-OPT] Cache the result for next lookup
            ctx->last_tag = tag;
            ctx->last_element = ctx->elements[i];
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

    // File Meta Information (0002,eeee) 总是 Explicit VR Little Endian
    // 先单独读取 Transfer Syntax，再切回数据集自己的 VR/字节序
    {
        long meta_start = ftell(ctx->file);
        while (!feof(ctx->file)) {
            const long element_start = ftell(ctx->file);
            uint32_t tag = 0;
            uint32_t length = 0;
            uint8_t* data = nullptr;
            uint8_t vr[2] = {0, 0};

            const bool saved_implicit = ctx->implicit_vr;
            const bool saved_big_endian = ctx->big_endian;
            ctx->implicit_vr = false;
            ctx->big_endian = false;
            const int rc = read_explicit_vr_element(ctx, &tag, vr, &length, &data);
            ctx->implicit_vr = saved_implicit;
            ctx->big_endian = saved_big_endian;

            if (rc != 0) {
                if (data) free(data);
                fseek(ctx->file, meta_start, SEEK_SET);
                break;
            }

            const uint16_t group = static_cast<uint16_t>(tag >> 16);
            if (group != 0x0002) {
                if (data) free(data);
                fseek(ctx->file, element_start, SEEK_SET);
                break;
            }

            const uint16_t element = static_cast<uint16_t>(tag & 0xFFFF);
            if (element == 0x0010 && data && length > 0) {
                std::string ts_uid = parse_string_ascii(data, length);
                DICOM_TransferSyntax syntax = ctx->transfer_syntax;
                bool implicit = ctx->implicit_vr;
                bool big_endian = ctx->big_endian;
                if (map_transfer_syntax_uid(ts_uid.c_str(), &syntax, &implicit, &big_endian)) {
                    ctx->transfer_syntax = syntax;
                    ctx->implicit_vr = implicit;
                    ctx->big_endian = big_endian;
                    ctx->encapsulated_pixel_data = is_encapsulated_transfer_syntax(syntax);
                }
            }

            if (data) {
                free(data);
            }
        }
    }
    
    // Parse elements until we hit pixel data or end of file
    while (!feof(ctx->file)) {
        uint32_t tag, length;
        uint8_t* data = nullptr;
        uint8_t vr[2] = {0, 0};
        
        if (ctx->implicit_vr) {
            if (read_implicit_vr_element(ctx, &tag, &length, &data) != 0) break;
        } else {
            if (read_explicit_vr_element(ctx, &tag, vr, &length, &data) != 0) break;
        }
        
        if (tag == DICOM_TAG_PIXEL_DATA) {
            // Pixel Data - store offset and stop parsing
            const long current_pos = ftell(ctx->file);
            if (current_pos >= 0 && length <= static_cast<uint32_t>(current_pos)) {
                ctx->pixel_data_offset = static_cast<size_t>(current_pos - static_cast<long>(length));
            }
            ctx->pixel_data_length = length;
            if (data) {
                ctx->pixel_buffer = data;
                ctx->pixel_buffer_size = length;
                data = nullptr;
            }
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
            if (element == 0x0010 || element == 0x0012) {
                std::string ts_uid = parse_string_ascii(data, length);
                DICOM_TransferSyntax syntax = ctx->transfer_syntax;
                bool implicit = ctx->implicit_vr;
                bool big_endian = ctx->big_endian;
                if (map_transfer_syntax_uid(ts_uid.c_str(), &syntax, &implicit, &big_endian)) {
                    ctx->transfer_syntax = syntax;
                    ctx->implicit_vr = implicit;
                    ctx->big_endian = big_endian;
                    ctx->encapsulated_pixel_data = is_encapsulated_transfer_syntax(syntax);
                }
            } else if (element == 0x0060) { // Modality
                strncpy(ctx->modality, (char*)data, length < 15 ? length : 15);
                ctx->modality[length < 15 ? length : 15] = '\0';
            } else if (element == 0x0060 && ctx->modality[0] == '\0') {
                // Default
                strcpy(ctx->modality, "OT");
            }
        }
        else if (group == 0x0028 && length > 0 && data) {
            if (element == 0x0100 && length >= 2) ctx->bits_allocated = read_u16_value(data, ctx->big_endian);
            else if (element == 0x0101 && length >= 2) ctx->bits_stored = read_u16_value(data, ctx->big_endian);
            else if (element == 0x0102 && length >= 2) ctx->high_bit = read_u16_value(data, ctx->big_endian);
            else if (element == 0x0103 && length >= 2) ctx->pixel_representation = read_u16_value(data, ctx->big_endian);
            else if (element == 0x0002 && length >= 2) ctx->samples_per_pixel = read_u16_value(data, ctx->big_endian);
            else if (element == 0x0010 && length >= 2) ctx->rows = read_u16_value(data, ctx->big_endian);
            else if (element == 0x0011 && length >= 2) ctx->columns = read_u16_value(data, ctx->big_endian);
            else if (element == 0x0008) ctx->number_of_frames = static_cast<int>(parse_float_ascii(data, length, 1.0f));
            else if (element == 0x1050 && length >= 4) { // Window Center
                ctx->window_center = parse_float_ascii(data, length, ctx->window_center);
            } else if (element == 0x1051 && length >= 4) { // Window Width
                ctx->window_width = parse_float_ascii(data, length, ctx->window_width);
            } else if (element == 0x0004) {
                std::string photometric = parse_string_ascii(data, length);
                (void)photometric;
            }
        }
        else if (group == 0x0020 && length > 0 && data) {
            if (element == 0x0010) { // Patient ID
                // Could store in context if needed
            }
        }
    }
    
    ctx->metadata_parsed = true;
    if (ctx->bits_allocated <= 0) ctx->bits_allocated = 16;
    if (ctx->bits_stored <= 0) ctx->bits_stored = 12;
    if (ctx->high_bit <= 0) ctx->high_bit = ctx->bits_stored - 1;
    if (ctx->samples_per_pixel <= 0) ctx->samples_per_pixel = 1;
    if (ctx->rows <= 0) ctx->rows = 512;
    if (ctx->columns <= 0) ctx->columns = 512;
    if (ctx->number_of_frames <= 0) ctx->number_of_frames = 1;
    if (ctx->window_center == 0.0f) ctx->window_center = 40.0f;
    if (ctx->window_width == 0.0f) ctx->window_width = 400.0f;
    if (ctx->rescale_slope == 0.0f) ctx->rescale_slope = 1.0f;
    if (ctx->rescale_intercept == 0.0f) ctx->rescale_intercept = -1024.0f;
    if (ctx->modality[0] == '\0') std::strcpy(ctx->modality, "OT");
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
    if (!ctx->metadata_parsed) {
        parse_dicom_metadata(ctx);
    }
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
        *value = read_u16_value(elem->data, ctx->big_endian);
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
        *value = read_u32_value(elem->data, ctx->big_endian);
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
        uint32_t bits = read_u32_value(elem->data, ctx->big_endian);
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
    if (!ctx || !compressed || !decompressed) return -1;
    return decode_rle_frame(compressed, comp_size, ctx->rows, ctx->columns,
                            ctx->samples_per_pixel, ctx->bits_allocated,
                            decompressed, decomp_size);
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
    if (!ctx->metadata_parsed) {
        parse_dicom_metadata(ctx);
    }

    memset(pixel_info, 0, sizeof(DICOM_PixelData));

    pixel_info->rows = ctx->rows > 0 ? static_cast<uint32_t>(ctx->rows) : 512u;
    pixel_info->columns = ctx->columns > 0 ? static_cast<uint32_t>(ctx->columns) : 512u;
    pixel_info->bits_allocated = ctx->bits_allocated > 0 ? static_cast<uint32_t>(ctx->bits_allocated) : 16u;
    pixel_info->bits_stored = ctx->bits_stored > 0 ? static_cast<uint32_t>(ctx->bits_stored) : 12u;
    pixel_info->high_bit = ctx->high_bit >= 0 ? static_cast<uint32_t>(ctx->high_bit) : (pixel_info->bits_stored - 1u);
    pixel_info->pixel_representation = ctx->pixel_representation > 0 ? 1u : 0u;
    pixel_info->samples_per_pixel = ctx->samples_per_pixel > 0 ? static_cast<uint32_t>(ctx->samples_per_pixel) : 1u;
    pixel_info->photometric_interp = "MONOCHROME2";

    if (ctx->pixel_data_length == 0 || ctx->pixel_data_length > MAX_PIXEL_DATA_SIZE) {
        return 0;
    }

    if (!ctx->pixel_buffer) {
        if (ctx->pixel_data_offset == 0) {
            return -1;
        }
        ctx->pixel_buffer = static_cast<uint8_t*>(malloc(ctx->pixel_data_length));
        if (!ctx->pixel_buffer) {
            return -1;
        }

        if (fseek(ctx->file, static_cast<long>(ctx->pixel_data_offset), SEEK_SET) != 0) {
            free(ctx->pixel_buffer);
            ctx->pixel_buffer = nullptr;
            return -1;
        }

        if (fread(ctx->pixel_buffer, 1, ctx->pixel_data_length, ctx->file) != ctx->pixel_data_length) {
            free(ctx->pixel_buffer);
            ctx->pixel_buffer = nullptr;
            return -1;
        }

        ctx->pixel_buffer_size = ctx->pixel_data_length;
    }

    if (ctx->transfer_syntax == DICOM_TRANSFER_RLE_LOSSLESS) {
        const size_t decoded_size = static_cast<size_t>(pixel_info->rows) *
                                    static_cast<size_t>(pixel_info->columns) *
                                    static_cast<size_t>(pixel_info->samples_per_pixel) *
                                    static_cast<size_t>(pixel_info->bits_allocated <= 8 ? 1 : 2);
        uint8_t* decoded = static_cast<uint8_t*>(malloc(decoded_size));
        if (!decoded) return -1;
        if (decode_rle(ctx, ctx->pixel_buffer, ctx->pixel_buffer_size, decoded, decoded_size) != 0) {
            free(decoded);
            return -1;
        }
        free(ctx->pixel_buffer);
        ctx->pixel_buffer = decoded;
        ctx->pixel_buffer_size = decoded_size;
        ctx->transfer_syntax = DICOM_TRANSFER_EXPLICIT_VR_LITTLE_ENDIAN;
        ctx->encapsulated_pixel_data = false;
    }

    pixel_info->pixel_data = ctx->pixel_buffer;
    pixel_info->pixel_data_size = ctx->pixel_buffer_size;

    return 0;
}

// ============================================================================
// HU Calculation
// ============================================================================

float dicom_pixel_to_hu(int raw_pixel, float slope, float intercept) {
    return slope * (float)raw_pixel + intercept;
}

int dicom_pixels_to_hu_batch(const uint16_t* raw_pixels, size_t count,
                             float slope, float intercept, float* hu_values) {
    if (!raw_pixels || !hu_values || count == 0) {
        return -1;
    }

    simd_pixels_to_hu_batch(raw_pixels, hu_values, count, slope, intercept);
    return 0;
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
    
    for (int i = 0; SOP_CLASS_MAP[i].uid != nullptr; i++) {
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

    memset(metadata, 0, sizeof(*metadata));
    strcpy(metadata->modality, ctx->modality[0] ? ctx->modality : "OT");
    metadata->rescale_slope = ctx->rescale_slope != 0.0f ? ctx->rescale_slope : 1.0f;
    metadata->rescale_intercept = ctx->rescale_intercept != 0.0f ? ctx->rescale_intercept : -1024.0f;
    metadata->window_center = ctx->window_center > 0.0f ? ctx->window_center : 40.0f;
    metadata->window_width = ctx->window_width > 0.0f ? ctx->window_width : 400.0f;
    metadata->number_of_frames = ctx->number_of_frames > 0 ? ctx->number_of_frames : 1;
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

    auto* ctx = static_cast<DICOM_Context*>(dataset);
    if (!ctx->metadata_parsed) {
        parse_dicom_metadata(ctx);
    }
    return ctx->number_of_frames > 0 ? ctx->number_of_frames : 1;
}

int dicom_read_frame(DICOM_Dataset dataset, int frame_index, DICOM_PixelData* pixel_info) {
    if (!dataset || !pixel_info) return -1;

    auto* ctx = static_cast<DICOM_Context*>(dataset);
    if (!ctx->metadata_parsed) {
        parse_dicom_metadata(ctx);
    }

    const int frame_count = dicom_get_frame_count(dataset);
    if (frame_index < 0 || frame_index >= frame_count) {
        return -1;
    }

    if (dicom_read_pixels(dataset, pixel_info) != 0) {
        return -1;
    }

    if (!pixel_info->pixel_data || frame_count <= 1) {
        return 0;
    }

    const size_t bytes_per_sample = pixel_info->bits_allocated <= 8 ? 1u : 2u;
    const size_t frame_bytes = static_cast<size_t>(pixel_info->rows) *
                               static_cast<size_t>(pixel_info->columns) *
                               static_cast<size_t>(pixel_info->samples_per_pixel) *
                               bytes_per_sample;
    const size_t offset = frame_bytes * static_cast<size_t>(frame_index);
    if (offset + frame_bytes > pixel_info->pixel_data_size) {
        return -1;
    }

    pixel_info->pixel_data = static_cast<const uint8_t*>(pixel_info->pixel_data) + offset;
    pixel_info->pixel_data_size = frame_bytes;
    return 0;
}
