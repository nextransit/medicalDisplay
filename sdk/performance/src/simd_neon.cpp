/**
 * @file simd_neon.cpp
 * @brief ARM NEON SIMD加速实现
 * 
 * 用于 Android/Raspberry Pi 等 ARM 平台
 */

#if defined(__ARM_NEON) || defined(__ARM_NEON__)

#include "simd_processing.h"
#include <arm_neon.h>
#include <cmath>
#include <algorithm>

// ============================================================================
// 辅助函数
// ============================================================================

static inline uint8x16_t clamp_u8x16(float32x4x4_t val) {
    // 将浮点向量限制在 0-255 范围
    float32x4_t r0 = val.val[0];
    float32x4_t r1 = val.val[1];
    float32x4_t r2 = val.val[2];
    float32x4_t r3 = val.val[3];
    
    r0 = vmaxq_f32(r0, vdupq_n_f32(0.0f));
    r0 = vminq_f32(r0, vdupq_n_f32(255.0f));
    r1 = vmaxq_f32(r1, vdupq_n_f32(0.0f));
    r1 = vminq_f32(r1, vdupq_n_f32(255.0f));
    r2 = vmaxq_f32(r2, vdupq_n_f32(0.0f));
    r2 = vminq_f32(r2, vdupq_n_f32(255.0f));
    r3 = vmaxq_f32(r3, vdupq_n_f32(0.0f));
    r3 = vminq_f32(r3, vdupq_n_f32(255.0f));
    
    val.val[0] = r0;
    val.val[1] = r1;
    val.val[2] = r2;
    val.val[3] = r3;
    
    return vcombine_u8(vmovn_u16(vqmovun_s32(vcvtq_s32_f32(r0))),
                       vmovn_u16(vqmovun_s32(vcvtq_s32_f32(r1))));
}

// ============================================================================
// 亮度/对比度调整
// ============================================================================

