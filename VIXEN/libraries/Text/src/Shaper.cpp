#include "Text/Shaper.h"

#include <SheenBidi/SBCodepointSequence.h>

#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <limits>
#include <memory>

namespace Vixen::Text {
namespace {

hb_script_t RunScript(const SBCodepointSequence& sequence, const BidiRun& run) {
    const SBUInteger end = static_cast<SBUInteger>(run.byteOffset + run.byteLength);
    SBUInteger byteOffset = static_cast<SBUInteger>(run.byteOffset);
    hb_unicode_funcs_t* unicodeFunctions = hb_unicode_funcs_get_default();
    while (byteOffset < end) {
        const SBUInteger originalOffset = byteOffset;
        const SBCodepoint codepoint = SBCodepointSequenceGetCodepointAt(&sequence, &byteOffset);
        if (codepoint == SBCodepointInvalid || byteOffset <= originalOffset)
            break;
        const hb_script_t script = hb_unicode_script(
            unicodeFunctions, static_cast<hb_codepoint_t>(codepoint));
        if (script != HB_SCRIPT_COMMON && script != HB_SCRIPT_INHERITED &&
            script != HB_SCRIPT_UNKNOWN && script != HB_SCRIPT_INVALID)
            return script;
    }
    return HB_SCRIPT_COMMON;
}

hb_buffer_cluster_level_t ClusterLevel(ShapingClass shapingClass) {
    switch (shapingClass) {
    case ShapingClass::Joining:
    case ShapingClass::Indic:
        return HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES;
    case ShapingClass::Cluster:
        return HB_BUFFER_CLUSTER_LEVEL_CHARACTERS;
    case ShapingClass::Default:
    default:
        return HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS;
    }
}

bool IsScriptRequired(std::span<const std::uint32_t> requiredScriptTags,
                      std::uint32_t scriptTag) {
    return std::find(requiredScriptTags.begin(), requiredScriptTags.end(), scriptTag) !=
           requiredScriptTags.end();
}

} // namespace

ShapedText ShapeUtf8(const PresetDesc& preset,
                     std::span<const std::uint32_t> requiredScriptTags,
                     FT_Face fontFace, std::string_view utf8Text,
                     std::string_view language) {
    ShapedText result;
    result.requiredScriptTableMatchesPreset =
        requiredScriptTags.size() == preset.requiredScriptOrdinalCount;
    if (!fontFace || utf8Text.empty() ||
        utf8Text.size() > std::numeric_limits<std::uint32_t>::max() ||
        utf8Text.size() > std::numeric_limits<SBUInteger>::max())
        return result;

    using HarfBuzzFont = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;
    HarfBuzzFont font(hb_ft_font_create_referenced(fontFace), &hb_font_destroy);
    if (!font)
        return result;
    hb_ft_font_set_load_flags(font.get(), FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
    hb_ft_font_changed(font.get());

    const SBCodepointSequence sequence{
        SBStringEncodingUTF8,
        utf8Text.data(),
        static_cast<SBUInteger>(utf8Text.size())};
    const BidiResult bidi = ResolveBidi(preset, utf8Text);
    const hb_language_t hbLanguage = language.empty()
                                        ? HB_LANGUAGE_INVALID
                                        : hb_language_from_string(
                                              language.data(),
                                              static_cast<int>(std::min<std::size_t>(
                                                  language.size(),
                                                  static_cast<std::size_t>(std::numeric_limits<int>::max()))));

    for (const BidiRun& bidiRun : bidi.visualRuns) {
        ShapedRun shapedRun;
        shapedRun.bidi = bidiRun;
        const hb_script_t script = RunScript(sequence, bidiRun);
        shapedRun.scriptTag = static_cast<std::uint32_t>(hb_script_to_iso15924_tag(script));
        shapedRun.scriptRequired = IsScriptRequired(requiredScriptTags, shapedRun.scriptTag);

        using HarfBuzzBuffer = std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)>;
        HarfBuzzBuffer buffer(hb_buffer_create(), &hb_buffer_destroy);
        if (!buffer || !hb_buffer_allocation_successful(buffer.get()))
            continue;

        hb_buffer_set_direction(buffer.get(), bidiRun.rightToLeft ? HB_DIRECTION_RTL
                                                                  : HB_DIRECTION_LTR);
        hb_buffer_set_script(buffer.get(), script);
        if (hbLanguage != HB_LANGUAGE_INVALID)
            hb_buffer_set_language(buffer.get(), hbLanguage);
        hb_buffer_set_cluster_level(buffer.get(), ClusterLevel(preset.shapingClass));

        const SBUInteger runEnd = static_cast<SBUInteger>(bidiRun.byteOffset + bidiRun.byteLength);
        SBUInteger byteOffset = static_cast<SBUInteger>(bidiRun.byteOffset);
        while (byteOffset < runEnd) {
            const SBUInteger originalOffset = byteOffset;
            const SBCodepoint codepoint = SBCodepointSequenceGetCodepointAt(&sequence, &byteOffset);
            if (codepoint == SBCodepointInvalid || byteOffset <= originalOffset)
                break;
            hb_buffer_add(buffer.get(), static_cast<hb_codepoint_t>(codepoint),
                          static_cast<unsigned int>(originalOffset));
        }
        if (!hb_buffer_allocation_successful(buffer.get()))
            continue;

        // SheenBidi supplies visual run order. HarfBuzz applies Unicode's
        // Bidi_Mirrored mapping while shaping RTL runs and retains input clusters.
        hb_shape(font.get(), buffer.get(), nullptr, 0);
        if (!hb_buffer_allocation_successful(buffer.get()))
            continue;

        unsigned int glyphCount = 0;
        const hb_glyph_info_t* glyphInfos = hb_buffer_get_glyph_infos(buffer.get(), &glyphCount);
        const hb_glyph_position_t* glyphPositions = hb_buffer_get_glyph_positions(buffer.get(), &glyphCount);
        if (!glyphInfos || !glyphPositions)
            continue;

        shapedRun.glyphs.reserve(glyphCount);
        for (unsigned int glyphIndex = 0; glyphIndex < glyphCount; ++glyphIndex) {
            shapedRun.glyphs.push_back({glyphInfos[glyphIndex].codepoint,
                                        glyphInfos[glyphIndex].cluster,
                                        glyphPositions[glyphIndex].x_advance,
                                        glyphPositions[glyphIndex].y_advance,
                                        glyphPositions[glyphIndex].x_offset,
                                        glyphPositions[glyphIndex].y_offset});
        }
        result.visualRuns.push_back(std::move(shapedRun));
    }
    return result;
}

} // namespace Vixen::Text
