#version 450

// AR#31 instancing demo fragment shader.
//
// No descriptors here on purpose: keeping the fragment stage descriptor-free means
// the whole program reflects exactly ONE binding (the vertex-stage instance SSBO at
// binding 0), so there is no cross-stage binding collision and no texture asset
// dependency. Colour is derived from UV and the per-instance id purely so the
// instanced cubes are visibly distinct.
layout(location = 0) in vec2 uv;
layout(location = 1) in float instanceId;

layout(location = 0) out vec4 outColor;

void main() {
    // Cheap per-instance hue so neighbouring cubes are distinguishable.
    float h = fract(instanceId * 0.1373);
    vec3 tint = vec3(h, 1.0 - h, 0.5 + 0.5 * sin(instanceId));
    // Mix in UV so each cube face has visible shading.
    vec3 col = mix(tint, vec3(uv, 1.0), 0.35);
    outColor = vec4(col, 1.0);
}
