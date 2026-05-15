#version 450

// ============================================================================
// GSDF LUT Compute Shader
// - DICOM GSDF 预计算
// - 12-bit LUT 生成
// - 环境光补偿
// ============================================================================

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) buffer GsdfCodeLutBuffer {
    uint codeLut[];
};

layout(set = 0, binding = 1, std430) buffer GsdfLuminanceLutBuffer {
    float luminanceLut[];
};

layout(push_constant) uniform GsdfPushConstants {
    uint lutSize;
    float minPanelLuminance;
    float maxPanelLuminance;
    float ambientLux;
    float panelReflectance;
    float ambientCompensation;
    float panelGamma;
    float reserved0;
} pc;

const float PI = 3.14159265359;
const float EPSILON = 1e-6;
const float GSDF_JND_MIN = 1.0;
const float GSDF_JND_MAX = 1023.0;

float ambientVeilingLuminance() {
    float reflected = max(pc.ambientLux, 0.0) * max(pc.panelReflectance, 0.0) / PI;
    return reflected * clamp(pc.ambientCompensation, 0.0, 4.0);
}

float luminanceFromJnd(float jndIndex) {
    float y = log(max(jndIndex, GSDF_JND_MIN));
    float y2 = y * y;
    float y3 = y2 * y;
    float y4 = y2 * y2;
    float y5 = y4 * y;

    float numerator =
        -1.3011877 +
         0.080242636 * y +
         0.13646699 * y2 +
        -0.025468404 * y3 +
         0.0013635334 * y4;

    float denominator =
         1.0 +
        -0.025840191 * y +
        -0.10320229 * y2 +
         0.02874562 * y3 +
        -0.0031978977 * y4 +
         0.00012992634 * y5;

    return pow(10.0, numerator / max(denominator, EPSILON));
}

float jndFromLuminance(float luminance) {
    float target = max(luminance, EPSILON);
    float low = GSDF_JND_MIN;
    float high = GSDF_JND_MAX;

    for (int i = 0; i < 24; ++i) {
        float mid = 0.5 * (low + high);
        float luminanceAtMid = luminanceFromJnd(mid);

        if (luminanceAtMid < target) {
            low = mid;
        } else {
            high = mid;
        }
    }

    return 0.5 * (low + high);
}

float encodePanelDrive(float luminance, float minLuminance, float maxLuminance) {
    float normalized = (luminance - minLuminance) / max(maxLuminance - minLuminance, EPSILON);
    normalized = clamp(normalized, 0.0, 1.0);

    float gamma = max(pc.panelGamma, EPSILON);
    return pow(normalized, 1.0 / gamma);
}

void main() {
    uint index = gl_GlobalInvocationID.x;
    uint lutSize = clamp(pc.lutSize, 1u, 4096u);

    if (index >= lutSize) {
        return;
    }

    float ambientLift = ambientVeilingLuminance();
    float compensatedMin = max(pc.minPanelLuminance + ambientLift, EPSILON);
    float compensatedMax = max(pc.maxPanelLuminance + ambientLift, compensatedMin + EPSILON);

    float minJnd = jndFromLuminance(compensatedMin);
    float maxJnd = jndFromLuminance(compensatedMax);
    float t = lutSize > 1u ? float(index) / float(lutSize - 1u) : 0.0;

    float targetJnd = mix(minJnd, maxJnd, t);
    float targetLuminance = clamp(luminanceFromJnd(targetJnd), compensatedMin, compensatedMax);
    float drive = encodePanelDrive(targetLuminance, compensatedMin, compensatedMax);

    codeLut[index] = min(uint(round(drive * 4095.0)), 4095u);
    luminanceLut[index] = targetLuminance;
}
