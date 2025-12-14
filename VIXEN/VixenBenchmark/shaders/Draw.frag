#version 450

layout(binding = 1) uniform sampler2D tex;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout (push_constant) uniform colorBlock {
    int constColorIndex;
    float mixerValue;
} pushConstantsColorBlock;

vec4 colors[4] = vec4[](
    vec4(1.0, 0.0, 0.0, 1.0), // Red
    vec4(0.0, 1.0, 0.0, 1.0), // Green
    vec4(0.0, 0.0, 1.0, 1.0), // Blue
    vec4(1.0, 1.0, 0.0, 1.0)  // Yellow
);

void main() {
    outColor = texture(tex, uv);
}
