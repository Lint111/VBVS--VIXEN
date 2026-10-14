#version 450
// RmlUi 2D UI fragment shader. Untextured geometry binds a 1x1 white texture, so this one
// pipeline handles both textured (font atlas / images) and solid-colour draws.
layout(location = 0) in vec4 vColour;
layout(location = 1) in vec2 vUV;

layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(push_constant) uniform PushConstants {
    vec2 translation;
    vec2 viewport;
    uint isMsdf;
} pc;

layout(location = 0) out vec4 outColour;

void main() {
    vec4 texel = texture(uTex, vUV);
    if (pc.isMsdf != 0u) {
        float medianDistance = max(min(texel.r, texel.g), min(max(texel.r, texel.g), texel.b));
        float atlasPixelsPerScreenPixel = max(fwidth(vUV.x) * float(textureSize(uTex, 0).x),
                                               fwidth(vUV.y) * float(textureSize(uTex, 0).y));
        float screenDistance = (medianDistance - 0.5) * 8.0 / max(atlasPixelsPerScreenPixel, 0.0001);
        float coverage = smoothstep(-0.5, 0.5, screenDistance);
        outColour = vColour * coverage;
    } else {
        outColour = vColour * texel;
    }
}
