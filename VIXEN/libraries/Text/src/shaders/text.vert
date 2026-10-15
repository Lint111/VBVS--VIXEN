#version 450
#extension GL_GOOGLE_include_directive : require

#include "text.g.glsl"

layout(std430, set = 0, binding = 1) readonly buffer GlyphBuffer { GlyphInstance glyphs[]; };
layout(std430, set = 0, binding = 2) readonly buffer StyleBuffer { SpanStyle styles[]; };
layout(std430, set = 0, binding = 3) readonly buffer AnimationBuffer { SpanAnim animations[]; };

layout(push_constant) uniform TextPushConstants {
    vec2 viewport;
    float time;
    float reserved;
} pc;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColour;
layout(location = 2) out float vMsdfRange;
layout(location = 3) out float vWeight;
layout(location = 4) out float vAnimationAlpha;

const vec2 kQuadVertices[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main() {
    GlyphInstance glyph = glyphs[gl_InstanceIndex];
    SpanStyle style = styles[glyph.span];
    SpanAnim animation = animations[glyph.span];
    vec2 corner = kQuadVertices[gl_VertexIndex];
    vec2 animatedOrigin = vec2(glyph.x, glyph.y) + TextAnimationOffset(animation, glyph.cluster, pc.time);
    float italicShear = style.italic * (1.0 - corner.y) * glyph.h * 0.2;
    vec2 pixel = animatedOrigin + vec2(corner.x * glyph.w + italicShear, corner.y * glyph.h);
    vec2 ndc = vec2(2.0 * pixel.x / pc.viewport.x - 1.0,
                    2.0 * pixel.y / pc.viewport.y - 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = mix(vec2(glyph.u0, glyph.v0), vec2(glyph.u1, glyph.v1), corner);
    vColour = unpackUnorm4x8(style.rgba);
    vMsdfRange = glyph.msdfRange;
    vWeight = style.weight + ((style.effect & TEXT_TAG_EFFECT_CORE_EMPHASIS) != 0u ? 0.08 : 0.0);
    vAnimationAlpha = TextAnimationAlpha(animation, glyph.cluster, pc.time);
}
