#include "Text/RmlFontEngine.h"

#include <gtest/gtest.h>
#include <RmlUi/Core/StringUtilities.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <array>
#include <cmath>
#include <cstdint>

namespace {

constexpr int kFontSize = 16;
constexpr FT_Int32 kLoadFlags = FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;

int MeasureThroughFreeType(FT_Face face, const char* text) {
    float width = 0.0f;
    FT_UInt previousGlyph = 0;
    for (const unsigned char* byte = reinterpret_cast<const unsigned char*>(text); *byte != 0; ++byte) {
        const FT_UInt glyph = FT_Get_Char_Index(face, *byte);
        if (previousGlyph != 0 && glyph != 0 && FT_HAS_KERNING(face)) {
            FT_Vector kerning{};
            if (FT_Get_Kerning(face, previousGlyph, glyph, FT_KERNING_UNSCALED, &kerning) == 0)
                width += static_cast<float>(kerning.x);
        }
        if (FT_Load_Glyph(face, glyph, kLoadFlags) == 0)
            width += static_cast<float>(face->glyph->advance.x);
        previousGlyph = glyph;
    }
    width *= static_cast<float>(kFontSize) / static_cast<float>(face->units_per_EM);
    return static_cast<int>(std::lround(width));
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
