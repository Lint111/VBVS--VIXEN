#pragma once

// Baked glyph rows for the glyph walk (R892.13, R892.14, R892.19).
//
// A font is imported once by the kernel tool `glyph-bake` (GlyphBake~ in the Yeroket kernel):
// adaptive chords, overlaps removed, and per glyph a uniform cell grid whose cells list the
// chords a sample of the cell needs, or, farther than the exact band from the outline, a lower
// bound of the distance and the sign. This header reads the decompressed image the build step
// writes, packs the glyphs a scene uses into the words of the packed glyph buffer, and holds the
// CPU face of the walk. The GLSL face is SdfCore_GlyphOutlineBounded in
// shaders/RecipeGlyphBindings.glsl; the two are the same statements in the same order, and both
// reproduce GlyphBaker.Evaluate of the kernel tool.
//
// Packed stream (32-bit words):
//   [0] row count
//   rows        2 words each: first placement (word index), placement count
//   placements  5 words each: glyph block (word index), offset x, offset y (row units),
//               font units per row unit, row units per font unit
//   blocks      glyph blocks as baked (font units), each stored once
// A row is the union of its placements: the walk returns the least signed distance over them.
// That is exact outside every glyph, and exact inside while the glyphs of a row do not overlap;
// where they do overlap the sign is still right and the magnitude is a lower bound.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Vixen::SVO::Recipe {

inline constexpr std::uint32_t kGlyphBakeMagic = 0x42594C47u; // 'GLYB'
inline constexpr std::uint32_t kGlyphBakeLayoutVersion = 3;
inline constexpr std::uint32_t kGlyphBakeImageHeaderWords = 22;
inline constexpr std::uint32_t kGlyphBakeExactBandWord = 19;
inline constexpr std::uint32_t kGlyphBakeBlockHeaderWords = 12;
inline constexpr std::uint32_t kGlyphBakeCellWords = 2;
inline constexpr std::uint32_t kGlyphBakeChordWords = 4;
inline constexpr std::uint32_t kGlyphBakeListCountBits = 12;
inline constexpr std::uint32_t kGlyphBakeRowWords = 2;
inline constexpr std::uint32_t kGlyphBakePlacementWords = 5;
// What the walk returns for a row without placements: the value the segment walk returned for
// a row without segments.
inline constexpr std::uint32_t kGlyphBakeEmptyRowDistanceBits = 1621981420u;
// Added to the largest offset a recipe applies to a glyph when the exact band it needs is
// derived: room for the hit threshold and the normal taps around the offset surface.
inline constexpr float kGlyphBakeBandMarginEm = 0.005f;

// The decompressed image of one baked font: header, directory, glyph blocks. Every block is
// checked when the image is read, so the walk never reads outside the block of a glyph.
class GlyphBakeImage {
public:
    [[nodiscard]] static std::shared_ptr<const GlyphBakeImage> FromWords(std::vector<std::uint32_t> words,
                                                                         std::string* error = nullptr) {
        auto fail = [&](std::string reason) -> std::shared_ptr<const GlyphBakeImage> {
            if (error) *error = std::move(reason);
            return nullptr;
        };
        if (words.size() < kGlyphBakeImageHeaderWords || words[0] != kGlyphBakeMagic)
            return fail("not a glyph bake image");
        if (words[1] != kGlyphBakeLayoutVersion)
            return fail("glyph bake image has layout version " + std::to_string(words[1]) + ", this build reads " +
                        std::to_string(kGlyphBakeLayoutVersion) + "; bake the font again");
        const std::uint64_t glyphCount = words[3];
        if (words[2] == 0 || std::uint64_t{words[21]} + kGlyphBakeImageHeaderWords != words.size() ||
            kGlyphBakeImageHeaderWords + glyphCount * 2 > words.size())
            return fail("glyph bake image is truncated (a compressed .glyphbake file is not an image; "
                        "the build step writes the image with glyph-bake --image)");
        for (std::uint64_t glyphId = 0; glyphId < glyphCount; ++glyphId) {
            const std::uint64_t offset = words[kGlyphBakeImageHeaderWords + glyphId * 2];
            const std::uint64_t length = words[kGlyphBakeImageHeaderWords + glyphId * 2 + 1];
            if (length == 0) continue;
            if (offset < kGlyphBakeImageHeaderWords + glyphCount * 2 || offset + length > words.size() ||
                !BlockIsSound(std::span<const std::uint32_t>(words).subspan(offset, length)))
                return fail("glyph bake image: block of glyph " + std::to_string(glyphId) + " is malformed");
        }
        auto image = std::shared_ptr<GlyphBakeImage>(new GlyphBakeImage());
        image->words_ = std::move(words);
        return image;
    }

