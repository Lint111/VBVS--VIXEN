#version 450

// AR#31 instancing demo (VIXEN_INSTANCING_DEMO).
//
// Self-contained instanced cube shader: the ONLY descriptor is the per-instance
// model-matrix SSBO at binding 0, satisfied by InstanceBufferNode through the
// DescriptorResourceGatherer (the same data-driven SSBO path the compute pipeline
// uses). A static proj*view is baked in here so the demo needs no MVP uniform
// buffer (the renderer has no UBO-producer node) and no push-constant wiring —
// the cubes only need to be visible for this increment.
layout(std430, binding = 0) readonly buffer Instances {
    mat4 model[];
} instances;

layout(location = 0) in vec4 pos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 outUV;
layout(location = 1) out float outInstance;

// Baked perspective projection (fov ~50deg, aspect 16:9, near 0.1, far 200),
// column-major (GLSL). Mirrors glm::perspective so the grid of cubes fits on screen.
const mat4 kProj = mat4(
    1.2070f, 0.0f,    0.0f,                0.0f,
    0.0f,    2.1445f, 0.0f,                0.0f,
    0.0f,    0.0f,   -1.0010f,            -1.0f,
    0.0f,    0.0f,   -0.2001f,             0.0f
);

// Baked view: camera pulled back along +Z looking toward the origin grid.
const mat4 kView = mat4(
    1.0f, 0.0f, 0.0f,   0.0f,
    0.0f, 1.0f, 0.0f,   0.0f,
    0.0f, 0.0f, 1.0f,   0.0f,
    0.0f, 0.0f, -45.0f, 1.0f
);

void main() {
    outUV = inUV;
    outInstance = float(gl_InstanceIndex);

    gl_Position = kProj * kView * instances.model[gl_InstanceIndex] * pos;

    // Flip Y and remap Z to [0,1] for Vulkan clip space (matches Draw.vert).
    gl_Position.y = -gl_Position.y;
    gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
