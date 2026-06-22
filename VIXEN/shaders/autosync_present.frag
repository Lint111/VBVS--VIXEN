// autosync_present.frag — auto-sync FrameGraph P4 demo (pass C, fragment stage).
// Reads the SAME SSBO the compute passes produced, indexed by gl_FragCoord, and
// emits it as the framebuffer colour. This fragment-read-after-compute-write is
// the second hazard in the chain; PassGroupNode bakes a buffer barrier
// (COMPUTE_SHADER storage write -> FRAGMENT_SHADER storage read) before the
// render pass to resolve it. Seeing the gradient on screen proves the chain is
// synchronized.
#version 460

// Binding 0: the shared SSBO. READ-ONLY here (consumer). std430 to match layout.
layout(std430, binding = 0) readonly buffer PixelBuffer {
    vec4 data[];
};

layout(push_constant) uniform PushConstants {
    uint width;
    uint height;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);

    // Clamp to the valid range (fragments are at pixel centres, but guard anyway).
    if (x >= pc.width)  x = pc.width  - 1u;
    if (y >= pc.height) y = pc.height - 1u;

    uint idx = y * pc.width + x;
    outColor = data[idx];
}