    [[nodiscard]] static std::shared_ptr<const GlyphBakeImage> Load(const std::filesystem::path& path,
                                                                    std::string* error = nullptr) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            if (error) *error = "cannot open glyph bake image " + path.string();
            return nullptr;
        }
        const std::streamoff size = file.tellg();
        std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(words.data()),
                  static_cast<std::streamsize>(words.size() * sizeof(std::uint32_t)));
        if (!file || size % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) {
            if (error) *error = "cannot read glyph bake image " + path.string();
            return nullptr;
        }
        std::string reason;
        auto image = FromWords(std::move(words), &reason);
        if (!image && error) *error = reason + " (" + path.string() + ")";
        return image;
    }

    [[nodiscard]] std::uint32_t UnitsPerEm() const noexcept { return words_[2]; }
    [[nodiscard]] std::uint32_t GlyphCount() const noexcept { return words_[3]; }
    // The band of the bake, in em; infinity when every cell is exact.
    [[nodiscard]] float ExactBandEm() const noexcept { return std::bit_cast<float>(words_[kGlyphBakeExactBandWord]); }
    [[nodiscard]] std::span<const std::uint32_t> Key() const noexcept {
        return std::span<const std::uint32_t>(words_).subspan(4, 8);
    }

    // The block of a glyph; empty for a glyph without an outline or outside the font.
    [[nodiscard]] std::span<const std::uint32_t> Block(std::uint32_t glyphId) const noexcept {
        if (glyphId >= GlyphCount()) return {};
        const std::uint32_t offset = words_[kGlyphBakeImageHeaderWords + glyphId * 2];
        const std::uint32_t length = words_[kGlyphBakeImageHeaderWords + glyphId * 2 + 1];
        return std::span<const std::uint32_t>(words_).subspan(offset, length);
    }

    // How far from the outline of this glyph the walk is exact, in em: the band of the bake,
    // and no farther than the grid reaches past the bounds (outside the grid the walk returns
    // the distance to the bounds).
    [[nodiscard]] float ExactReachEm(std::uint32_t glyphId) const noexcept {
        const std::span<const std::uint32_t> block = Block(glyphId);
        if (block.empty()) return std::numeric_limits<float>::infinity();
        auto f = [&](std::size_t index) { return std::bit_cast<float>(block[index]); };
        const float gridMaxX = f(8) + static_cast<float>(block[1]) / f(10);
        const float gridMaxY = f(9) + static_cast<float>(block[2]) / f(11);
        const float reach = std::min(std::min(f(4) - f(8), f(5) - f(9)), std::min(gridMaxX - f(6), gridMaxY - f(7)));
        return std::min(ExactBandEm(), reach / static_cast<float>(UnitsPerEm()));
    }

    [[nodiscard]] static bool BlockIsSound(std::span<const std::uint32_t> block) noexcept {
        if (block.size() < kGlyphBakeBlockHeaderWords) return false;
        const std::uint64_t chords = block[0], columns = block[1], rows = block[2], entries = block[3];
        if (chords == 0 || chords > 0xFFFFu || columns == 0 || columns > 1024 || rows == 0 || rows > 1024) return false;
        for (std::size_t index = 4; index < kGlyphBakeBlockHeaderWords; ++index)
            if (!std::isfinite(std::bit_cast<float>(block[index]))) return false;
        if (!(std::bit_cast<float>(block[10]) > 0.0f) || !(std::bit_cast<float>(block[11]) > 0.0f)) return false;
        const std::uint64_t chordBase = kGlyphBakeBlockHeaderWords + columns * rows * kGlyphBakeCellWords;
        const std::uint64_t listBase = chordBase + chords * kGlyphBakeChordWords;
        if (listBase + (entries + 1) / 2 != block.size()) return false;
        for (std::uint64_t cell = 0; cell < columns * rows; ++cell) {
            const std::uint32_t listWord = block[kGlyphBakeBlockHeaderWords + cell * kGlyphBakeCellWords];
            const std::uint64_t count = listWord & ((1u << kGlyphBakeListCountBits) - 1u);
            if ((listWord >> kGlyphBakeListCountBits) + count > entries) return false;
        }
        for (std::uint64_t entry = 0; entry < entries; ++entry)
            if (((block[listBase + (entry >> 1)] >> ((entry & 1) * 16)) & 0xFFFFu) >= chords) return false;
        for (std::uint64_t index = chordBase; index < listBase; ++index)
            if (!std::isfinite(std::bit_cast<float>(block[index]))) return false;
        return true;
    }

