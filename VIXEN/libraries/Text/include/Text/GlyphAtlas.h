#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/RenderManager.h>

#include <cstdint>
#include <memory>

namespace Vixen::Text {

struct AtlasGlyph {
    std::uint32_t glyphIndex = 0;
    float advance = 0.0f;
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

// A lazily populated, single-page RGB multi-channel signed-distance atlas.
// Glyph quads include the distance-field border; their advances remain the
// FreeType 26.6 metrics used by the RmlFontEngine.
class GlyphAtlas final {
public:
    explicit GlyphAtlas(std::uint32_t pageDimension = 1024, float distanceRange = 4.0f);
    ~GlyphAtlas();

    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    bool AddGlyph(FT_Face face, std::uint32_t glyphIndex);
    [[nodiscard]] const AtlasGlyph* FindGlyph(std::uint32_t glyphIndex) const;
    [[nodiscard]] Rml::Texture GetTexture(Rml::RenderManager& renderManager) const;
    [[nodiscard]] std::uint32_t Version() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Vixen::Text
