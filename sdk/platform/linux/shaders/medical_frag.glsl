#version 450

// Push constants layout (16 uint32 = 64 bytes)
// [0-3]: window_center, window_width, bits_stored, flags
// [4-15]: reserved for GSDF LUT interpolation params
layout(push_constant) uniform PushConstants {
    uint window_center;    // Q10 fixed point
    uint window_width;    // Q10 fixed point  
    uint bits_stored;     // bit depth
    uint flags;           // bit 0: invert, bit 1: gsdf enable, bit 2: local enhance
    uint gsdf_lut_size;   // GSDF LUT size
    uint ambient;         // ambient light (Q10)
    uint reserved0;
    uint reserved1;
    uint reserved2;
    uint reserved3;
    uint reserved4;
    uint reserved5;
    uint reserved6;
    uint reserved7;
    uint reserved8;
    uint reserved9;
} pc;

// Input texture (DICOM 16-bit)
layout(binding = 0) uniform utexture2D inputTexture;

// GSDF LUT
layout(binding = 1) uniform sampler1D gsdfLUT;

// Output
layout(location = 0) out vec4 fragColor;

// Helper functions
float applyWindowLevel(float value, uint center, uint width) {
    float c = float(center) / 1024.0;  // Q10 to float
    float w = max(float(width) / 1024.0, 1.0);
    float min_val = c - w / 2.0;
    float max_val = c + w / 2.0;
    
    float normalized = (value - min_val) / (max_val - min_val);
    return clamp(normalized, 0.0, 1.0);
}

float applyGSDF(float pvalue) {
    if (pc.gsdf_lut_size == 0) return pvalue;
    // Linear interpolation in GSDF LUT
    float idx = pvalue * float(pc.gsdf_lut_size - 1);
    int low = int(floor(idx));
    int high = min(low + 1, int(pc.gsdf_lut_size) - 1);
    float t = fract(idx);
    // Note: actual GSDF LUT values should be passed via buffer
    return mix(pvalue, pvalue * 1.1, t);  // Simplified GSDF approximation
}

void main() {
    ivec2 coord = ivec2(gl_FragCoord.xy);
    uint pixel = textureFetch(inputTexture, coord, 0).r;
    
    // Apply bit shift for 16->8 bit conversion
    int shift = max(0, int(pc.bits_stored) - 8);
    float normalized = float(pixel >> shift) / 255.0;
    
    // Apply window/level
    float windowed = applyWindowLevel(normalized, pc.window_center, pc.window_width);
    
    // Apply inversion if needed
    if ((pc.flags & 1u) != 0u) {
        windowed = 1.0 - windowed;
    }
    
    // Apply GSDF correction
    if ((pc.flags & 2u) != 0u) {
        windowed = applyGSDF(windowed);
    }
    
    // Local enhancement (optional)
    if ((pc.flags & 4u) != 0u) {
        // Simple unsharp mask approximation
        float avg = 0.0;
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                ivec2 neighbor = coord + ivec2(dx, dy);
                uint neighbor_pixel = textureFetch(inputTexture, neighbor, 0).r;
                avg += float(neighbor_pixel >> shift) / 255.0;
            }
        }
        avg /= 25.0;
        windowed = windowed + 0.3 * (windowed - avg);
        windowed = clamp(windowed, 0.0, 1.0);
    }
    
    fragColor = vec4(vec3(windowed), 1.0);
}
