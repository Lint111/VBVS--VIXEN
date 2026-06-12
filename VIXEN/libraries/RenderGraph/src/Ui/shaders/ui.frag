#version 450
// RmlUi 2D UI fragment shader. Untextured geometry binds a 1x1 white texture, so this one
// pipeline handles both textured (font atlas / images) and solid-colour draws.
layout(location = 0) in vec4 vColour;
layout(location = 1) in vec2 vUV;

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(location = 0) out vec4 outColour;

void main() {
    outColour = vColour * texture(uTex, vUV);
}
