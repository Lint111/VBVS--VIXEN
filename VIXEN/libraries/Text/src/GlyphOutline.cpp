#include "Text/GlyphOutline.h"

#include <ft2build.h>
#include FT_OUTLINE_H

#include <array>
#include <limits>
#include <unordered_map>

namespace Vixen::Text {
namespace {

struct OutlineBuilder {
    GlyphOutline* outline = nullptr;
    FT_Vector contourStart{};
    FT_Vector current{};
    bool contourOpen = false;
    bool failed = false;
};

GlyphOutlinePoint Point(const FT_Vector& point) {
    return {static_cast<float>(point.x), static_cast<float>(point.y)};
}

void AppendSegment(OutlineBuilder& builder, GlyphOutlineSegmentKind kind,
                   std::span<const FT_Vector> points) {
    constexpr std::size_t maxIndex = std::numeric_limits<std::uint32_t>::max();
    if (!builder.contourOpen || !builder.outline ||
        builder.outline->segmentKinds.size() >= maxIndex ||
        builder.outline->controlAndEndPoints.size() > maxIndex ||
        points.size() > maxIndex - builder.outline->controlAndEndPoints.size()) {
        builder.failed = true;
        return;
    }

    builder.outline->segmentKinds.push_back(kind);
    for (const FT_Vector& point : points)
        builder.outline->controlAndEndPoints.push_back(Point(point));
    ++builder.outline->contours.back().segmentCount;
}

void FinishContour(OutlineBuilder& builder) {
    if (!builder.contourOpen || builder.failed)
        return;
    if (builder.current.x != builder.contourStart.x ||
        builder.current.y != builder.contourStart.y) {
        const std::array<FT_Vector, 1> close{{builder.contourStart}};
        AppendSegment(builder, GlyphOutlineSegmentKind::Line, close);
    }
    builder.contourOpen = false;
}

int MoveTo(const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    try {
        FinishContour(builder);
        if (builder.failed || builder.outline->contours.size() >=
                                  std::numeric_limits<std::uint32_t>::max() ||
            builder.outline->segmentKinds.size() >
                std::numeric_limits<std::uint32_t>::max()) {
            builder.failed = true;
            return 1;
        }
        builder.contourStart = *to;
        builder.current = *to;
        builder.outline->contours.push_back(
            {Point(*to), static_cast<std::uint32_t>(builder.outline->segmentKinds.size()), 0});
        builder.contourOpen = true;
        return 0;
    } catch (...) {
        builder.failed = true;
        return 1;
    }
}

int LineTo(const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    try {
        const std::array<FT_Vector, 1> points{{*to}};
        AppendSegment(builder, GlyphOutlineSegmentKind::Line, points);
        builder.current = *to;
        return builder.failed ? 1 : 0;
    } catch (...) {
        builder.failed = true;
        return 1;
    }
}

int ConicTo(const FT_Vector* control, const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    try {
        const std::array<FT_Vector, 2> points{{*control, *to}};
        AppendSegment(builder, GlyphOutlineSegmentKind::Quadratic, points);
        builder.current = *to;
        return builder.failed ? 1 : 0;
    } catch (...) {
        builder.failed = true;
        return 1;
    }
}

int CubicTo(const FT_Vector* control1, const FT_Vector* control2,
            const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    try {
        const std::array<FT_Vector, 3> points{{*control1, *control2, *to}};
        AppendSegment(builder, GlyphOutlineSegmentKind::Cubic, points);
        builder.current = *to;
        return builder.failed ? 1 : 0;
    } catch (...) {
        builder.failed = true;
        return 1;
    }
}

constexpr FT_Outline_Funcs kOutlineCallbacks{
    &MoveTo,
    &LineTo,
    &ConicTo,
    &CubicTo,
    0,
    0};

} // namespace

bool LoadGlyphOutline(FT_Face fontFace, std::uint32_t glyphId, GlyphOutline& outline) {
    outline = {};
    if (!fontFace || glyphId >= static_cast<std::uint32_t>(fontFace->num_glyphs))
        return false;
    if (FT_Load_Glyph(fontFace, glyphId, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) != 0)
        return false;

    if (fontFace->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        return true;

    FT_Outline& ftOutline = fontFace->glyph->outline;
    outline.fillRule = (ftOutline.flags & FT_OUTLINE_EVEN_ODD_FILL) != 0
                           ? GlyphOutlineFillRule::EvenOdd
                           : GlyphOutlineFillRule::NonZeroWinding;
    if (ftOutline.n_points == 0 || ftOutline.n_contours == 0)
        return true;

    OutlineBuilder builder{&outline};
    if (FT_Outline_Decompose(&ftOutline, &kOutlineCallbacks, &builder) != 0 || builder.failed) {
        outline = {};
        return false;
    }
    FinishContour(builder);
    if (builder.failed) {
        outline = {};
        return false;
    }
    return true;
}

WorldTextLayout ShapeWorldText(const PresetDesc& preset,
                               std::span<const std::uint32_t> requiredScriptTags,
                               FT_Face fontFace,
                               std::string_view utf8Text,
                               std::string_view language) {
    WorldTextLayout result;
    if (!fontFace || utf8Text.empty())
        return result;

    result.direction = preset.direction;
    result.mirrorX = preset.direction == TextDirection::RightToLeft;
    result.lineBreaks = FindLineBreaks(preset, utf8Text);

    const ShapedText shaped = ShapeUtf8(preset, requiredScriptTags, fontFace,
                                        utf8Text, language);
    result.requiredScriptTableMatchesPreset = shaped.requiredScriptTableMatchesPreset;
    if (!result.requiredScriptTableMatchesPreset || shaped.visualRuns.empty())
        return result;

    std::unordered_map<std::uint32_t, std::uint32_t> outlineByGlyph;
    std::int64_t penX = 0;
    std::int64_t penY = 0;
    for (const ShapedRun& run : shaped.visualRuns) {
        for (const ShapedGlyph& glyph : run.glyphs) {
            auto found = outlineByGlyph.find(glyph.glyphId);
            std::uint32_t outlineIndex = 0;
            if (found == outlineByGlyph.end()) {
                if (result.outlines.size() >= std::numeric_limits<std::uint32_t>::max())
                    return {};
                GlyphOutline outline;
                if (!LoadGlyphOutline(fontFace, glyph.glyphId, outline))
                    return {};
                outlineIndex = static_cast<std::uint32_t>(result.outlines.size());
                result.outlines.push_back(std::move(outline));
                outlineByGlyph.emplace(glyph.glyphId, outlineIndex);
            } else {
                outlineIndex = found->second;
            }

            const std::int64_t positionedX = penX + glyph.xOffset;
            const std::int64_t positionedY = penY + glyph.yOffset;
            if (positionedX < std::numeric_limits<std::int32_t>::min() ||
                positionedX > std::numeric_limits<std::int32_t>::max() ||
                positionedY < std::numeric_limits<std::int32_t>::min() ||
                positionedY > std::numeric_limits<std::int32_t>::max())
                return {};

            result.glyphs.push_back({glyph.glyphId,
                                     glyph.cluster,
                                     outlineIndex,
                                     static_cast<std::int32_t>(positionedX),
                                     static_cast<std::int32_t>(positionedY),
                                     glyph.xAdvance,
                                     glyph.yAdvance,
                                     run.bidi.rightToLeft});
            penX += glyph.xAdvance;
            penY += glyph.yAdvance;
            if (penX < std::numeric_limits<std::int32_t>::min() ||
                penX > std::numeric_limits<std::int32_t>::max() ||
                penY < std::numeric_limits<std::int32_t>::min() ||
                penY > std::numeric_limits<std::int32_t>::max())
                return {};
        }
    }
    result.valid = true;
    return result;
}

} // namespace Vixen::Text
