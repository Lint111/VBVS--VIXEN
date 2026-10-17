#include "Text/Abi.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string ReadGeneratedGlsl() {
    std::ifstream input(TEXT_SHADER_GLSL_PATH, std::ios::binary);
    EXPECT_TRUE(input.good()) << TEXT_SHADER_GLSL_PATH;
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void ExpectConstant(const std::string& glsl, const char* name, std::uint32_t value) {
    const std::string declaration = "const uint " + std::string(name) + " = " + std::to_string(value) + "u;";
    EXPECT_NE(glsl.find(declaration), std::string::npos) << declaration;
}

} // namespace

TEST(TextShaderLayout, GeneratedStd430FieldsMatchCppOffsets) {
    using namespace Vixen::Text;
    static_assert(sizeof(GlyphInstance) == 48);
    static_assert(offsetof(GlyphInstance, x) == 0);
    static_assert(offsetof(GlyphInstance, y) == 4);
    static_assert(offsetof(GlyphInstance, w) == 8);
    static_assert(offsetof(GlyphInstance, h) == 12);
    static_assert(offsetof(GlyphInstance, u0) == 16);
    static_assert(offsetof(GlyphInstance, v0) == 20);
    static_assert(offsetof(GlyphInstance, u1) == 24);
    static_assert(offsetof(GlyphInstance, v1) == 28);
    static_assert(offsetof(GlyphInstance, span) == 32);
    static_assert(offsetof(GlyphInstance, cluster) == 36);
    static_assert(offsetof(GlyphInstance, msdfRange) == 40);
    static_assert(offsetof(GlyphInstance, flags) == 44);
    static_assert(sizeof(SpanStyle) == 16);
    static_assert(offsetof(SpanStyle, rgba) == 0);
    static_assert(offsetof(SpanStyle, weight) == 4);
    static_assert(offsetof(SpanStyle, italic) == 8);
    static_assert(offsetof(SpanStyle, effect) == 12);
    static_assert(sizeof(SpanAnim) == 24);
    static_assert(offsetof(SpanAnim, kind) == 0);
    static_assert(offsetof(SpanAnim, t0) == 4);
    static_assert(offsetof(SpanAnim, duration) == 8);
    static_assert(offsetof(SpanAnim, amplitude) == 12);
    static_assert(offsetof(SpanAnim, frequency) == 16);
    static_assert(offsetof(SpanAnim, phase) == 20);

    const std::string glsl = ReadGeneratedGlsl();
    for (const char* type : {"GlyphInstance", "SpanStyle", "SpanAnim"})
        EXPECT_NE(glsl.find(std::string("struct ") + type + " {"), std::string::npos);
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_SIZE", sizeof(GlyphInstance));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_X_OFFSET", offsetof(GlyphInstance, x));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_Y_OFFSET", offsetof(GlyphInstance, y));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_W_OFFSET", offsetof(GlyphInstance, w));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_H_OFFSET", offsetof(GlyphInstance, h));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_U0_OFFSET", offsetof(GlyphInstance, u0));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_V0_OFFSET", offsetof(GlyphInstance, v0));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_U1_OFFSET", offsetof(GlyphInstance, u1));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_V1_OFFSET", offsetof(GlyphInstance, v1));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_SPAN_OFFSET", offsetof(GlyphInstance, span));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_CLUSTER_OFFSET", offsetof(GlyphInstance, cluster));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_MSDFRANGE_OFFSET", offsetof(GlyphInstance, msdfRange));
    ExpectConstant(glsl, "TEXT_LAYOUT_GLYPHINSTANCE_FLAGS_OFFSET", offsetof(GlyphInstance, flags));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANSTYLE_SIZE", sizeof(SpanStyle));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANSTYLE_RGBA_OFFSET", offsetof(SpanStyle, rgba));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANSTYLE_WEIGHT_OFFSET", offsetof(SpanStyle, weight));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANSTYLE_ITALIC_OFFSET", offsetof(SpanStyle, italic));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANSTYLE_EFFECT_OFFSET", offsetof(SpanStyle, effect));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANANIM_SIZE", sizeof(SpanAnim));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANANIM_KIND_OFFSET", offsetof(SpanAnim, kind));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANANIM_T0_OFFSET", offsetof(SpanAnim, t0));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANANIM_DURATION_OFFSET", offsetof(SpanAnim, duration));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANANIM_AMPLITUDE_OFFSET", offsetof(SpanAnim, amplitude));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANANIM_FREQUENCY_OFFSET", offsetof(SpanAnim, frequency));
    ExpectConstant(glsl, "TEXT_LAYOUT_SPANANIM_PHASE_OFFSET", offsetof(SpanAnim, phase));
}

TEST(TextShaderLayout, GeneratedContentEnumsMatchCppValues) {
    using namespace Vixen::Text;
    const std::string glsl = ReadGeneratedGlsl();
    ExpectConstant(glsl, "TEXT_ANIMATION_FADE", static_cast<std::uint32_t>(TextAnimationKind::Fade));
    ExpectConstant(glsl, "TEXT_ANIMATION_PULSE", static_cast<std::uint32_t>(TextAnimationKind::Pulse));
    ExpectConstant(glsl, "TEXT_ANIMATION_SHAKE", static_cast<std::uint32_t>(TextAnimationKind::Shake));
    ExpectConstant(glsl, "TEXT_ANIMATION_TYPEWRITER", static_cast<std::uint32_t>(TextAnimationKind::Typewriter));
    ExpectConstant(glsl, "TEXT_ANIMATION_WAVE", static_cast<std::uint32_t>(TextAnimationKind::Wave));
    ExpectConstant(glsl, "TEXT_TAG_EFFECT_CORE_COLOR", static_cast<std::uint32_t>(TextTagEffect::CoreColor));
    ExpectConstant(glsl, "TEXT_TAG_EFFECT_CORE_EMPHASIS",
                   static_cast<std::uint32_t>(TextTagEffect::CoreEmphasis));
    ExpectConstant(glsl, "TEXT_TAG_EFFECT_CORE_LANG", static_cast<std::uint32_t>(TextTagEffect::CoreLang));
    ExpectConstant(glsl, "TEXT_TAG_EFFECT_CORE_PAUSE", static_cast<std::uint32_t>(TextTagEffect::CorePause));
    ExpectConstant(glsl, "TEXT_TAG_EFFECT_CORE_SIZE", static_cast<std::uint32_t>(TextTagEffect::CoreSize));
}
