#include "Text/RmlFontEngine.h"
#include "Text/AtlasFormat.h"
#include "Text/GlyphAtlas.h"

#include <gtest/gtest.h>
#include <RmlUi/Core/StringUtilities.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace {

constexpr int kFontSize = 16;
constexpr FT_Int32 kLoadFlags = FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;

int MeasureThroughFreeType(FT_Face face, const char* text) {
    if (FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(kFontSize) * 64, 0, 0) != 0)
        return -1;
    int width = 0;
    FT_UInt previousGlyph = 0;
    for (const unsigned char* byte = reinterpret_cast<const unsigned char*>(text); *byte != 0; ++byte) {
        const FT_UInt glyph = FT_Get_Char_Index(face, *byte);
        if (previousGlyph != 0 && glyph != 0 && FT_HAS_KERNING(face)) {
            FT_Vector kerning{};
            if (FT_Get_Kerning(face, previousGlyph, glyph, FT_KERNING_DEFAULT, &kerning) == 0)
                width += static_cast<int>(kerning.x >> 6);
        }
        if (FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) == 0)
            width += static_cast<int>(face->glyph->metrics.horiAdvance >> 6);
        previousGlyph = glyph;
    }
    return std::max(width, 0);
}

} // namespace

TEST(TextGlyphMetrics, LatoAdvancesMatchFreeTypeForTenStrings) {
    Vixen::Text::RmlFontEngine engine;
    engine.Initialize();
    ASSERT_TRUE(engine.LoadFontFace(TEXT_LATO_FONT_PATH, 0, "LatoLatin", Rml::Style::FontStyle::Normal,
                                    Rml::Style::FontWeight::Normal, true));

    const Rml::FontFaceHandle font = engine.GetFontFaceHandle(
        "LatoLatin", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, kFontSize);
    ASSERT_NE(font, Rml::FontFaceHandle{});

    const Rml::String language = "en";
    const Rml::TextShapingContext shapingContext{
        language, Rml::Style::Direction::Auto, Rml::Style::FontKerning::Normal, 0.0f};
    const std::array<const char*, 10> strings{
        "A", "To", "VIXEN", "text module", "glyph metrics", "Lato Latin", "0123456789", "AVATAR", "ffi", "Hello, world!"};

    FT_Library library = nullptr;
    ASSERT_EQ(FT_Init_FreeType(&library), 0);
    FT_Face referenceFace = nullptr;
    ASSERT_EQ(FT_New_Face(library, TEXT_LATO_FONT_PATH, 0, &referenceFace), 0);

    for (const char* text : strings) {
        const int referenceWidth = MeasureThroughFreeType(referenceFace, text);
        EXPECT_EQ(engine.GetStringWidth(font, Rml::StringView(text), shapingContext), referenceWidth) << text;
    }

    FT_Done_Face(referenceFace);
    FT_Done_FreeType(library);
    engine.Shutdown();
}