private:
    GlyphBakeImage() = default;
    std::vector<std::uint32_t> words_;
};

// The bakes of one font face, one per exact band (the band is a bake parameter). A row uses the
// narrowest band that covers what its recipes need.
struct GlyphBakeFace {
    std::string name;
    std::vector<std::shared_ptr<const GlyphBakeImage>> images;

    // The image with the narrowest band that is exact to `neededEm` around every glyph listed;
    // null when no bake of the face reaches that far.
    [[nodiscard]] const GlyphBakeImage* Select(float neededEm, std::span<const std::uint32_t> glyphIds) const noexcept {
        const GlyphBakeImage* best = nullptr;
        for (const auto& image : images) {
            if (!image) continue;
            bool reaches = true;
            for (const std::uint32_t glyphId : glyphIds)
                reaches = reaches && !image->Block(glyphId).empty() && image->ExactReachEm(glyphId) >= neededEm;
            if (reaches && (!best || image->ExactBandEm() < best->ExactBandEm())) best = image.get();
        }
        return best;
    }

    [[nodiscard]] float WidestBandEm() const noexcept {
        float widest = 0.0f;
        for (const auto& image : images)
            if (image) widest = std::max(widest, image->ExactBandEm());
        return widest;
    }
};

struct GlyphBakePlacement {
    std::uint32_t glyphId = 0;
    // Where the glyph origin sits in the row, row units.
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    friend bool operator==(const GlyphBakePlacement&, const GlyphBakePlacement&) = default;
};

// One GlyphOutline row as baked glyphs: the glyphs of one face placed in the row's own units
// (26.6 pixel units for shaped text). An empty row has no baked form.
struct GlyphBakedRow {
    std::shared_ptr<const GlyphBakeFace> face;
    float fontUnitsPerRowUnit = 1.0f;
    std::vector<GlyphBakePlacement> placements;

    [[nodiscard]] bool Baked() const noexcept { return face != nullptr && !placements.empty(); }
    friend bool operator==(const GlyphBakedRow&, const GlyphBakedRow&) = default;

    [[nodiscard]] std::vector<std::uint32_t> GlyphIds() const {
        std::vector<std::uint32_t> ids;
        ids.reserve(placements.size());
        for (const GlyphBakePlacement& placement : placements) ids.push_back(placement.glyphId);
        return ids;
    }

    // Bounds of the row in row units, from the baked bounds of its glyphs in `image`.
    [[nodiscard]] bool Bounds(const GlyphBakeImage& image, float& minX, float& minY, float& maxX, float& maxY) const noexcept {
        minX = minY = std::numeric_limits<float>::max();
        maxX = maxY = std::numeric_limits<float>::lowest();
        for (const GlyphBakePlacement& placement : placements) {
            const std::span<const std::uint32_t> block = image.Block(placement.glyphId);
            if (block.empty()) return false;
            auto f = [&](std::size_t index) { return std::bit_cast<float>(block[index]) / fontUnitsPerRowUnit; };
            minX = std::min(minX, placement.offsetX + f(4));
            minY = std::min(minY, placement.offsetY + f(5));
            maxX = std::max(maxX, placement.offsetX + f(6));
            maxY = std::max(maxY, placement.offsetY + f(7));
        }
        return !placements.empty();
    }
};

