#pragma once

#include "Text/Bidi.h"
#include "Text/LineBreaker.h"
#include "Text/Shaper.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Vixen::Text {

enum class GlyphOutlineFillRule : std::uint8_t { NonZeroWinding, EvenOdd };
enum class GlyphOutlineSegmentKind : std::uint8_t { Line, Quadratic, Cubic };

struct GlyphOutlinePoint {
    float x = 0.0f;
    float y = 0.0f;
};

// Segment starts are implicit: the first segment begins at its contour's start
// point, and every later segment begins at the previous segment's end point.
// A segment kind consumes one, two, or three points respectively (line end;
// quadratic control + end; cubic controls + end).
struct GlyphOutlineContour {
    GlyphOutlinePoint start;
    std::uint32_t firstSegment = 0;
    std::uint32_t segmentCount = 0;
};

struct GlyphOutline {
    GlyphOutlineFillRule fillRule = GlyphOutlineFillRule::NonZeroWinding;
    std::vector<GlyphOutlineContour> contours;
    std::vector<GlyphOutlineSegmentKind> segmentKinds;
    std::vector<GlyphOutlinePoint> controlAndEndPoints;
};

// Position values use the same 26.6 pixel units as FreeType and HarfBuzz.
// outlineIndex refers to the request-local deduplicated outlines array.
struct PositionedGlyphOutline {
    std::uint32_t glyphId = 0;
    std::uint32_t cluster = 0;
    std::uint32_t outlineIndex = 0;
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
    std::vector<GlyphOutline> outlines;
    std::vector<PositionedGlyphOutline> glyphs;
};

// Extract one already-shaped FreeType glyph. Empty outlines are valid (for
// example, the space glyph); malformed glyphs and FreeType errors return false.
bool LoadGlyphOutline(FT_Face fontFace, std::uint32_t glyphId, GlyphOutline& outline);

// Shape a literal with the selected preset, preserve its visual bidi run order,
// and attach one outline reference to every positioned glyph. The caller
// applies worldUnitsPerPixel / 64 to both the contour coordinates and the
// instance positions. RTL layouts carry mirrorX so the scene transform can
// mirror the sign as a whole.
WorldTextLayout ShapeWorldText(const PresetDesc& preset,
                               std::span<const std::uint32_t> requiredScriptTags,
                               FT_Face fontFace,
                               std::string_view utf8Text,
                               std::string_view language = {});

} // namespace Vixen::Text
