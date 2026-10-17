#version 450

layout(set = 0, binding = 0) uniform sampler2D uAtlas;
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColour;
layout(location = 2) in float vMsdfRange;
layout(location = 3) in float vWeight;
layout(location = 4) in float vAnimationAlpha;
layout(location = 0) out vec4 outColour;

float Median3(vec3 value) {
    return max(min(value.r, value.g), min(max(value.r, value.g), value.b));
}

void main() {
    vec3 sampleValue = texture(uAtlas, vUV).rgb;
    vec2 unitRange = vec2(vMsdfRange) / vec2(textureSize(uAtlas, 0));
    vec2 screenTexSize = 1.0 / max(fwidth(vUV), vec2(0.0001));
    float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    float screenDistance = (Median3(sampleValue) - 0.5) * screenPxRange + vWeight;
    float coverage = clamp(screenDistance + 0.5, 0.0, 1.0) * clamp(vAnimationAlpha, 0.0, 1.0);
    outColour = vec4(vColour.rgb * coverage, vColour.a * coverage);
}