// Packs rows into the stream the walk reads. `neededBandEm[row]` is the exact band the recipes
// using the row need (0 when absent). A row that is not baked gets no placements. Returns false,
// with the reason, when a row needs a band no bake of its face has.
[[nodiscard]] inline bool BuildGlyphBakeStream(std::span<const GlyphBakedRow> rows,
                                               std::span<const float> neededBandEm,
                                               std::vector<std::uint32_t>& words, std::string* error = nullptr) {
    words.clear();
    std::size_t placementCount = 0;
    for (const GlyphBakedRow& row : rows)
        if (row.Baked()) placementCount += row.placements.size();
    const std::size_t placementBase = 1 + rows.size() * kGlyphBakeRowWords;
    words.assign(placementBase + placementCount * kGlyphBakePlacementWords, 0u);
    words[0] = static_cast<std::uint32_t>(rows.size());
    std::map<std::pair<const GlyphBakeImage*, std::uint32_t>, std::uint32_t> blockBases;
    std::size_t placementCursor = placementBase;
    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const GlyphBakedRow& row = rows[rowIndex];
        if (!row.Baked()) continue;
        const float needed = rowIndex < neededBandEm.size() ? neededBandEm[rowIndex] : 0.0f;
        const std::vector<std::uint32_t> glyphIds = row.GlyphIds();
        const GlyphBakeImage* image = row.face->Select(needed, glyphIds);
        if (!image) {
            if (error)
                *error = "glyph row " + std::to_string(rowIndex) + " of face '" + row.face->name + "' needs an exact band of " +
                         std::to_string(needed) + " em; the widest bake of the face has " +
                         std::to_string(row.face->WidestBandEm()) + " em (or a glyph of the row has no outline). "
                         "Fix: bake the font with a wider --exact-band-em";
            words.clear();
            return false;
        }
        const std::size_t rowBase = 1 + rowIndex * kGlyphBakeRowWords;
        words[rowBase + 0] = static_cast<std::uint32_t>(placementCursor);
        words[rowBase + 1] = static_cast<std::uint32_t>(row.placements.size());
        for (const GlyphBakePlacement& placement : row.placements) {
            auto [found, inserted] = blockBases.try_emplace({image, placement.glyphId}, 0u);
            if (inserted) {
                const std::span<const std::uint32_t> block = image->Block(placement.glyphId);
                found->second = static_cast<std::uint32_t>(words.size());
                words.insert(words.end(), block.begin(), block.end());
            }
            words[placementCursor + 0] = found->second;
            words[placementCursor + 1] = std::bit_cast<std::uint32_t>(placement.offsetX);
            words[placementCursor + 2] = std::bit_cast<std::uint32_t>(placement.offsetY);
            words[placementCursor + 3] = std::bit_cast<std::uint32_t>(row.fontUnitsPerRowUnit);
            words[placementCursor + 4] = std::bit_cast<std::uint32_t>(1.0f / row.fontUnitsPerRowUnit);
            placementCursor += kGlyphBakePlacementWords;
        }
    }
    return true;
}

// The walk over one baked glyph block, in font units: the statements of GlyphBaker.Evaluate.
// `chordsVisited`, when given, counts the chords measured (the cost of the sample).
// Defined in src/Recipe/GlyphBakeWalk.cpp, a translation unit compiled without fused
// multiply-add, so the CPU face gives the bits the kernel tool gives.
[[nodiscard]] float EvaluateGlyphBakeBlock(const std::uint32_t* block, float px, float py,
                                           std::uint64_t* chordsVisited = nullptr) noexcept;

// The walk over one packed row, in row units: the least signed distance over its placements.
[[nodiscard]] float EvaluateGlyphBakeRow(std::span<const std::uint32_t> stream, std::uint32_t rowIndex,
                                         float px, float py, std::uint64_t* chordsVisited = nullptr) noexcept;

// True when the packed stream holds baked placements for the row.
[[nodiscard]] inline bool GlyphBakeStreamHasRow(std::span<const std::uint32_t> stream, std::uint32_t rowIndex) noexcept {
    return !stream.empty() && rowIndex < stream[0] && stream[1u + rowIndex * kGlyphBakeRowWords + 1u] != 0u;
}

} // namespace Vixen::SVO::Recipe
