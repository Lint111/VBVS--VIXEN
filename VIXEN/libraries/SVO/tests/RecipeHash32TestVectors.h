#pragma once

#include <array>
#include <cstdint>

namespace Vixen::SVO::Recipe::TestVectors {

struct Hash32Vector { uint32_t input; uint32_t expected; };
inline constexpr std::array<Hash32Vector, 14> Hash32GoldenVectors = {{
    {0x00000000u, 0x00000000u}, {0x00000001u, 0x688990c0u},
    {0x00000002u, 0xd1132181u}, {0x0000002au, 0x172733c2u},
    {0xdeadbeefu, 0xe628c683u}, {0xffffffffu, 0x6768824au},
    {0x80000000u, 0xcc4b4124u}, {0x0000ffffu, 0x33cad8bau},
    {0x00010000u, 0xdcafae10u}, {0x12345678u, 0xf5e71c96u},
    {0x01010101u, 0x3a23c2fdu}, {0x7fffffffu, 0x8d29ffb8u},
    {0xc001d00du, 0x33bdc00du}, {0xabcdef01u, 0x08898fefu},
}};

struct Hash32CombineVector { uint32_t state; uint32_t value; uint32_t expected; };
inline constexpr std::array<Hash32CombineVector, 3> Hash32CombineGoldenVectors = {{
    {0x00000000u, 0x00000000u, 0x01fce552u},
    {0xdeadbeefu, 0x01020304u, 0x4f25b017u},
    {0x7fffffffu, 0xffffffffu, 0x827ce94fu},
}};

// lowbias32(seed), then combine size-class, cell-x, cell-y, cell-z, salt in this order.
inline constexpr uint32_t BodySeedFoldExpected = 0x4e08fd7cu;

}  // namespace Vixen::SVO::Recipe::TestVectors
