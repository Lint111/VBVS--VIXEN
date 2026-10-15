#include "Text/Bidi.h"
#include "Text/GlyphOutline.h"
#include "Text/LineBreaker.h"
#include "Text/RmlFontEngine.h"
#include "Text/Shaper.h"

#include <gtest/gtest.h>

#include <RmlUi/Core/StringUtilities.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t ScriptTag(char a, char b, char c, char d) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8U) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

Vixen::Text::PresetDesc MakePreset(Vixen::Text::TextDirection direction,
                                   Vixen::Text::ShapingClass shapingClass,
                                   Vixen::Text::LineBreakingClass lineBreakingClass,
                                   std::uint32_t requiredScriptCount) {
    Vixen::Text::PresetDesc preset{};
    preset.direction = direction;
    preset.shapingClass = shapingClass;
    preset.lineBreakingClass = lineBreakingClass;
    preset.requiredScriptOrdinalCount = requiredScriptCount;
    return preset;
}

std::string FormatGlyphs(const std::vector<Vixen::Text::ShapedGlyph>& glyphs) {
    std::ostringstream output;
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        if (index != 0)
            output << ',';
        output << glyphs[index].glyphId << ':' << glyphs[index].cluster;
    }
    return output.str();
}

class TextShapingFixture : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(FT_Init_FreeType(&library), 0);
    }

    void TearDown() override {
        if (arabicFace)
            FT_Done_Face(arabicFace);
        if (latinFace)
            FT_Done_Face(latinFace);
        if (library)
            FT_Done_FreeType(library);
    }

    FT_Face LoadFace(const char* path) {
        FT_Face face = nullptr;
        if (FT_New_Face(library, path, 0, &face) != 0)
            return nullptr;
        if (FT_Set_Char_Size(face, 0, 24 * 64, 0, 0) != 0) {
            FT_Done_Face(face);
            return nullptr;
        }
        return face;
    }

    FT_Library library = nullptr;
    FT_Face latinFace = nullptr;
    FT_Face arabicFace = nullptr;
};

TEST_F(TextShapingFixture, LatinGlyphIdsAndByteClustersAreStable) {
    latinFace = LoadFace(TEXT_LATO_FONT_PATH);
    ASSERT_NE(latinFace, nullptr);

    const auto preset = MakePreset(Vixen::Text::TextDirection::LeftToRight,
                                   Vixen::Text::ShapingClass::Default,
                                   Vixen::Text::LineBreakingClass::Word, 1);
    constexpr std::array<std::uint32_t, 1> requiredScripts{ScriptTag('L', 'a', 't', 'n')};
    const Vixen::Text::ShapedText shaped = Vixen::Text::ShapeUtf8(
        preset, requiredScripts, latinFace, "office", "en");

    ASSERT_TRUE(shaped.requiredScriptTableMatchesPreset);
    ASSERT_EQ(shaped.visualRuns.size(), 1U);
    EXPECT_EQ(shaped.visualRuns[0].scriptTag, ScriptTag('L', 'a', 't', 'n'));
    EXPECT_TRUE(shaped.visualRuns[0].scriptRequired);
    EXPECT_EQ(FormatGlyphs(shaped.visualRuns[0].glyphs), "105:0,72:1,49:4,55:5");
}

TEST_F(TextShapingFixture, GlyphOutlinePayloadPreservesCurveTypesAndHoles) {
    latinFace = LoadFace(TEXT_LATO_FONT_PATH);
    ASSERT_NE(latinFace, nullptr);

    Vixen::Text::GlyphOutline round;
    ASSERT_TRUE(Vixen::Text::LoadGlyphOutline(
        latinFace, FT_Get_Char_Index(latinFace, U'o'), round));
    EXPECT_EQ(round.fillRule, Vixen::Text::GlyphOutlineFillRule::NonZeroWinding);
    ASSERT_GE(round.contours.size(), 2U);
    EXPECT_FALSE(round.segmentKinds.empty());
    std::size_t expectedPointCount = 0;
    for (const Vixen::Text::GlyphOutlineSegmentKind kind : round.segmentKinds) {
        expectedPointCount += kind == Vixen::Text::GlyphOutlineSegmentKind::Line
                                  ? 1U
                                  : kind == Vixen::Text::GlyphOutlineSegmentKind::Quadratic
                                        ? 2U
                                        : 3U;
    }
    EXPECT_EQ(round.controlAndEndPoints.size(), expectedPointCount);
    EXPECT_GT(std::count(round.segmentKinds.begin(), round.segmentKinds.end(),
                         Vixen::Text::GlyphOutlineSegmentKind::Quadratic), 0);
    for (const Vixen::Text::GlyphOutlineContour& contour : round.contours) {
        EXPECT_GT(contour.segmentCount, 0U);
        EXPECT_LE(contour.firstSegment + contour.segmentCount, round.segmentKinds.size());
    }

    Vixen::Text::GlyphOutline eight;
    ASSERT_TRUE(Vixen::Text::LoadGlyphOutline(
        latinFace, FT_Get_Char_Index(latinFace, U'8'), eight));
    EXPECT_GE(eight.contours.size(), 3U);
}

