#version 450

// ============================================================================
// HDR Tone Mapping Compute Shader
// - HLG OETF / EOTF
// - PQ (ST.2084) 转换
// - Local Dimming
// ============================================================================

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D hdrInput;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D hdrOutput;
layout(set = 0, binding = 2) uniform sampler2D localDimmingMap;

layout(push_constant) uniform HdrPushConstants {
    int inputTransfer;
    int outputTransfer;
    int enableToneMapping;
    int enableLocalDimming;
    float referenceWhiteNits;
    float contentPeakNits;
    float displayPeakNits;
    float displayBlackNits;
    float localDimmingStrength;
    float highlightKnee;
    float shadowLift;
    float reserved0;
} pc;

const float EPSILON = 1e-6;
const vec3 LUMA = vec3(0.2627, 0.6780, 0.0593);

const float HLG_A = 0.17883277;
const float HLG_B = 1.0 - 4.0 * HLG_A;
const float HLG_C = 0.5 - HLG_A * log(4.0 * HLG_A);

const float PQ_M1 = 2610.0 / 16384.0;
const float PQ_M2 = 2523.0 / 32.0;
const float PQ_C1 = 3424.0 / 4096.0;
const float PQ_C2 = 2413.0 / 128.0;
const float PQ_C3 = 2392.0 / 128.0;

float hlgOetfScalar(float linearValue) {
    float value = clamp(linearValue, 0.0, 1.0);
    return value <= (1.0 / 12.0)
        ? sqrt(3.0 * value)
        : HLG_A * log(12.0 * value - HLG_B) + HLG_C;
}

float hlgEotfScalar(float encodedValue) {
    float value = clamp(encodedValue, 0.0, 1.0);
    return value <= 0.5
        ? (value * value) / 3.0
        : (exp((value - HLG_C) / HLG_A) + HLG_B) / 12.0;
}

float pqEotfScalar(float encodedValue) {
    float value = clamp(encodedValue, 0.0, 1.0);
    float p = pow(value, 1.0 / PQ_M2);
    float numerator = max(p - PQ_C1, 0.0);
    float denominator = max(PQ_C2 - PQ_C3 * p, EPSILON);
    return 10000.0 * pow(numerator / denominator, 1.0 / PQ_M1);
}

float pqOetfScalar(float luminanceNits) {
    float normalized = pow(clamp(luminanceNits / 10000.0, 0.0, 1.0), PQ_M1);
    float numerator = PQ_C1 + PQ_C2 * normalized;
    float denominator = 1.0 + PQ_C3 * normalized;
    return pow(numerator / max(denominator, EPSILON), PQ_M2);
}

vec3 decodeHdr(vec3 color) {
    if (pc.inputTransfer == 1) {
        return vec3(
            hlgEotfScalar(color.r),
            hlgEotfScalar(color.g),
            hlgEotfScalar(color.b)
        ) * max(pc.contentPeakNits, 1.0);
    }

    if (pc.inputTransfer == 2) {
        return vec3(
            pqEotfScalar(color.r),
            pqEotfScalar(color.g),
            pqEotfScalar(color.b)
        );
    }

    return max(color, vec3(0.0)) * max(pc.contentPeakNits, 1.0);
}

vec3 encodeHdr(vec3 luminanceNits) {
    if (pc.outputTransfer == 1) {
        float peak = max(pc.displayPeakNits, 1.0);
        vec3 normalized = clamp(luminanceNits / peak, 0.0, 1.0);
        return vec3(
            hlgOetfScalar(normalized.r),
            hlgOetfScalar(normalized.g),
            hlgOetfScalar(normalized.b)
        );
    }

    if (pc.outputTransfer == 2) {
        return vec3(
            pqOetfScalar(luminanceNits.r),
            pqOetfScalar(luminanceNits.g),
            pqOetfScalar(luminanceNits.b)
        );
    }

    return clamp(luminanceNits / max(pc.displayPeakNits, 1.0), 0.0, 1.0);
}

float localPeakNits(ivec2 pixel, ivec2 imageSizePx) {
    if (pc.enableLocalDimming == 0) {
        return max(pc.displayPeakNits, pc.displayBlackNits + EPSILON);
    }

    vec2 uv = (vec2(pixel) + 0.5) / vec2(imageSizePx);
    float backlight = clamp(texture(localDimmingMap, uv).r, 0.0, 1.0);
    float dimmedPeak = max(pc.displayBlackNits, backlight * pc.displayPeakNits);

    return max(
        mix(pc.displayPeakNits, dimmedPeak, clamp(pc.localDimmingStrength, 0.0, 1.0)),
        pc.displayBlackNits + EPSILON
    );
}

float toneMapLuminance(float sourceNits, float peakNits) {
    float clampedSource = max(sourceNits, 0.0);

    if (pc.enableToneMapping == 0) {
        return clamp(clampedSource, pc.displayBlackNits, peakNits);
    }

    float knee = clamp(pc.highlightKnee, 0.35, 0.95);
    float kneeNits = knee * peakNits;

    if (clampedSource <= kneeNits) {
        return max(clampedSource, pc.displayBlackNits);
    }

    float sourcePeak = max(pc.contentPeakNits, peakNits);
    float over = max((clampedSource - kneeNits) / max(sourcePeak - kneeNits, EPSILON), 0.0);
    float compressed = 1.0 - exp(-over * 4.0);
    return mix(kneeNits, peakNits, compressed);
}

vec3 applyShadowLift(vec3 normalizedColor) {
    float lift = clamp(pc.shadowLift, 0.0, 1.0);
    return mix(normalizedColor, sqrt(normalizedColor), lift);
}

vec3 toneMapRgb(vec3 sourceNits, float peakNits) {
    float luminance = max(dot(sourceNits, LUMA), EPSILON);
    float mappedLuminance = toneMapLuminance(luminance, peakNits);
    float chromaScale = mappedLuminance / luminance;

    vec3 mapped = min(sourceNits * chromaScale, vec3(peakNits));
    vec3 normalized = clamp(mapped / peakNits, 0.0, 1.0);
    normalized = applyShadowLift(normalized);

    return max(normalized * peakNits, vec3(pc.displayBlackNits));
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imageSizePx = textureSize(hdrInput, 0);

    if (any(greaterThanEqual(pixel, imageSizePx))) {
        return;
    }

    vec4 src = texelFetch(hdrInput, pixel, 0);
    vec3 sourceNits = decodeHdr(src.rgb);
    float peakNits = localPeakNits(pixel, imageSizePx);
    vec3 mappedNits = toneMapRgb(sourceNits, peakNits);
    vec3 encoded = clamp(encodeHdr(mappedNits), 0.0, 1.0);

    imageStore(hdrOutput, pixel, vec4(encoded, src.a));
}
