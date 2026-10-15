#pragma once

#include "Text/Abi.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Vixen::Text {

struct LineBreakOpportunity {
    std::uint32_t byteOffset = 0;
    bool mandatory = false;
};

// Offsets are UTF-8 byte positions immediately after the code point before the break.
std::vector<LineBreakOpportunity> FindLineBreaks(const PresetDesc& preset,
                                                std::string_view utf8Text);

} // namespace Vixen::Text