TEST_F(TextShapingFixture, WorldTextPositionsShapedGlyphsAndCarriesRtlMirror) {
    latinFace = LoadFace(TEXT_LATO_FONT_PATH);
    ASSERT_NE(latinFace, nullptr);

    constexpr std::array<std::uint32_t, 1> requiredScripts{ScriptTag('L', 'a', 't', 'n')};
    const auto leftToRight = MakePreset(Vixen::Text::TextDirection::LeftToRight,
                                        Vixen::Text::ShapingClass::Default,
                                        Vixen::Text::LineBreakingClass::Word, 1);
    const Vixen::Text::WorldTextLayout latin = Vixen::Text::ShapeWorldText(
        leftToRight, requiredScripts, latinFace, "OPEN", "en");

    ASSERT_TRUE(latin.valid);
    EXPECT_FALSE(latin.mirrorX);
    ASSERT_EQ(latin.glyphs.size(), 4U);
    EXPECT_EQ(latin.glyphs.front().x, 0);
    for (const Vixen::Text::PositionedGlyphOutline& glyph : latin.glyphs)
        EXPECT_LT(glyph.outlineIndex, latin.outlines.size());

    const auto rightToLeft = MakePreset(Vixen::Text::TextDirection::RightToLeft,
                                        Vixen::Text::ShapingClass::Default,
                                        Vixen::Text::LineBreakingClass::Word, 1);
    const Vixen::Text::WorldTextLayout rtl = Vixen::Text::ShapeWorldText(
        rightToLeft, requiredScripts, latinFace, "NEPO", "qps-rtl");

    ASSERT_TRUE(rtl.valid);
    EXPECT_EQ(rtl.direction, Vixen::Text::TextDirection::RightToLeft);
    EXPECT_TRUE(rtl.mirrorX);
    ASSERT_EQ(rtl.glyphs.size(), 4U);
    EXPECT_TRUE(rtl.requiredScriptTableMatchesPreset);
}

