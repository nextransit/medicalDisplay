#version 450

// ============================================================================
// Color Space Compute Shader
// - sRGB <-> Linear
// - DCI-P3 / Rec.2020 矩阵变换
// - 3D LUT 采样
// ============================================================================

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D srcImage;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D dstImage;
layout(set = 0, binding = 2) uniform sampler3D lut3D;

layout(push_constant) uniform ColorSpacePushConstants {
    int inputTransfer;
    int outputTransfer;
    int inputPrimaries;
    int outputPrimaries;
    int enable3DLut;
    float lutStrength;
    float exposure;
    float saturation;
} pc;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

const mat3 SRGB_TO_XYZ = mat3(
    vec3(0.4124564, 0.2126729, 0.0193339),
    vec3(0.3575761, 0.7151522, 0.1191920),
    vec3(0.1804375, 0.0721750, 0.9503041)
);

const mat3 XYZ_TO_SRGB = mat3(
    vec3( 3.2404542, -0.9692660,  0.0556434),
    vec3(-1.5371385,  1.8760108, -0.2040259),
    vec3(-0.4985314,  0.0415560,  1.0572252)
);

const mat3 P3_TO_XYZ = mat3(
    vec3(0.4865709, 0.2289746, 0.0000000),
    vec3(0.2656676, 0.6917385, 0.0451134),
    vec3(0.1982173, 0.0792869, 1.0439444)
);

const mat3 XYZ_TO_P3 = mat3(
    vec3( 2.4934969, -0.8294889,  0.0358458),
    vec3(-0.9313836,  1.7626640, -0.0761724),
    vec3(-0.4027107,  0.0236247,  0.9568845)
);

const mat3 BT2020_TO_XYZ = mat3(
    vec3(0.6369580, 0.2627002, 0.0000000),
    vec3(0.1446169, 0.6779981, 0.0280727),
    vec3(0.1688809, 0.0593017, 1.0609851)
);

const mat3 XYZ_TO_BT2020 = mat3(
    vec3( 1.7166512, -0.6666844,  0.0176399),
    vec3(-0.3556708,  1.6164812, -0.0427706),
    vec3(-0.2533663,  0.0157685,  0.9421031)
);

float srgbToLinearScalar(float value) {
    return value <= 0.04045
        ? value / 12.92
        : pow((value + 0.055) / 1.055, 2.4);
}

float linearToSrgbScalar(float value) {
    return value <= 0.0031308
        ? value * 12.92
        : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

vec3 decodeTransfer(vec3 color, int transfer) {
    color = clamp(color, 0.0, 1.0);

    if (transfer == 1) {
        return vec3(
            srgbToLinearScalar(color.r),
            srgbToLinearScalar(color.g),
            srgbToLinearScalar(color.b)
        );
    }

    return color;
}

vec3 encodeTransfer(vec3 color, int transfer) {
    color = max(color, vec3(0.0));

    if (transfer == 1) {
        return vec3(
            linearToSrgbScalar(color.r),
            linearToSrgbScalar(color.g),
            linearToSrgbScalar(color.b)
        );
    }

    return color;
}

mat3 toXyzMatrix(int primaries) {
    if (primaries == 1) {
        return P3_TO_XYZ;
    }
    if (primaries == 2) {
        return BT2020_TO_XYZ;
    }
    return SRGB_TO_XYZ;
}

mat3 fromXyzMatrix(int primaries) {
    if (primaries == 1) {
        return XYZ_TO_P3;
    }
    if (primaries == 2) {
        return XYZ_TO_BT2020;
    }
    return XYZ_TO_SRGB;
}

vec3 applySaturation(vec3 color, float saturation) {
    float luminance = dot(color, LUMA);
    return mix(vec3(luminance), color, max(saturation, 0.0));
}

vec3 apply3DLut(vec3 color) {
    vec3 baseColor = clamp(color, 0.0, 1.0);

    if (pc.enable3DLut == 0) {
        return baseColor;
    }

    vec3 lutColor = texture(lut3D, baseColor).rgb;
    return mix(baseColor, lutColor, clamp(pc.lutStrength, 0.0, 1.0));
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imageSizePx = textureSize(srcImage, 0);

    if (any(greaterThanEqual(pixel, imageSizePx))) {
        return;
    }

    vec4 src = texelFetch(srcImage, pixel, 0);
    vec3 linear = decodeTransfer(src.rgb, pc.inputTransfer);
    vec3 xyz = toXyzMatrix(pc.inputPrimaries) * linear;
    vec3 transformed = fromXyzMatrix(pc.outputPrimaries) * xyz;

    transformed = max(transformed * exp2(pc.exposure), vec3(0.0));
    transformed = applySaturation(transformed, pc.saturation);
    transformed = apply3DLut(transformed);

    vec3 encoded = clamp(encodeTransfer(transformed, pc.outputTransfer), 0.0, 1.0);
    imageStore(dstImage, pixel, vec4(encoded, src.a));
}
