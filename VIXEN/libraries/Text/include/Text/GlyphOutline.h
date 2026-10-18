#pragma once

#include "Text/Bidi.h"
#include "Text/LineBreaker.h"
#include "Text/Shaper.h"
#include "Recipe/generated/GlyphOutlineInstance.g.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace Vixen::Text {

using GlyphOutlineInstance = Yeroket::Sdf::Kernels::GlyphOutlineInstance;
using GlyphOutlineSegment = Yeroket::Sdf::Kernels::PathSegment;
using GlyphOutlinePoint = Yeroket::Sdf::Kernels::PathPoint;
class GlyphAtlas;

enum class GlyphOutlineBakeDiagnosticCode : std::uint8_t {
    None,
    InvalidFace,
    GlyphLoadFailed,
    SegmentCapacityExceeded,
};

// World-space contour evaluation keeps its geometric detail while the glyph is
// large enough on screen. At the final small-size band, reuse the atlas MSDF
// generated from this same contour bucket.
enum class WorldTextRenderBand : std::uint8_t { AnalyticSdf, MsdfScreen };
inline constexpr float kMsdfScreenFallbackMaxGlyphHeightPixels = 16.0f;

[[nodiscard]] inline WorldTextRenderBand SelectWorldTextRenderBand(
    float projectedGlyphHeightPixels, bool msdfAvailable) noexcept {
    return msdfAvailable && projectedGlyphHeightPixels <= kMsdfScreenFallbackMaxGlyphHeightPixels
        ? WorldTextRenderBand::MsdfScreen
        : WorldTextRenderBand::AnalyticSdf;
}

struct GlyphOutlineBakeDiagnostic {
    std::uint32_t glyphId = 0;
    GlyphOutlineBakeDiagnosticCode code = GlyphOutlineBakeDiagnosticCode::None;
    std::uint32_t requiredSegments = 0;
    std::uint32_t segmentCapacity = 0;
};

// Position values use the same 26.6 pixel units as FreeType and HarfBuzz.
// Each positioned glyph refers directly to the atlas-owned typed instance row;
// repeated glyphs across strings share that same immutable bucket.
struct PositionedGlyphOutline {
    std::uint32_t glyphId = 0;
    std::uint32_t cluster = 0;
    std::shared_ptr<const GlyphOutlineInstance> instance;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t xAdvance = 0;
    std::int32_t yAdvance = 0;
    bool rightToLeft = false;
};

struct WorldTextLayout {
    bool valid = false;
    bool requiredScriptTableMatchesPreset = false;
    bool mirrorX = false;
    TextDirection direction = TextDirection::LeftToRight;
    std::vector<LineBreakOpportunity> lineBreaks;
    std::vector<GlyphOutlineBakeDiagnostic> diagnostics;
    std::vector<PositionedGlyphOutline> glyphs;
};

// Bake one already-shaped FreeType glyph into the existing fixed-width GPU
// bucket. Capacity overflow is returned as a bake diagnostic; it never grows
// the bucket or throws from runtime evaluation.
bool LoadGlyphOutline(FT_Face fontFace, std::uint32_t glyphId,
                      GlyphOutlineInstance& instance,
                      GlyphOutlineBakeDiagnostic* diagnostic = nullptr);

// Shape a literal with the selected preset, preserve its visual bidi run order,
// and attach one typed glyph-instance reference to every positioned glyph. The
// caller applies worldUnitsPerPixel / 64 to both contour coordinates and
// instance positions. RTL layouts carry mirrorX so the scene transform can
// mirror the sign as a whole.
WorldTextLayout ShapeWorldText(const PresetDesc& preset,
                               std::span<const std::uint32_t> requiredScriptTags,
                               GlyphAtlas& glyphAtlas,
                               FT_Face fontFace,
                               std::string_view utf8Text,
                               std::string_view language);

} // namespace Vixen::Text