TEST_F(TextShapingFixture, ArabicGlyphIdsAndByteClustersAreStable) {
    arabicFace = LoadFace(TEXT_NOTO_ARABIC_FONT_PATH);
    ASSERT_NE(arabicFace, nullptr);

    // Arabic "salaam" in logical order. UTF-8 source byte offsets are the
    // cluster values expected from the HarfBuzz run.
    constexpr std::string_view salaam = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    const auto preset = MakePreset(Vixen::Text::TextDirection::RightToLeft,
                                   Vixen::Text::ShapingClass::Joining,
                                   Vixen::Text::LineBreakingClass::Word, 1);
    constexpr std::array<std::uint32_t, 1> requiredScripts{ScriptTag('A', 'r', 'a', 'b')};
    const Vixen::Text::ShapedText shaped = Vixen::Text::ShapeUtf8(
        preset, requiredScripts, arabicFace, salaam, "ar");

    ASSERT_TRUE(shaped.requiredScriptTableMatchesPreset);
    ASSERT_EQ(shaped.visualRuns.size(), 1U);
    EXPECT_EQ(shaped.visualRuns[0].scriptTag, ScriptTag('A', 'r', 'a', 'b'));
    EXPECT_TRUE(shaped.visualRuns[0].scriptRequired);
    EXPECT_TRUE(shaped.visualRuns[0].bidi.rightToLeft);
    EXPECT_EQ(FormatGlyphs(shaped.visualRuns[0].glyphs), "74:6,11:4,71:2,37:0");

    constexpr std::string_view bracketed = "(\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85)";
    const Vixen::Text::ShapedText shapedBracketed = Vixen::Text::ShapeUtf8(
        preset, requiredScripts, arabicFace, bracketed, "ar");
    ASSERT_EQ(shapedBracketed.visualRuns.size(), 1U);
    bool foundMirroredLeft = false;
    bool foundMirroredRight = false;
    for (const Vixen::Text::ShapedGlyph& glyph : shapedBracketed.visualRuns[0].glyphs) {
        if (glyph.cluster == 0U) {
            foundMirroredLeft = glyph.glyphId == FT_Get_Char_Index(arabicFace, U')');
        } else if (glyph.cluster == 9U) {
            foundMirroredRight = glyph.glyphId == FT_Get_Char_Index(arabicFace, U'(');
        }
    }
    EXPECT_TRUE(foundMirroredLeft)
        << "glyphs: " << FormatGlyphs(shapedBracketed.visualRuns[0].glyphs)
        << ", mirrored ')' cmap id: " << FT_Get_Char_Index(arabicFace, U')');
    EXPECT_TRUE(foundMirroredRight)
        << "glyphs: " << FormatGlyphs(shapedBracketed.visualRuns[0].glyphs)
        << ", mirrored '(' cmap id: " << FT_Get_Char_Index(arabicFace, U'(');

    const Vixen::Text::ShapedText stalePreset = Vixen::Text::ShapeUtf8(
        MakePreset(Vixen::Text::TextDirection::RightToLeft,
                   Vixen::Text::ShapingClass::Joining,
                   Vixen::Text::LineBreakingClass::Word, 2),
        requiredScripts, arabicFace, salaam, "ar");
    EXPECT_FALSE(stalePreset.requiredScriptTableMatchesPreset);
}

TEST_F(TextShapingFixture, RmlFontEngineUsesAndClearsPresetShaping) {
    latinFace = LoadFace(TEXT_LATO_FONT_PATH);
    ASSERT_NE(latinFace, nullptr);

    Vixen::Text::RmlFontEngine engine;
    engine.Initialize();
    ASSERT_TRUE(engine.LoadFontFace(TEXT_LATO_FONT_PATH, 0, "LatoLatin",
                                    Rml::Style::FontStyle::Normal,
                                    Rml::Style::FontWeight::Normal, true));
    const Rml::FontFaceHandle font = engine.GetFontFaceHandle(
        "LatoLatin", Rml::Style::FontStyle::Normal,
        Rml::Style::FontWeight::Normal, 24);
    ASSERT_NE(font, Rml::FontFaceHandle{});

    const Rml::String language = "en";
    const Rml::TextShapingContext context{
        language, Rml::Style::Direction::Auto,
        Rml::Style::FontKerning::Normal, 0.0f};
    const int legacyWidth = engine.GetStringWidth(font, Rml::StringView("office"), context);

    const auto preset = MakePreset(Vixen::Text::TextDirection::LeftToRight,
                                   Vixen::Text::ShapingClass::Default,
                                   Vixen::Text::LineBreakingClass::Word, 1);
    constexpr std::array<std::uint32_t, 1> requiredScripts{ScriptTag('L', 'a', 't', 'n')};
    const Vixen::Text::ShapedText shaped = Vixen::Text::ShapeUtf8(
        preset, requiredScripts, latinFace, "office", "en");
    ASSERT_EQ(shaped.visualRuns.size(), 1U);
    std::int32_t shapedAdvance = 0;
    for (const Vixen::Text::ShapedGlyph& glyph : shaped.visualRuns[0].glyphs)
        shapedAdvance += glyph.xAdvance;
    const int expectedShapedWidth = static_cast<int>(std::lround(
        static_cast<double>(shapedAdvance) / 64.0));

    engine.SetPresetDesc(preset, requiredScripts, "en");
    EXPECT_EQ(engine.GetStringWidth(font, Rml::StringView("office"), context),
              expectedShapedWidth);
    engine.ClearPresetDesc();
    EXPECT_EQ(engine.GetStringWidth(font, Rml::StringView("office"), context),
              legacyWidth);
    engine.Shutdown();
}

