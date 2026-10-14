#pragma once

#include <cstdint>

namespace Vixen::Text {

// The reserved first RGBA texel identifies a callback texture as an MSDF page
// to the VIXEN UI renderer. Glyph cells never include this texel.
inline constexpr std::uint8_t kAtlasFormatMarker[4] = {0x4D, 0x53, 0x44, 0xFF};
inline constexpr std::uint32_t kAtlasDesignPixelsPerEm = 64;

} // namespace Vixen::Text
