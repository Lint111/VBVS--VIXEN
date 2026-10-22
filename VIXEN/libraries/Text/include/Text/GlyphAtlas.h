#pragma once

#include "Text/GlyphCellStore.h"
#include "Text/GlyphOutline.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/RenderManager.h>

#include <cstdint>
#include <memory>
#include <span>

namespace Vixen::Text {

struct AtlasGlyph {
    std::uint32_t glyphIndex = 0;
    // The atlas is owned by one pixel-size font handle; geometry and metrics
    // are therefore in hinted screen pixels rather than normalized em units.
    float advance = 0.0f;
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    float padding = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

// A lazily populated, single-page RGB multi-channel signed-distance atlas.
// Each font-size handle owns an atlas populated from FreeType's hinted,
// pixel-sized outlines. Bearings, padding, and cell dimensions are in pixels.
class GlyphAtlas final {
public:
    explicit GlyphAtlas(std::uint32_t pageDimension = 1024, float distanceRange = 4.0f);
    ~GlyphAtlas();

    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    // Seed this atlas from, and collect new cells for, the persistent glyph store. Call once,
    // before the first glyph. The key's page dimension and range are taken from the atlas.
    void AttachPersistentStore(const GlyphCacheKey& key);
    // Publish cells generated since the last flush (atomic rename). Runs at destruction; call it
    // earlier only from a shutdown path, never per frame.
    bool Flush();
    [[nodiscard]] std::size_t GeneratedGlyphCount() const noexcept;

    bool AddGlyph(FT_Face face, std::uint32_t glyphIndex, int fontSize);
    // Rasterize the same bounded contour instance used by world-space recipes.
    // pointsToPixels converts FreeType's 26.6 outline units to atlas pixels.
    bool AddGlyphFromOutline(std::uint32_t glyphIndex, const GlyphOutlineInstance& outline,
                             float pointsToPixels = 1.0f / 64.0f);
    [[nodiscard]] std::shared_ptr<const GlyphOutlineInstance> GetOrAddOutline(
        FT_Face face, std::uint32_t glyphIndex,
        GlyphOutlineBakeDiagnostic* diagnostic = nullptr);
    [[nodiscard]] const AtlasGlyph* FindGlyph(std::uint32_t glyphIndex) const;
    // Preserve RmlUi's small outlined replacement glyph when no bundled face
    // provides a requested codepoint. The adapter owns one atlas per font size.
    bool AddReplacementGlyph(FT_Face face, int fontSize);
    [[nodiscard]] Rml::Texture GetTexture(Rml::RenderManager& renderManager) const;
    [[nodiscard]] std::uint32_t Version() const;
    [[nodiscard]] std::span<const std::uint8_t> Pixels() const noexcept;
    [[nodiscard]] std::uint32_t PageDimension() const noexcept;
    [[nodiscard]] float DistanceRange() const noexcept;

private:
    bool GenerateGlyph(FT_Face face, std::uint32_t glyphIndex, int fontSize);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Vixen::Text