void neon_brightness_contrast(const uint8_t* input,
                              uint8_t* output,
                              int width, int height,
                              float brightness,
                              float contrast) {
    float bright_offset = brightness * 128.0f;
    float32x4_t bright_vec = vdupq_n_f32(bright_offset);
    float32x4_t contrast_vec = vdupq_n_f32(contrast);
    float32x4_t offset_vec = vdupq_n_f32(128.0f);
    
    int count = width * height;
    int i = 0;
    
    // 处理 16 像素 (48 字节 RGB)
    for (; i + 16 <= count; i += 16) {
        // 加载 RGB 数据
        uint8x16x3_t rgb = vld3q_u8(input + i * 3);
        
        // 转换为浮点
        float32x4_t r = vcvtq_f32_u32(vmovl_u16(vget_low_u16(vmovl_u8(rgb.val[0]))));
        float32x4_t r2 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(vmovl_u8(rgb.val[0]))));
        float32x4_t g = vcvtq_f32_u32(vmovl_u16(vget_low_u16(vmovl_u8(rgb.val[1]))));
        float32x4_t g2 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(vmovl_u8(rgb.val[1]))));
        float32x4_t b = vcvtq_f32_u32(vmovl_u16(vget_low_u16(vmovl_u8(rgb.val[2]))));
        float32x4_t b2 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(vmovl_u8(rgb.val[2]))));
        
        // 亮度调整
        r = vaddq_f32(r, bright_vec);
        r2 = vaddq_f32(r2, bright_vec);
        g = vaddq_f32(g, bright_vec);
        g2 = vaddq_f32(g2, bright_vec);
        b = vaddq_f32(b, bright_vec);
        b2 = vaddq_f32(b2, bright_vec);
        
        // 对比度调整
        r = vsubq_f32(vmulq_f32(vsubq_f32(r, offset_vec), contrast_vec), offset_vec);
        r2 = vsubq_f32(vmulq_f32(vsubq_f32(r2, offset_vec), contrast_vec), offset_vec);
        g = vsubq_f32(vmulq_f32(vsubq_f32(g, offset_vec), contrast_vec), offset_vec);
        g2 = vsubq_f32(vmulq_f32(vsubq_f32(g2, offset_vec), contrast_vec), offset_vec);
        b = vsubq_f32(vmulq_f32(vsubq_f32(b, offset_vec), contrast_vec), offset_vec);
        b2 = vsubq_f32(vmulq_f32(vsubq_f32(b2, offset_vec), contrast_vec), offset_vec);
        
        // 限制并转换回字节
        r = vmaxq_f32(r, vdupq_n_f32(0.0f));
        r = vminq_f32(r, vdupq_n_f32(255.0f));
        r2 = vmaxq_f32(r2, vdupq_n_f32(0.0f));
        r2 = vminq_f32(r2, vdupq_n_f32(255.0f));
        g = vmaxq_f32(g, vdupq_n_f32(0.0f));
        g = vminq_f32(g, vdupq_n_f32(255.0f));
        g2 = vmaxq_f32(g2, vdupq_n_f32(0.0f));
        g2 = vminq_f32(g2, vdupq_n_f32(255.0f));
        b = vmaxq_f32(b, vdupq_n_f32(0.0f));
        b = vminq_f32(b, vdupq_n_f32(255.0f));
        b2 = vmaxq_f32(b2, vdupq_n_f32(0.0f));
        b2 = vminq_f32(b2, vdupq_n_f32(255.0f));
        
        rgb.val[0] = vcombine_u8(vqmovun_s32(vcvtq_s32_f32(r)), vqmovun_s32(vcvtq_s32_f32(r2)));
        rgb.val[1] = vcombine_u8(vqmovun_s32(vcvtq_s32_f32(g)), vqmovun_s32(vcvtq_s32_f32(g2)));
        rgb.val[2] = vcombine_u8(vqmovun_s32(vcvtq_s32_f32(b)), vqmovun_s32(vcvtq_s32_f32(b2)));
        
        vst3q_u8(output + i * 3, rgb);
    }
    
    // 处理剩余像素
    for (; i < count; i++) {
        int idx = i * 3;
        for (int c = 0; c < 3; c++) {
            float val = input[idx + c] + bright_offset;
            val = (val - 128.0f) * contrast + 128.0f;
            output[idx + c] = (uint8_t)std::max(0.0f, std::min(255.0f, val));
        }
    }
}

// ============================================================================
// 饱和度调整
// ============================================================================

