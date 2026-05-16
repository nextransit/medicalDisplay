#version 450

// ============================================================================
// HDR10 Passthrough Shader
// For displays that accept HDR10 metadata directly
// ============================================================================

#extension GL_EXT_samplerless_texture_functions : enable

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform texture2D inputTexture;
layout(set = 0, binding = 1) uniform writeonly image2D outputImage;

// HDR10 metadata
layout(push_constant) uniform Hdr10Metadata {
    uint max_luminance;      // Maximum luminance (e.g., 1000 for 1000 nits)
    uint min_luminance;      // Minimum luminance (typically 0)
    uint max_content_light_level;  // MaxCLL
    uint max_picture_average_light_level; // MaxFALL
    vec2 white_point;        // White point chromaticity (x, y)
    vec2 red_primary;        // Red primary chromaticity
    vec2 green_primary;      // Green primary chromaticity
    vec2 blue_primary;       // Blue primary chromaticity
} metadata;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imageSize = textureSize(inputTexture, 0, 0);
    
    if (any(greaterThanEqual(pixel, imageSize))) {
        return;
    }
    
    // Sample the HDR texture
    vec4 hdrColor = texelFetch(inputTexture, pixel, 0);
    
    // HDR10 uses Rec.2020 color space with PQ transfer
    // Passthrough mode - just format for output
    vec3 outputColor = clamp(hdrColor.rgb, 0.0, 1.0);
    
    // Write to output
    imageStore(outputImage, pixel, vec4(outputColor, hdrColor.a));
}
