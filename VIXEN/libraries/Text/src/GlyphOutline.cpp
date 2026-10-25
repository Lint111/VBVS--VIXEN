#include "Text/GlyphOutline.h"

#include "Recipe/generated/GlyphOutlineInstance.g.hpp"
#include "Recipe/generated/PathInstance.g.hpp"
#include "Recipe/generated/SdfCoreKernels.g.hpp"
#include "Text/GlyphAtlas.h"

#include <ft2build.h>
#include FT_OUTLINE_H

#include <array>
#include <cstdint>
#include <limits>

namespace Vixen::Text {
namespace {

struct OutlineBuilder {
    GlyphOutlineInstance* instance = nullptr;
    FT_Vector contourStart{};
    FT_Vector current{};
    std::uint64_t requiredSegments = 0;
    bool contourOpen = false;
    bool startsNewContour = false;
    bool overflow = false;
    bool malformed = false;
};

void StorePoint(PathPoint& target, const FT_Vector& point) {
    target.X = static_cast<float>(point.x);
    target.Y = static_cast<float>(point.y);
}

enum class SegmentKind : std::uint32_t { Line, Quadratic, Cubic };

void AppendSegment(OutlineBuilder& builder, SegmentKind kind,
                   const FT_Vector& control1, const FT_Vector& control2,
                   const FT_Vector& end) {
    if (!builder.contourOpen || !builder.instance) {
        builder.malformed = true;
        return;
    }

    ++builder.requiredSegments;
    if (builder.instance->SegmentCount >= Yeroket::Sdf::Kernels::SegmentCapacity) {
        builder.overflow = true;
        return;
    }

    PathSegment segment{};
    StorePoint(segment.P0, builder.current);
    StorePoint(segment.P1, kind == SegmentKind::Line ? end : control1);
    StorePoint(segment.P2, kind == SegmentKind::Line ? end :
               kind == SegmentKind::Quadratic ? end : control2);
    StorePoint(segment.P3, end);
    segment.Kind = static_cast<std::uint32_t>(kind);
    segment.ContourStart = builder.startsNewContour ? 1U : 0U;
    builder.instance->Segments[builder.instance->SegmentCount++] = segment;
    builder.startsNewContour = false;
}

void FinishContour(OutlineBuilder& builder) {
    if (!builder.contourOpen || builder.malformed)
        return;
    if (builder.current.x != builder.contourStart.x ||
        builder.current.y != builder.contourStart.y) {
        const FT_Vector zero{};
        AppendSegment(builder, SegmentKind::Line, zero, zero, builder.contourStart);
    }
    builder.contourOpen = false;
}

int MoveTo(const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    FinishContour(builder);
    if (builder.malformed)
        return 1;
    builder.contourStart = *to;
    builder.current = *to;
    builder.contourOpen = true;
    builder.startsNewContour = true;
    return 0;
}

int LineTo(const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    const FT_Vector zero{};
    AppendSegment(builder, SegmentKind::Line, zero, zero, *to);
    builder.current = *to;
    return builder.malformed ? 1 : 0;
}

int ConicTo(const FT_Vector* control, const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    const FT_Vector zero{};
    AppendSegment(builder, SegmentKind::Quadratic, *control, zero, *to);
    builder.current = *to;
    return builder.malformed ? 1 : 0;
}

int CubicTo(const FT_Vector* control1, const FT_Vector* control2,
            const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    AppendSegment(builder, SegmentKind::Cubic, *control1, *control2, *to);
    builder.current = *to;
    return builder.malformed ? 1 : 0;
}

constexpr FT_Outline_Funcs kOutlineCallbacks{
    &MoveTo,
    &LineTo,
    &ConicTo,
    &CubicTo,
    0,
    0};

void SetDiagnostic(GlyphOutlineBakeDiagnostic* diagnostic, std::uint32_t glyphId,
                   GlyphOutlineBakeDiagnosticCode code, std::uint64_t requiredSegments,
                   std::uint32_t capacity) {
    if (!diagnostic)
        return;
    diagnostic->glyphId = glyphId;
    diagnostic->code = code;
    diagnostic->requiredSegments = static_cast<std::uint32_t>(
        requiredSegments > std::numeric_limits<std::uint32_t>::max()
            ? std::numeric_limits<std::uint32_t>::max()
            : requiredSegments);
    diagnostic->segmentCapacity = capacity;
}

} // namespace

bool LoadGlyphOutline(FT_Face fontFace, std::uint32_t glyphId,
                      GlyphOutlineInstance& instance,
                      GlyphOutlineBakeDiagnostic* diagnostic) {
    instance = {};
    const auto capacity = static_cast<std::uint32_t>(Yeroket::Sdf::Kernels::SegmentCapacity);
    SetDiagnostic(diagnostic, glyphId, GlyphOutlineBakeDiagnosticCode::None, 0, capacity);
    if (!fontFace || glyphId >= static_cast<std::uint32_t>(fontFace->num_glyphs)) {
        SetDiagnostic(diagnostic, glyphId, GlyphOutlineBakeDiagnosticCode::InvalidFace, 0, capacity);
        return false;
    }
    if (FT_Load_Glyph(fontFace, glyphId, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) != 0) {
        SetDiagnostic(diagnostic, glyphId, GlyphOutlineBakeDiagnosticCode::GlyphLoadFailed, 0, capacity);
        return false;
    }

    if (fontFace->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        return true;

    FT_Outline& ftOutline = fontFace->glyph->outline;
    instance.FillRule = (ftOutline.flags & FT_OUTLINE_EVEN_ODD_FILL) != 0 ? 1U : 0U;
    if (ftOutline.n_points == 0 || ftOutline.n_contours == 0)
        return true;

    FT_BBox bounds{};
    FT_Outline_Get_CBox(&ftOutline, &bounds);
    instance.BoundsMin.X = static_cast<float>(bounds.xMin);
    instance.BoundsMin.Y = static_cast<float>(bounds.yMin);
    instance.BoundsMax.X = static_cast<float>(bounds.xMax);
    instance.BoundsMax.Y = static_cast<float>(bounds.yMax);

    OutlineBuilder builder{};
    builder.instance = &instance;
    if (FT_Outline_Decompose(&ftOutline, &kOutlineCallbacks, &builder) != 0 || builder.malformed) {
        instance = {};
        SetDiagnostic(diagnostic, glyphId, GlyphOutlineBakeDiagnosticCode::GlyphLoadFailed,
                      builder.requiredSegments, capacity);
        return false;
    }
    FinishContour(builder);
    if (builder.malformed) {
        instance = {};
        SetDiagnostic(diagnostic, glyphId, GlyphOutlineBakeDiagnosticCode::GlyphLoadFailed,
                      builder.requiredSegments, capacity);
        return false;
    }
    if (builder.overflow) {
        SetDiagnostic(diagnostic, glyphId, GlyphOutlineBakeDiagnosticCode::SegmentCapacityExceeded,
                      builder.requiredSegments, capacity);
        return false;
    }
    return true;
}

WorldTextLayout ShapeWorldText(const PresetDesc& preset,
                               std::span<const std::uint32_t> requiredScriptTags,
                               GlyphAtlas& glyphAtlas,
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

    std::int64_t penX = 0;
    std::int64_t penY = 0;
    for (const ShapedRun& run : shaped.visualRuns) {
        for (const ShapedGlyph& glyph : run.glyphs) {
            GlyphOutlineBakeDiagnostic diagnostic;
            std::shared_ptr<const GlyphOutlineInstance> instance =
                glyphAtlas.GetOrAddOutline(fontFace, glyph.glyphId, &diagnostic);
            if (!instance) {
                result.diagnostics.push_back(diagnostic);
                return result;
            }
            const std::uint32_t outlineIndex = glyphAtlas.GetOrAddOutlineIndex(
                fontFace, glyph.glyphId, nullptr);
            if (outlineIndex == std::numeric_limits<std::uint32_t>::max())
                return {};

            const std::int64_t positionedX = penX + glyph.xOffset;
            const std::int64_t positionedY = penY + glyph.yOffset;
            if (positionedX < std::numeric_limits<std::int32_t>::min() ||
                positionedX > std::numeric_limits<std::int32_t>::max() ||
                positionedY < std::numeric_limits<std::int32_t>::min() ||
                positionedY > std::numeric_limits<std::int32_t>::max())
                return {};

            result.glyphs.push_back({glyph.glyphId,
                                     outlineIndex,
                                     glyph.cluster,
                                     std::move(instance),
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