void neon_saturation(const uint8_t* input,
                     uint8_t* output,
                     int width, int height,
                     float saturation) {
    float32x4_t sat_vec = vdupq_n_f32(saturation);
    float32x4_t gray_w_r = vdupq_n_f32(0.299f);
    float32x4_t gray_w_g = vdupq_n_f32(0.587f);
    float32x4_t gray_w_b = vdupq_n_f32(0.114f);
    
    int count = width * height;
    int i = 0;
    
    for (; i + 16 <= count; i += 16) {
        uint8x16x3_t rgb = vld3q_u8(input + i * 3);
        
        // 转换为浮点
        float32x4_t r = vcvtq_f32_u32(vmovl_u16(vget_low_u16(vmovl_u8(rgb.val[0]))));
        float32x4_t r2 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(vmovl_u8(rgb.val[0]))));
        float32x4_t g = vcvtq_f32_u32(vmovl_u16(vget_low_u16(vmovl_u8(rgb.val[1]))));
        float32x4_t g2 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(vmovl_u8(rgb.val[1]))));
        float32x4_t b = vcvtq_f32_u32(vmovl_u16(vget_low_u16(vmovl_u8(rgb.val[2]))));
        float32x4_t b2 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(vmovl_u8(rgb.val[2]))));
        
        // 计算灰度
        float32x4_t gray = vmulq_f32(r, gray_w_r);
        gray = vaddq_f32(gray, vmulq_f32(g, gray_w_g));
        gray = vaddq_f32(gray, vmulq_f32(b, gray_w_b));
        
        float32x4_t gray2 = vmulq_f32(r2, gray_w_r);
        gray2 = vaddq_f32(gray2, vmulq_f32(g2, gray_w_g));
        gray2 = vaddq_f32(gray2, vmulq_f32(b2, gray_w_b));
        
        // 调整饱和度
        r = vaddq_f32(gray, vmulq_f32(vsubq_f32(r, gray), sat_vec));
        r2 = vaddq_f32(gray2, vmulq_f32(vsubq_f32(r2, gray2), sat_vec));
        g = vaddq_f32(gray, vmulq_f32(vsubq_f32(g, gray), sat_vec));
        g2 = vaddq_f32(gray2, vmulq_f32(vsubq_f32(g2, gray2), sat_vec));
        b = vaddq_f32(gray, vmulq_f32(vsubq_f32(b, gray), sat_vec));
        b2 = vaddq_f32(gray2, vmulq_f32(vsubq_f32(b2, gray2), sat_vec));
        
        // 限制
        r = vmaxq_f32(r, vdupq_n_f32(0.0f));
        r = vminq_f32(r, vdupq_n_f32(255.0f));
        r2 = vmaxq_f32(r2, vdupq_n_f32(0.0f));
        r2 = vminq_f32(r2, vdupq_n_f32(255.0f));
        g = vmaxq_f32(g, vdupq_n_f32(0.0f));
        g = vminq_f32(g, vdupq_n_f32(255.0f));
        g2 = vmaxq_f32(g2, vdupq_n_f32(0.0f));
        g2 = vminq_f32(g2, vdupq_n_f32(255.0f));
        b = vmaxq_f32(b, vdupq_n_f32(0.0f));
        b = vminq_f32(b, vdupq_n_f32(255.0f));
        b2 = vmaxq_f32(b2, vdupq_n_f32(0.0f));
        b2 = vminq_f32(b2, vdupq_n_f32(255.0f));
        
        rgb.val[0] = vcombine_u8(vqmovun_s32(vcvtq_s32_f32(r)), vqmovun_s32(vcvtq_s32_f32(r2)));
        rgb.val[1] = vcombine_u8(vqmovun_s32(vcvtq_s32_f32(g)), vqmovun_s32(vcvtq_s32_f32(g2)));
        rgb.val[2] = vcombine_u8(vqmovun_s32(vcvtq_s32_f32(b)), vqmovun_s32(vcvtq_s32_f32(b2)));
        
        vst3q_u8(output + i * 3, rgb);
    }
    
    for (; i < count; i++) {
        int idx = i * 3;
        float r = input[idx], g = input[idx + 1], b = input[idx + 2];
        float gray = 0.299f * r + 0.587f * g + 0.114f * b;
        output[idx] = (uint8_t)std::max(0.0f, std::min(255.0f, gray + (r - gray) * saturation));
        output[idx + 1] = (uint8_t)std::max(0.0f, std::min(255.0f, gray + (g - gray) * saturation));
        output[idx + 2] = (uint8_t)std::max(0.0f, std::min(255.0f, gray + (b - gray) * saturation));
    }
}

// ============================================================================
// RGB 转灰度
// ============================================================================

void neon_rgb_to_grayscale(const uint8_t* rgb,
                          uint8_t* gray,
                          int width, int height) {
    // 权重预乘: 77, 150, 29 (整数近似 0.299*256, 0.587*256, 0.114*256)
    uint16x8_t w_r = vdup_n_u16(77);
    uint16x8_t w_g = vdup_n_u16(150);
    uint16x8_t w_b = vdup_n_u16(29);
    
    int count = width * height;
    int i = 0;
    
    for (; i + 8 <= count; i += 8) {
        uint8x8x3_t rgb8 = vld3_u8(rgb + i * 3);
        
        uint16x8_t r = vmovl_u8(rgb8.val[0]);
        uint16x8_t g = vmovl_u8(rgb8.val[1]);
        uint16x8_t b = vmovl_u8(rgb8.val[2]);
        
        uint16x8_t gray16 = vmlaq_u16(vmlaq_u16(vmulq_u16(r, w_r), g, w_g), b, w_b);
        gray16 = vshrq_n_u16(gray16, 8);
        
        vst1_u8(gray + i, vmovn_u16(gray16));
    }
    
    for (; i < count; i++) {
        gray[i] = (uint8_t)((77 * rgb[i*3] + 150 * rgb[i*3+1] + 29 * rgb[i*3+2]) >> 8);
    }
}

