#version 450

layout (std140, binding = 0) uniform bufferVals {
    mat4 mvp;
} myBufferVals;

// AR#31: per-instance model matrices, indexed by gl_InstanceIndex. At binding 2
// (binding 1 is Draw.frag's sampler2D); 0=MVP UBO, 1=albedo sampler, 2=instance transforms.
layout (std430, binding = 2) readonly buffer Instances {
    mat4 model[];
} instances;

layout(location = 0) in vec4 pos;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec2 outUV;

void main() {
    outUV = inUV;
    gl_Position = myBufferVals.mvp * instances.model[gl_InstanceIndex] * pos;

    // Flip Y and move Z to [0, 1] for Vulkan
    gl_Position.y = -gl_Position.y;
    gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
