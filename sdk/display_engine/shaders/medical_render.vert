#version 450

// ============================================================================
// Medical Display Vertex Shader
// Full-screen quad rendering
// ============================================================================

// Full-screen triangle via triangle strip
void main() {
    // Generate full-screen quad vertices
    // Vertex 0: (1, 1) - top right
    // Vertex 1: (-1, 1) - top left
    // Vertex 2: (1, -1) - bottom right
    // Vertex 3: (-1, -1) - bottom left
    
    float x = float((gl_VertexID & 1) * 2) - 1.0;
    float y = float((gl_VertexID >> 1) * 2) - 1.0;
    
    gl_Position = vec4(x, y, 0.0, 1.0);
}