// ============================================================================
// GSDF 查表应用
// ============================================================================

void neon_gsdf_lut_apply(const uint8_t* input,
                         uint8_t* output,
                         int width, int height,
                         const uint8_t* lut) {
    int count = width * height * 3;
    int i = 0;
    
    // 加载 LUT 到向量
    uint8x16_t lut_vec0 = vld1q_u8(lut);
    uint8x16_t lut_vec1 = vld1q_u8(lut + 16);
    uint8x16_t lut_vec2 = vld1q_u8(lut + 32);
    uint8x16_t lut_vec3 = vld1q_u8(lut + 48);
    uint8x16_t lut_vec4 = vld1q_u8(lut + 64);
    uint8x16_t lut_vec5 = vld1q_u8(lut + 80);
    uint8x16_t lut_vec6 = vld1q_u8(lut + 96);
    uint8x16_t lut_vec7 = vld1q_u8(lut + 112);
    uint8x16_t lut_vec8 = vld1q_u8(lut + 128);
    uint8x16_t lut_vec9 = vld1q_u8(lut + 144);
    uint8x16_t lut_vec10 = vld1q_u8(lut + 160);
    uint8x16_t lut_vec11 = vld1q_u8(lut + 176);
    uint8x16_t lut_vec12 = vld1q_u8(lut + 192);
    uint8x16_t lut_vec13 = vld1q_u8(lut + 208);
    uint8x16_t lut_vec14 = vld1q_u8(lut + 224);
    uint8x16_t lut_vec15 = vld1q_u8(lut + 240);
    
    for (; i + 48 <= count; i += 48) {
        uint8x16_t val0 = vld1q_u8(input + i);
        uint8x16_t val1 = vld1q_u8(input + i + 16);
        uint8x16_t val2 = vld1q_u8(input + i + 32);
        
        // 使用查表指令 (如果可用) 或简化处理
        uint8x16_t res0, res1, res2;
        
        // 简化: 线性插值 (实际应使用 PSHUFB 等指令)
        for (int j = 0; j < 16; j++) {
            res0[j] = lut[val0[j]];
            res1[j] = lut[val1[j]];
            res2[j] = lut[val2[j]];
        }
        
        vst1q_u8(output + i, res0);
        vst1q_u8(output + i + 16, res1);
        vst1q_u8(output + i + 32, res2);
    }
    
    for (; i < count; i++) {
        output[i] = lut[input[i]];
    }
}

// ============================================================================
// Sobel 边缘检测
// ============================================================================

void neon_sobel_edge(const uint8_t* gray,
                      uint8_t* edge,
                      int width, int height,
                      float threshold) {
    int threshold_i = (int)(threshold * 256);  // 放大阈值
    
    for (int y = 1; y < height - 1; y++) {
        int i = y * width + 1;
        
        for (int x = 1; x < width - 1; x++, i++) {
            // Sobel X
            int gx = 0;
            gx -= gray[i - width - 1] + 2 * gray[i - 1] + gray[i + width - 1];
            gx += gray[i - width + 1] + 2 * gray[i + 1] + gray[i + width + 1];
            
            // Sobel Y
            int gy = 0;
            gy -= gray[i - width - 1] + 2 * gray[i - width] + gray[i - width + 1];
            gy += gray[i + width - 1] + 2 * gray[i + width] + gray[i + width + 1];
            
            // 梯度幅度 (近似)
            int mag = (abs(gx) + abs(gy) + 1) >> 1;
            
            edge[i] = (mag > threshold_i) ? 255 : 0;
        }
    }
}

// ============================================================================
// 无血术野增强
// ============================================================================