TEST(TextGlyphMetrics, DescenderAtlasCellsCoverFreeTypeBounds) {
    FT_Library library = nullptr;
    ASSERT_EQ(FT_Init_FreeType(&library), 0);
    FT_Face face = nullptr;
    ASSERT_EQ(FT_New_Face(library, TEXT_LATO_FONT_PATH, 0, &face), 0);

    constexpr double distanceRange = 4.0;
    constexpr double designPixelsPerEm = Vixen::Text::kAtlasDesignPixelsPerEm;
    const double fontUnitsPerPixel = static_cast<double>(face->units_per_EM) / designPixelsPerEm;
    const double padding = distanceRange * fontUnitsPerPixel;
    Vixen::Text::GlyphAtlas atlas(1024, static_cast<float>(distanceRange));

    constexpr std::array<char32_t, 4> descenders{U'g', U'y', U'p', U'j'};
    for (const char32_t character : descenders) {
        const FT_UInt glyphIndex = FT_Get_Char_Index(face, character);
        ASSERT_NE(glyphIndex, 0U) << static_cast<char>(character);
        ASSERT_EQ(FT_Load_Glyph(face, glyphIndex, kLoadFlags), 0);
        const FT_Glyph_Metrics metrics = face->glyph->metrics;
        const FT_Pos advance = face->glyph->advance.x;

        ASSERT_TRUE(atlas.AddGlyph(face, glyphIndex)) << static_cast<char>(character);
        const Vixen::Text::AtlasGlyph* glyph = atlas.FindGlyph(glyphIndex);
        ASSERT_NE(glyph, nullptr) << static_cast<char>(character);

        const double left = static_cast<double>(metrics.horiBearingX);
        const double top = static_cast<double>(metrics.horiBearingY);
        const double right = left + static_cast<double>(metrics.width);
        const double bottom = top - static_cast<double>(metrics.height);
        ASSERT_LT(bottom, 0.0) << "fixture must exercise a real descender: " << static_cast<char>(character);

        const double expectedCellWidth = std::ceil(static_cast<double>(metrics.width) / fontUnitsPerPixel +
                                                   2.0 * distanceRange) / designPixelsPerEm;
        const double expectedCellHeight = std::ceil(static_cast<double>(metrics.height) / fontUnitsPerPixel +
                                                    2.0 * distanceRange) / designPixelsPerEm;
        EXPECT_NEAR(glyph->bearingX, (left - padding) / face->units_per_EM, 1e-6) << static_cast<char>(character);
        EXPECT_NEAR(glyph->bearingY, (top + padding) / face->units_per_EM, 1e-6) << static_cast<char>(character);
        EXPECT_NEAR(glyph->padding, distanceRange / designPixelsPerEm, 1e-6) << static_cast<char>(character);
        EXPECT_NEAR(glyph->width, expectedCellWidth, 1e-6) << static_cast<char>(character);
        EXPECT_NEAR(glyph->height, expectedCellHeight, 1e-6) << static_cast<char>(character);
        EXPECT_NEAR(glyph->advance, static_cast<float>(advance) / face->units_per_EM, 1e-6f)
            << static_cast<char>(character);

        // Removing the four-pixel SDF border from the cell must recover the complete FreeType
        // bounds, with at most one atlas texel of harmless ceil padding on the right/bottom.
        const double unpaddedLeft = glyph->bearingX + distanceRange / designPixelsPerEm;
        const double unpaddedTop = glyph->bearingY - distanceRange / designPixelsPerEm;
        const double unpaddedRight = glyph->bearingX + glyph->width - distanceRange / designPixelsPerEm;
        const double unpaddedBottom = glyph->bearingY - glyph->height + distanceRange / designPixelsPerEm;
        const double texel = 1.0 / designPixelsPerEm;
        EXPECT_NEAR(unpaddedLeft, left / face->units_per_EM, 1e-6) << static_cast<char>(character);
        EXPECT_NEAR(unpaddedTop, top / face->units_per_EM, 1e-6) << static_cast<char>(character);
        EXPECT_GE(unpaddedRight, right / face->units_per_EM - 1e-6) << static_cast<char>(character);
        EXPECT_LE(unpaddedRight, right / face->units_per_EM + texel + 1e-6) << static_cast<char>(character);
        EXPECT_LE(unpaddedBottom, bottom / face->units_per_EM + 1e-6) << static_cast<char>(character);
        EXPECT_GE(unpaddedBottom, bottom / face->units_per_EM - texel - 1e-6) << static_cast<char>(character);
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);
}

TEST(TextGlyphMetrics, MissingCharacterUsesRmlUiOutlinedReplacementBox) {
    FT_Library library = nullptr;
    ASSERT_EQ(FT_Init_FreeType(&library), 0);
    FT_Face face = nullptr;
    ASSERT_EQ(FT_New_Face(library, TEXT_LATO_FONT_PATH, 0, &face), 0);

    constexpr int fontSize = 28;
    constexpr double distanceRange = 4.0;
    constexpr double designPixelsPerEm = Vixen::Text::kAtlasDesignPixelsPerEm;
    Vixen::Text::GlyphAtlas atlas(1024, static_cast<float>(distanceRange));
    ASSERT_TRUE(atlas.AddReplacementGlyph(face, fontSize));
    const Vixen::Text::AtlasGlyph* glyph = atlas.FindGlyph(0);
    ASSERT_NE(glyph, nullptr);

    const double padding = distanceRange / designPixelsPerEm;
    const double left = glyph->bearingX + padding;
    const double top = glyph->bearingY - padding;
    const double right = glyph->bearingX + glyph->width - padding;
    const double bottom = glyph->bearingY - glyph->height + padding;
    const double outputTexel = fontSize / designPixelsPerEm;
    EXPECT_NEAR(left * fontSize, 1.0, 1e-4);
    EXPECT_NEAR(top * fontSize, 18.0, 1e-4);
    EXPECT_NEAR((right - left) * fontSize, 9.0, outputTexel);
    EXPECT_NEAR(bottom * fontSize, 0.0, outputTexel);

    FT_Done_Face(face);
    FT_Done_FreeType(library);
}
