#version 450

// Input from vertex shader
layout(location = 0) in vec2 vTexCoord;

// Output
layout(location = 0) out vec4 fragColor;

// Descriptor sets
layout(set = 0, binding = 1) uniform sampler2D inputTexture;    // DICOM影像
layout(set = 0, binding = 2) uniform sampler1D gsdfLUT;          // GSDF 1D LUT
layout(set = 0, binding = 3) uniform sampler3D colorSpaceLUT;    // 3D色彩空间转换
layout(set = 0, binding = 4) uniform sampler2D enhanceMask;      // AI增强掩码

// Push constants
layout(push_constant) uniform PushConstants {
    // 显示参数
    float gamma;              // Gamma值
    int colorSpace;           // 色彩空间
    int enableGSDF;           // 启用GSDF
    int enableLocalEnhance;    // 启用局部增强
    
    // 窗口/层级 (CT/MRI)
    float windowCenter;
    float windowWidth;
    float windowMin;
    float windowMax;
    
    // 增强参数
    float sharpness;
    float contrast;
    float brightness;
    
    // 模态
    int modality;
} pc;

// 窗口/层级变换
vec3 applyWindowLevel(vec3 value) {
    if (pc.windowWidth <= 0.0) return value;
    
    float windowMin = pc.windowCenter - pc.windowWidth * 0.5;
    float windowMax = pc.windowCenter + pc.windowWidth * 0.5;
    float normalized = (value.r - windowMin) / (windowMax - windowMin);
    normalized = clamp(normalized, 0.0, 1.0);
    return vec3(normalized);
}

// 边缘检测 (Sobel)
float sobelEdge(vec2 uv) {
    vec2 texel = pc.windowMin; // 复用为texelSize
    
    float tl = texture(inputTexture, uv + vec2(-texel.x, -texel.y)).r;
    float t  = texture(inputTexture, uv + vec2(0, -texel.y)).r;
    float tr = texture(inputTexture, uv + vec2(texel.x, -texel.y)).r;
    float l  = texture(inputTexture, uv + vec2(-texel.x, 0)).r;
    float r  = texture(inputTexture, uv + vec2(texel.x, 0)).r;
    float bl = texture(inputTexture, uv + vec2(-texel.x, texel.y)).r;
    float b  = texture(inputTexture, uv + vec2(0, texel.y)).r;
    float br = texture(inputTexture, uv + vec2(texel.x, texel.y)).r;
    
    float gx = tl + 2.0*l + bl - tr - 2.0*r - br;
    float gy = tl + 2.0*t + tr - bl - 2.0*b - br;
    
    return sqrt(gx*gx + gy*gy);
}

// 局部增强
vec3 applyLocalEnhance(vec3 color, vec2 uv) {
    // 边缘检测
    float edge = sobelEdge(uv);
    
    // 锐化 (USM - Unsharp Mask)
    vec3 blur = texture(inputTexture, uv, 2).rgb; // 大模糊
    vec3 sharp = color - blur;
    color += sharp * pc.sharpness * 0.5;
    
    // 对比度调整
    color = (color - 0.5) * pc.contrast + 0.5;
    
    // 亮度调整
    color = color * pc.brightness;
    
    // 边缘增强 (根据模态)
    if (pc.modality == 1) { // CT
        color += vec3(edge * 0.15); // 骨骼增强
    } else if (pc.modality == 5) { // 超声
        // 血流增强等
    }
    
    return color;
}

// GSDF应用
vec3 applyGSDF(vec3 linearValue) {
    // 提取亮度
    float luminance = dot(linearValue, vec3(0.2126, 0.7152, 0.0722));
    
    // LUT查找
    float gsdfValue = texture(gsdfLUT, vec2(luminance, 0.5)).r;
    
    // 保持色彩比例
    return linearValue * (gsdfValue / (luminance + 0.0001));
}

// 色彩空间转换
vec3 convertColorSpace(vec3 color) {
    if (pc.colorSpace == 0) { // sRGB
        return color;
    } else if (pc.colorSpace == 1) { // DCI-P3
        return texture(colorSpaceLUT, color).rgb;
    } else if (pc.colorSpace == 2) { // Rec.2020
        return texture(colorSpaceLUT, color).rgb;
    }
    return color;
}

void main() {
    // 采样原始影像
    vec4 rawColor = texture(inputTexture, vTexCoord);
    vec3 color = rawColor.rgb;
    
    // 窗口/层级变换
    color = applyWindowLevel(color);
    
    // 局部增强
    if (pc.enableLocalEnhance == 1) {
        color = applyLocalEnhance(color, vTexCoord);
    }
    
    // 色彩空间转换
    color = convertColorSpace(color);
    
    // GSDF校正
    if (pc.enableGSDF == 1) {
        color = applyGSDF(color);
    }
    
    // Gamma校正
    color = pow(color, vec3(1.0 / pc.gamma));
    
    // 范围限制
    color = clamp(color, 0.0, 1.0);
    
    fragColor = vec4(color, rawColor.a);
}