void neon_bloodless_enhance(const uint8_t* input,
                             uint8_t* output,
                             int width, int height,
                             float suppress_level,
                             float tissue_enhance,
                             float edge_preserve) {
    float32x4_t suppress = vdupq_n_f32(suppress_level);
    float32x4_t enhance = vdupq_n_f32(1.0f + tissue_enhance * 0.2f);
    
    int count = width * height;
    int i = 0;
    
    for (; i + 4 <= count; i += 4) {
        uint8x8x3_t rgb8 = vld3_u8(input + i * 3);
        
        // 转换为浮点
        float32x4_t r = vcvtq_f32_u32(vmovl_u16(vmovl_u8(rgb8.val[0])));
        float32x4_t g = vcvtq_f32_u32(vmovl_u16(vmovl_u8(rgb8.val[1])));
        float32x4_t b = vcvtq_f32_u32(vmovl_u16(vmovl_u8(rgb8.val[2])));
        
        // 血色检测
        float32x4_t blood = vmulq_f32(r, vdupq_n_f32(0.5f));
        blood = vsubq_f32(blood, vmulq_f32(g, vdupq_n_f32(0.3f)));
        blood = vsubq_f32(blood, vmulq_f32(b, vdupq_n_f32(0.2f)));
        blood = vmaxq_f32(vdupq_n_f32(0.0f), blood);
        blood = vminq_f32(vdupq_n_f32(1.0f), vmulq_f32(blood, vdupq_n_f32(1.0f/128.0f)));
        
        // 抑制血色
        float32x4_t suppress_factor = vmulq_f32(blood, suppress);
        r = vmulq_f32(r, vsubq_f32(vdupq_n_f32(1.0f), vmulq_f32(suppress_factor, vdupq_n_f32(0.3f))));
        g = vmulq_f32(g, vaddq_f32(vdupq_n_f32(1.0f), vmulq_f32(suppress_factor, vdupq_n_f32(0.2f))));
        b = vmulq_f32(b, vaddq_f32(vdupq_n_f32(1.0f), vmulq_f32(suppress_factor, vdupq_n_f32(0.2f))));
        
        // 限制
        r = vmaxq_f32(r, vdupq_n_f32(0.0f));
        r = vminq_f32(r, vdupq_n_f32(255.0f));
        g = vmaxq_f32(g, vdupq_n_f32(0.0f));
        g = vminq_f32(g, vdupq_n_f32(255.0f));
        b = vmaxq_f32(b, vdupq_n_f32(0.0f));
        b = vminq_f32(b, vdupq_n_f32(255.0f));
        
        rgb8.val[0] = vqmovun_s32(vcvtq_s32_f32(r));
        rgb8.val[1] = vqmovun_s32(vcvtq_s32_f32(g));
        rgb8.val[2] = vqmovun_s32(vcvtq_s32_f32(b));
        
        vst3_u8(output + i * 3, rgb8);
    }
    
    for (; i < count; i++) {
        int idx = i * 3;
        float r = input[idx], g = input[idx + 1], b = input[idx + 2];
        
        float blood_score = r * 0.5f - g * 0.3f - b * 0.2f;
        float blood_mask = std::max(0.0f, std::min(1.0f, blood_score / 128.0f));
        
        float suppress = blood_mask * suppress_level;
        r = r * (1.0f - suppress * 0.3f);
        g = g * (1.0f + suppress * 0.2f);
        b = b * (1.0f + suppress * 0.2f);
        
        float gray = 0.299f * r + 0.587f * g + 0.114f * b;
        r = gray + (r - gray) * (1.0f + tissue_enhance * 0.2f);
        g = gray + (g - gray) * (1.0f + tissue_enhance * 0.2f);
        b = gray + (b - gray) * (1.0f + tissue_enhance * 0.2f);
        
        output[idx] = (uint8_t)std::max(0.0f, std::min(255.0f, r));
        output[idx + 1] = (uint8_t)std::max(0.0f, std::min(255.0f, g));
        output[idx + 2] = (uint8_t)std::max(0.0f, std::min(255.0f, b));
    }
}

#endif // __ARM_NEON__