TEST(TextBidi, RtlPresetOrdersRunsVisuallyAndMirrorsPairedBrackets) {
    const auto preset = MakePreset(Vixen::Text::TextDirection::RightToLeft,
                                   Vixen::Text::ShapingClass::Joining,
                                   Vixen::Text::LineBreakingClass::Word, 2);
    constexpr std::string_view mixed = "\xD7\x90\xD7\x91\xD7\x92" "abc";
    const Vixen::Text::BidiResult bidi = Vixen::Text::ResolveBidi(preset, mixed);

    ASSERT_EQ(bidi.visualRuns.size(), 2U);
    EXPECT_EQ(bidi.visualRuns[0].byteOffset, 6U);
    EXPECT_EQ(bidi.visualRuns[0].byteLength, 3U);
    EXPECT_FALSE(bidi.visualRuns[0].rightToLeft);
    EXPECT_EQ(bidi.visualRuns[1].byteOffset, 0U);
    EXPECT_EQ(bidi.visualRuns[1].byteLength, 6U);
    EXPECT_TRUE(bidi.visualRuns[1].rightToLeft);

    constexpr std::string_view bracketed = "(\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85)";
    const Vixen::Text::BidiResult mirrored = Vixen::Text::ResolveBidi(preset, bracketed);
    ASSERT_EQ(mirrored.mirroredCodepoints.size(), 2U);
    EXPECT_EQ(mirrored.mirroredCodepoints[0].byteOffset, 0U);
    EXPECT_EQ(mirrored.mirroredCodepoints[0].value, U')');
    EXPECT_EQ(mirrored.mirroredCodepoints[1].byteOffset, 9U);
    EXPECT_EQ(mirrored.mirroredCodepoints[1].value, U'(');
}

TEST_F(TextShapingFixture, RtlVisualRunOrderIsPreservedBeforeShaping) {
    arabicFace = LoadFace(TEXT_NOTO_ARABIC_FONT_PATH);
    ASSERT_NE(arabicFace, nullptr);

    constexpr std::string_view mixed = "\xD7\x90\xD7\x91\xD7\x92" "abc";
    const auto preset = MakePreset(Vixen::Text::TextDirection::RightToLeft,
                                   Vixen::Text::ShapingClass::Joining,
                                   Vixen::Text::LineBreakingClass::Word, 2);
    constexpr std::array<std::uint32_t, 2> requiredScripts{
        ScriptTag('H', 'e', 'b', 'r'), ScriptTag('L', 'a', 't', 'n')};
    const Vixen::Text::ShapedText shaped = Vixen::Text::ShapeUtf8(
        preset, requiredScripts, arabicFace, mixed, "qps-rtl");

    ASSERT_TRUE(shaped.requiredScriptTableMatchesPreset);
    ASSERT_EQ(shaped.visualRuns.size(), 2U);
    EXPECT_EQ(shaped.visualRuns[0].bidi.byteOffset, 6U);
    EXPECT_FALSE(shaped.visualRuns[0].bidi.rightToLeft);
    EXPECT_EQ(shaped.visualRuns[0].scriptTag, ScriptTag('L', 'a', 't', 'n'));
    EXPECT_EQ(shaped.visualRuns[1].bidi.byteOffset, 0U);
    EXPECT_TRUE(shaped.visualRuns[1].bidi.rightToLeft);
    EXPECT_EQ(shaped.visualRuns[1].scriptTag, ScriptTag('H', 'e', 'b', 'r'));
    EXPECT_TRUE(shaped.visualRuns[0].scriptRequired);
    EXPECT_TRUE(shaped.visualRuns[1].scriptRequired);
}

TEST(TextLineBreaking, PresetClassProducesUtf8ByteOffsets) {
    const auto preset = MakePreset(Vixen::Text::TextDirection::LeftToRight,
                                   Vixen::Text::ShapingClass::Default,
                                   Vixen::Text::LineBreakingClass::Phrase, 0);
    constexpr std::string_view text = "Hi there.";
    const std::vector<Vixen::Text::LineBreakOpportunity> breaks =
        Vixen::Text::FindLineBreaks(preset, text);

    ASSERT_FALSE(breaks.empty());
    EXPECT_EQ(breaks.front().byteOffset, 3U);

    constexpr std::string_view arabicPrefix = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85" " world";
    const std::vector<Vixen::Text::LineBreakOpportunity> utf8Breaks =
        Vixen::Text::FindLineBreaks(preset, arabicPrefix);
    ASSERT_FALSE(utf8Breaks.empty());
    EXPECT_EQ(utf8Breaks.front().byteOffset, 9U);
}

} // namespace
