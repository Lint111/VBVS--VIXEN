#pragma once

#include "Text/Abi.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Vixen::Text {

struct BidiRun {
    std::uint32_t byteOffset = 0;
    std::uint32_t byteLength = 0;
    bool rightToLeft = false;
};

struct MirroredCodepoint {
    std::uint32_t byteOffset = 0;
    char32_t value = U'\0';
};

struct BidiResult {
    // Runs are returned in visual order. Offsets still point into the original UTF-8 text.
    std::vector<BidiRun> visualRuns;
    // UBA L4 substitutions, keyed by original UTF-8 byte offset.
    std::vector<MirroredCodepoint> mirroredCodepoints;
};

BidiResult ResolveBidi(const PresetDesc& preset, std::string_view utf8Text);

} // namespace Vixen::Text
