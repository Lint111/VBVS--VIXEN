// The CPU face of the baked glyph walk (see Recipe/GlyphBakeStream.h). It must give the bits the
// kernel tool gives, so nothing in this file may fuse a multiply and an add into one rounded
// operation: the whole translation unit is compiled without contraction.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma fp_contract(off)
#define VIXEN_GLYPH_BAKE_EXACT
#elif defined(__clang__)
#pragma clang fp contract(off)
#define VIXEN_GLYPH_BAKE_EXACT
#else
#define VIXEN_GLYPH_BAKE_EXACT __attribute__((optimize("fp-contract=off")))
#endif

#include "Recipe/GlyphBakeStream.h"

namespace Vixen::SVO::Recipe {

VIXEN_GLYPH_BAKE_EXACT
float EvaluateGlyphBakeBlock(const std::uint32_t* block, float px, float py,
                                                  std::uint64_t* chordsVisited) noexcept {
    auto f = [block](std::uint32_t index) { return std::bit_cast<float>(block[index]); };
    const std::int32_t columns = static_cast<std::int32_t>(block[1]);
    const std::int32_t rows = static_cast<std::int32_t>(block[2]);
    const float cellX = (px - f(8)) * f(10);
    const float cellY = (py - f(9)) * f(11);
    if (!(cellX >= 0.0f && cellY >= 0.0f && cellX < static_cast<float>(columns) && cellY < static_cast<float>(rows))) {
        const float outX = std::max(std::max(f(4) - px, px - f(6)), 0.0f);
        const float outY = std::max(std::max(f(5) - py, py - f(7)), 0.0f);
        return std::sqrt(outX * outX + outY * outY);
    }
    const std::int32_t ix = static_cast<std::int32_t>(cellX);
    const std::int32_t iy = static_cast<std::int32_t>(cellY);
    const std::uint32_t cell = kGlyphBakeBlockHeaderWords + static_cast<std::uint32_t>(iy * columns + ix) * kGlyphBakeCellWords;
    const std::uint32_t listWord = block[cell];
    const std::uint32_t boundWord = block[cell + 1];
    const std::uint32_t count = listWord & ((1u << kGlyphBakeListCountBits) - 1u);
    const std::uint32_t offset = listWord >> kGlyphBakeListCountBits;
    bool inside = (boundWord & 0x80000000u) != 0u;
    if (count == 0u) {
        const float bound = std::bit_cast<float>(boundWord & 0x7FFFFFFFu);
        return inside ? -bound : bound;
    }
    const float centreX = f(8) + (static_cast<float>(ix) + 0.5f) / f(10);
    const float centreY = f(9) + (static_cast<float>(iy) + 0.5f) / f(11);
    const float lowX = std::min(px, centreX), highX = std::max(px, centreX);
    const float lowY = std::min(py, centreY), highY = std::max(py, centreY);
    const std::uint32_t chordBase = kGlyphBakeBlockHeaderWords + static_cast<std::uint32_t>(columns * rows) * kGlyphBakeCellWords;
    const std::uint32_t listBase = chordBase + block[0] * kGlyphBakeChordWords;
    float minimumSquared = std::numeric_limits<float>::max();
    if (chordsVisited) *chordsVisited += count;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint32_t entry = offset + index;
        const std::uint32_t chord = chordBase + ((block[listBase + (entry >> 1)] >> ((entry & 1u) * 16u)) & 0xFFFFu) * kGlyphBakeChordWords;
        const float ax = f(chord), ay = f(chord + 1), bx = f(chord + 2), by = f(chord + 3);
        const float ex = bx - ax, ey = by - ay, wx = px - ax, wy = py - ay;
        const float lengthSquared = ex * ex + ey * ey;
        const float t = lengthSquared > 0.0f ? std::clamp((wx * ex + wy * ey) / lengthSquared, 0.0f, 1.0f) : 0.0f;
        const float dx = wx - ex * t, dy = wy - ey * t;
        minimumSquared = std::min(minimumSquared, dx * dx + dy * dy);
        // Sign: crossings of the path sample -> (centre x, sample y) -> centre, which stays in the cell.
        if ((ay <= py) != (by <= py)) {
            const float low = wy * ex - (lowX - ax) * ey, high = wy * ex - (highX - ax) * ey;
            const bool beyondLow = ey > 0.0f ? low > 0.0f : low < 0.0f;
            const bool beyondHigh = ey > 0.0f ? high > 0.0f : high < 0.0f;
            if (beyondLow && !beyondHigh) inside = !inside;
        }
        if ((ax <= centreX) != (bx <= centreX)) {
            const float vx = centreX - ax;
            const float low = vx * ey - (lowY - ay) * ex, high = vx * ey - (highY - ay) * ex;
            const bool beyondLow = ex > 0.0f ? low > 0.0f : low < 0.0f;
            const bool beyondHigh = ex > 0.0f ? high > 0.0f : high < 0.0f;
            if (beyondLow && !beyondHigh) inside = !inside;
        }
    }
    const float distance = std::sqrt(minimumSquared);
    return inside ? -distance : distance;
}

VIXEN_GLYPH_BAKE_EXACT
float EvaluateGlyphBakeRow(std::span<const std::uint32_t> stream, std::uint32_t rowIndex,
                                                float px, float py, std::uint64_t* chordsVisited) noexcept {
    float minimumDistance = std::bit_cast<float>(kGlyphBakeEmptyRowDistanceBits);
    if (stream.empty() || rowIndex >= stream[0]) return minimumDistance;
    const std::uint32_t rowBase = 1u + rowIndex * kGlyphBakeRowWords;
    const std::uint32_t placementBase = stream[rowBase + 0];
    const std::uint32_t placementCount = stream[rowBase + 1];
    for (std::uint32_t index = 0; index < placementCount; ++index) {
        const std::uint32_t placement = placementBase + index * kGlyphBakePlacementWords;
        const float fontPerRow = std::bit_cast<float>(stream[placement + 3]);
        const float qx = (px - std::bit_cast<float>(stream[placement + 1])) * fontPerRow;
        const float qy = (py - std::bit_cast<float>(stream[placement + 2])) * fontPerRow;
        const float distance = EvaluateGlyphBakeBlock(stream.data() + stream[placement + 0], qx, qy, chordsVisited) *
                               std::bit_cast<float>(stream[placement + 4]);
        minimumDistance = std::min(minimumDistance, distance);
    }
    return minimumDistance;
}

} // namespace Vixen::SVO::Recipe
