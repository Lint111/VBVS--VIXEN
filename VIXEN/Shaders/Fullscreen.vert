#version 460

// Fullscreen quad vertex shader
// Used with VoxelRayMarch.frag for fragment shader ray marching pipeline

// Output to fragment shader
layout(location = 0) out vec2 fragUV;

// Fullscreen triangle vertices (covers entire screen with single triangle)
// Advantage: No diagonal seam, single primitive, GPU-friendly
const vec2 POSITIONS[3] = vec2[](
    vec2(-1.0, -1.0),  // Bottom-left
    vec2( 3.0, -1.0),  // Bottom-right (off-screen)
    vec2(-1.0,  3.0)   // Top-left (off-screen)
);

const vec2 UVS[3] = vec2[](
    vec2(0.0, 0.0),
    vec2(2.0, 0.0),
    vec2(0.0, 2.0)
);

void main() {
    // Use vertex index to select position (no vertex buffer needed)
    gl_Position = vec4(POSITIONS[gl_VertexIndex], 0.0, 1.0);
    fragUV = UVS[gl_VertexIndex];
}
