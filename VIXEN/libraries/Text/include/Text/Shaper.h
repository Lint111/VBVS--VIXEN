#pragma once

#include "Text/Abi.h"
#include "Text/Bidi.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Vixen::Text {

struct ShapedGlyph {
    std::uint32_t glyphId = 0;
    std::uint32_t cluster = 0;
    std::int32_t xAdvance = 0;
    std::int32_t yAdvance = 0;
    std::int32_t xOffset = 0;
    std::int32_t yOffset = 0;
};

struct ShapedRun {
    BidiRun bidi;
    std::uint32_t scriptTag = 0;
    bool scriptRequired = false;
    std::vector<ShapedGlyph> glyphs;
};

struct ShapedText {
    // False means the resolved tag span did not match the count in PresetDesc.
    // Shaping still returns its best result so a caller can report the mismatch.
    bool requiredScriptTableMatchesPreset = false;
    std::vector<ShapedRun> visualRuns;
};

// requiredScriptTags are the resolved ISO 15924 tags for the PresetDesc's
// required-script ordinal slice. Visual bidi runs are shaped in order and
// HarfBuzz handles Unicode mirroring for RTL runs. Clusters remain byte offsets.
ShapedText ShapeUtf8(const PresetDesc& preset, std::span<const std::uint32_t> requiredScriptTags,
                     FT_Face fontFace, std::string_view utf8Text,
                     std::string_view language = {});

} // namespace Vixen::Text
