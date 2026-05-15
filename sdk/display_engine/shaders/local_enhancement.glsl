#version 450

// ============================================================================
// Local Enhancement Compute Shader
// - 边缘锐化
// - 噪声抑制
// - ROI 增强
// ============================================================================

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D srcImage;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D dstImage;
layout(set = 0, binding = 2) uniform sampler2D roiMask;

layout(push_constant) uniform LocalEnhancementPushConstants {
    float sharpenStrength;
    float denoiseStrength;
    float edgeThreshold;
    float rangeSigma;
    float roiBoost;
    float detailLimit;
    int enableRoiMask;
    int reserved0;
} pc;

const float EPSILON = 1e-6;
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

ivec2 clampPixel(ivec2 pixel, ivec2 imageSizePx) {
    return clamp(pixel, ivec2(0), imageSizePx - ivec2(1));
}

vec3 fetchColor(ivec2 pixel, ivec2 imageSizePx) {
    return texelFetch(srcImage, clampPixel(pixel, imageSizePx), 0).rgb;
}

float luminanceOf(vec3 color) {
    return dot(color, LUMA);
}

vec3 gaussianBlur3x3(ivec2 pixel, ivec2 imageSizePx) {
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float weight = (x == 0 && y == 0) ? 4.0 : ((x == 0 || y == 0) ? 2.0 : 1.0);
            sum += fetchColor(pixel + ivec2(x, y), imageSizePx) * weight;
            weightSum += weight;
        }
    }

    return sum / max(weightSum, EPSILON);
}

vec3 bilateralFilter3x3(ivec2 pixel, ivec2 imageSizePx, vec3 centerColor) {
    float sigma = max(pc.rangeSigma, 1e-3);
    float centerLuma = luminanceOf(centerColor);
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec3 sampleColor = fetchColor(pixel + ivec2(x, y), imageSizePx);
            float delta = luminanceOf(sampleColor) - centerLuma;
            float spatial = float(x * x + y * y);
            float spatialWeight = exp(-0.5 * spatial);
            float rangeWeight = exp(-(delta * delta) / (2.0 * sigma * sigma));
            float weight = spatialWeight * rangeWeight;

            sum += sampleColor * weight;
            weightSum += weight;
        }
    }

    return sum / max(weightSum, EPSILON);
}

float sobelEdge(ivec2 pixel, ivec2 imageSizePx) {
    float tl = luminanceOf(fetchColor(pixel + ivec2(-1, -1), imageSizePx));
    float t  = luminanceOf(fetchColor(pixel + ivec2( 0, -1), imageSizePx));
    float tr = luminanceOf(fetchColor(pixel + ivec2( 1, -1), imageSizePx));
    float l  = luminanceOf(fetchColor(pixel + ivec2(-1,  0), imageSizePx));
    float r  = luminanceOf(fetchColor(pixel + ivec2( 1,  0), imageSizePx));
    float bl = luminanceOf(fetchColor(pixel + ivec2(-1,  1), imageSizePx));
    float b  = luminanceOf(fetchColor(pixel + ivec2( 0,  1), imageSizePx));
    float br = luminanceOf(fetchColor(pixel + ivec2( 1,  1), imageSizePx));

    float gx = -tl - 2.0 * l - bl + tr + 2.0 * r + br;
    float gy = -tl - 2.0 * t - tr + bl + 2.0 * b + br;

    return sqrt(gx * gx + gy * gy);
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imageSizePx = textureSize(srcImage, 0);

    if (any(greaterThanEqual(pixel, imageSizePx))) {
        return;
    }

    vec4 src = texelFetch(srcImage, pixel, 0);
    vec3 centerColor = src.rgb;
    vec2 uv = (vec2(pixel) + 0.5) / vec2(imageSizePx);

    float roi = pc.enableRoiMask != 0 ? clamp(texture(roiMask, uv).r, 0.0, 1.0) : 0.0;
    float edge = sobelEdge(pixel, imageSizePx);
    float threshold = max(pc.edgeThreshold, 1e-4);
    float edgeMask = smoothstep(threshold, threshold * 2.5 + 1e-4, edge);

    vec3 denoised = bilateralFilter3x3(pixel, imageSizePx, centerColor);
    vec3 base = mix(centerColor, denoised, clamp(pc.denoiseStrength, 0.0, 1.0) * (1.0 - edgeMask));

    vec3 blurred = gaussianBlur3x3(pixel, imageSizePx);
    float detailClamp = max(pc.detailLimit, 0.0);
    vec3 detail = clamp(base - blurred, vec3(-detailClamp), vec3(detailClamp));

    float sharpen = clamp(pc.sharpenStrength, 0.0, 4.0);
    float roiFactor = 1.0 + roi * max(pc.roiBoost, 0.0);
    vec3 enhanced = base + detail * sharpen * mix(0.35, 1.0, edgeMask) * roiFactor;

    float roiContrast = 1.0 + roi * max(pc.roiBoost, 0.0) * 0.2;
    enhanced = (enhanced - 0.5) * roiContrast + 0.5;
    enhanced = clamp(enhanced, 0.0, 1.0);

    imageStore(dstImage, pixel, vec4(enhanced, src.a));
}
