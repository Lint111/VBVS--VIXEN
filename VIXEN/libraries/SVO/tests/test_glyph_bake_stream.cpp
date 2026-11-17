// Baked glyph rows (R892.14): the image reader, the packed stream and the CPU face of the walk.
//
// The fonts are baked by the build (target vixen_glyph_bake, kernel tool glyph-bake). Beside each
// image the tool writes samples: points and the value its own walk returned for them. The C++
// face must return the same bits for every one of them.

#include "Recipe/GlyphBakeStream.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace Vixen::SVO::Recipe;

std::vector<std::uint32_t> ReadWords(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    std::vector<std::uint32_t> words(static_cast<std::size_t>(file.tellg()) / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(words.size() * sizeof(std::uint32_t)));
    return words;
}

std::shared_ptr<const GlyphBakeImage> LoadImage(const char* path) {
    std::string error;
    auto image = GlyphBakeImage::Load(path, &error);
    EXPECT_NE(image, nullptr) << error << "; fix: build target vixen_glyph_bake";
    return image;
}

std::shared_ptr<const GlyphBakeFace> FaceOf(std::shared_ptr<const GlyphBakeImage> image) {
    auto face = std::make_shared<GlyphBakeFace>();
    face->name = "test";
    face->images.push_back(std::move(image));
    return face;
}

// The first glyphs of the font that have an outline.
std::vector<std::uint32_t> OutlinedGlyphs(const GlyphBakeImage& image, std::size_t count) {
    std::vector<std::uint32_t> ids;
    for (std::uint32_t glyphId = 0; glyphId < image.GlyphCount() && ids.size() < count; ++glyphId)
        if (!image.Block(glyphId).empty()) ids.push_back(glyphId);
    return ids;
}

struct BakedFont {
    const char* name;
    const char* image;
    const char* samples;
    std::uint32_t unitsPerEm;
    std::uint32_t glyphCount;
};

const BakedFont kFonts[] = {
    {"Lato", GLYPH_BAKE_LATO_IMAGE, GLYPH_BAKE_LATO_SAMPLES, 2000u, 467u},
    {"NotoSansArabic", GLYPH_BAKE_NOTO_IMAGE, GLYPH_BAKE_NOTO_SAMPLES, 1000u, 1711u},
};

TEST(GlyphBakeImage, ReadsTheHeaderOfEachBakedFont) {
    for (const BakedFont& font : kFonts) {
        SCOPED_TRACE(font.name);
        const auto image = LoadImage(font.image);
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->UnitsPerEm(), font.unitsPerEm);
        EXPECT_EQ(image->GlyphCount(), font.glyphCount);
        EXPECT_EQ(image->ExactBandEm(), 0.03f);
        EXPECT_TRUE(image->Block(font.glyphCount).empty());
        EXPECT_TRUE(image->Block(0xFFFFFFFFu).empty());
    }
}

TEST(GlyphBakeImage, TheWalkIsExactToTheBandAroundEveryGlyph) {
    for (const BakedFont& font : kFonts) {
        SCOPED_TRACE(font.name);
        const auto image = LoadImage(font.image);
        ASSERT_NE(image, nullptr);
        std::uint32_t outlined = 0;
        for (std::uint32_t glyphId = 0; glyphId < image->GlyphCount(); ++glyphId) {
            if (image->Block(glyphId).empty()) continue;
            ++outlined;
            // The grid of a small glyph (a period) reaches the band past its bounds too.
            ASSERT_GE(image->ExactReachEm(glyphId), image->ExactBandEm()) << "glyph " << glyphId;
        }
        EXPECT_GT(outlined, font.glyphCount * 9u / 10u);
    }
}

TEST(GlyphBakeImage, RefusesWordsThatAreNotASoundImage) {
    const std::vector<std::uint32_t> words = ReadWords(GLYPH_BAKE_LATO_IMAGE);
    ASSERT_GT(words.size(), kGlyphBakeImageHeaderWords);
    std::string error;
    EXPECT_NE(GlyphBakeImage::FromWords(words, &error), nullptr) << error;

    auto truncated = words;
    truncated.resize(truncated.size() - 7);
    EXPECT_EQ(GlyphBakeImage::FromWords(truncated, &error), nullptr);
    EXPECT_NE(error.find("truncated"), std::string::npos) << error;

    auto otherVersion = words;
    otherVersion[1] = 2;
    EXPECT_EQ(GlyphBakeImage::FromWords(otherVersion, &error), nullptr);
    EXPECT_NE(error.find("layout version 2"), std::string::npos) << error;

    auto otherMagic = words;
    otherMagic[0] = 0;
    EXPECT_EQ(GlyphBakeImage::FromWords(otherMagic, &error), nullptr);

    // A list entry that names a chord the glyph does not have would send the walk outside the block.
    const auto image = GlyphBakeImage::FromWords(words);
    ASSERT_NE(image, nullptr);
    const std::uint32_t glyphId = OutlinedGlyphs(*image, 1).front();
    const std::uint32_t blockOffset = words[kGlyphBakeImageHeaderWords + glyphId * 2];
    const std::uint32_t blockWords = words[kGlyphBakeImageHeaderWords + glyphId * 2 + 1];
    auto badList = words;
    badList[blockOffset + blockWords - 1] = 0xFFFFFFFFu;
    EXPECT_EQ(GlyphBakeImage::FromWords(badList, &error), nullptr);
    EXPECT_NE(error.find("glyph " + std::to_string(glyphId)), std::string::npos) << error;

    auto badCell = words;
    badCell[blockOffset + kGlyphBakeBlockHeaderWords] = 0xFFFFFFFFu;
    EXPECT_EQ(GlyphBakeImage::FromWords(badCell, &error), nullptr);

    EXPECT_EQ(GlyphBakeImage::Load("no-such-file.glyphimage", &error), nullptr);
    EXPECT_NE(error.find("cannot open"), std::string::npos) << error;
}

TEST(GlyphBakeWalk, ReproducesTheKernelToolBitForBit) {
    for (const BakedFont& font : kFonts) {
        SCOPED_TRACE(font.name);
        const auto image = LoadImage(font.image);
        ASSERT_NE(image, nullptr);
        const std::vector<std::uint32_t> samples = ReadWords(font.samples);
        ASSERT_GE(samples.size(), 2u) << font.samples;
        ASSERT_EQ(samples[0], 0x53594C47u);
        ASSERT_EQ(samples.size(), 2u + std::size_t{samples[1]} * 4u);
        std::uint64_t mismatches = 0, inside = 0, chords = 0;
        for (std::size_t row = 0; row < samples[1]; ++row) {
            const std::size_t at = 2 + row * 4;
            const std::span<const std::uint32_t> block = image->Block(samples[at]);
            ASSERT_FALSE(block.empty()) << "glyph " << samples[at];
            const float value = EvaluateGlyphBakeBlock(block.data(), std::bit_cast<float>(samples[at + 1]),
                                                       std::bit_cast<float>(samples[at + 2]), &chords);
            if (std::bit_cast<std::uint32_t>(value) != samples[at + 3]) {
                if (++mismatches <= 5)
                    ADD_FAILURE() << "glyph " << samples[at] << " at (" << std::bit_cast<float>(samples[at + 1]) << ", "
                                  << std::bit_cast<float>(samples[at + 2]) << "): C++ " << value << ", kernel tool "
                                  << std::bit_cast<float>(samples[at + 3]);
            }
            inside += value < 0.0f ? 1u : 0u;
        }
        EXPECT_EQ(mismatches, 0u) << "of " << samples[1] << " samples";
        EXPECT_GT(samples[1], font.glyphCount * 50u);
        EXPECT_GT(inside, samples[1] / 20u) << "the samples reach inside the glyphs";
        RecordProperty(std::string(font.name) + "_samples", static_cast<int>(samples[1]));
        std::printf("[GLYPH_BAKE] %s: %u samples, 0 expected mismatches, %llu found, %.2f chords per sample\n", font.name,
                    samples[1], static_cast<unsigned long long>(mismatches),
                    static_cast<double>(chords) / static_cast<double>(samples[1]));
    }
}

TEST(GlyphBakeStream, ARowIsTheLeastDistanceOverItsPlacements) {
    const auto image = LoadImage(GLYPH_BAKE_LATO_IMAGE);
    ASSERT_NE(image, nullptr);
    const std::vector<std::uint32_t> ids = OutlinedGlyphs(*image, 3);
    ASSERT_EQ(ids.size(), 3u);
    const float fontPerRow = 2000.0f / 1536.0f; // Lato at 24 px in 26.6 units
    std::vector<GlyphBakedRow> rows(3);
    rows[0] = {FaceOf(image), fontPerRow, {{ids[0], 0.0f, 0.0f}}};
    // Row 1 is not baked; row 2 places three glyphs, one of them twice.
    rows[2] = {rows[0].face, fontPerRow, {{ids[0], -900.0f, 40.0f}, {ids[1], 100.0f, 0.0f}, {ids[2], 1300.0f, -64.0f}, {ids[1], 2600.0f, 0.0f}}};
    std::vector<std::uint32_t> stream;
    std::string error;
    ASSERT_TRUE(BuildGlyphBakeStream(rows, {}, stream, &error)) << error;
    EXPECT_EQ(stream[0], 3u);
    EXPECT_TRUE(GlyphBakeStreamHasRow(stream, 0));
    EXPECT_FALSE(GlyphBakeStreamHasRow(stream, 1));
    EXPECT_TRUE(GlyphBakeStreamHasRow(stream, 2));
    EXPECT_FALSE(GlyphBakeStreamHasRow(stream, 3));
    // Each glyph block is stored once, however many placements use it.
    const std::size_t expected = 1 + 3 * kGlyphBakeRowWords + 5 * kGlyphBakePlacementWords +
        image->Block(ids[0]).size() + image->Block(ids[1]).size() + image->Block(ids[2]).size();
    EXPECT_EQ(stream.size(), expected);

    const float empty = std::bit_cast<float>(kGlyphBakeEmptyRowDistanceBits);
    EXPECT_EQ(EvaluateGlyphBakeRow(stream, 1, 10.0f, 10.0f), empty);
    EXPECT_EQ(EvaluateGlyphBakeRow(stream, 7, 10.0f, 10.0f), empty);
    EXPECT_EQ(EvaluateGlyphBakeRow({}, 0, 10.0f, 10.0f), empty);

    float minX, minY, maxX, maxY;
    ASSERT_TRUE(rows[2].Bounds(*image, minX, minY, maxX, maxY));
    std::uint32_t state = 12345u, negative = 0;
    for (int sample = 0; sample < 20000; ++sample) {
        auto next = [&state]() { state = state * 1664525u + 1013904223u; return static_cast<float>(state >> 8) / 16777216.0f; };
        const float px = minX - 200.0f + next() * (maxX - minX + 400.0f);
        const float py = minY - 200.0f + next() * (maxY - minY + 400.0f);
        float expectedDistance = empty;
        for (const GlyphBakePlacement& placement : rows[2].placements) {
            const float qx = (px - placement.offsetX) * fontPerRow, qy = (py - placement.offsetY) * fontPerRow;
            expectedDistance = std::min(expectedDistance,
                EvaluateGlyphBakeBlock(image->Block(placement.glyphId).data(), qx, qy) * (1.0f / fontPerRow));
        }
        const float value = EvaluateGlyphBakeRow(stream, 2, px, py);
        ASSERT_EQ(std::bit_cast<std::uint32_t>(value), std::bit_cast<std::uint32_t>(expectedDistance)) << px << ", " << py;
        negative += value < 0.0f ? 1u : 0u;
    }
    EXPECT_GT(negative, 200u) << "the samples reach inside the placed glyphs";
}

TEST(GlyphBakeStream, ARowUsesTheNarrowestBakeThatCoversItsBand) {
    const auto image = LoadImage(GLYPH_BAKE_LATO_IMAGE);
    ASSERT_NE(image, nullptr);
    const std::uint32_t glyphId = OutlinedGlyphs(*image, 1).front();
    const auto face = FaceOf(image);
    const std::vector<std::uint32_t> ids{glyphId};
    EXPECT_EQ(face->Select(0.0f, ids), image.get());
    EXPECT_EQ(face->Select(0.03f, ids), image.get());
    EXPECT_EQ(face->Select(0.031f, ids), nullptr);
    EXPECT_EQ(face->WidestBandEm(), 0.03f);
    // A glyph without an outline has no block to place.
    const std::vector<std::uint32_t> missing{image->GlyphCount()};
    EXPECT_EQ(face->Select(0.0f, missing), nullptr);

    std::vector<GlyphBakedRow> rows{{face, 1.0f, {{glyphId, 0.0f, 0.0f}}}};
    std::vector<std::uint32_t> stream;
    std::string error;
    const std::vector<float> narrow{0.02f}, wide{0.05f};
    EXPECT_TRUE(BuildGlyphBakeStream(rows, narrow, stream, &error)) << error;
    EXPECT_FALSE(BuildGlyphBakeStream(rows, wide, stream, &error));
    EXPECT_TRUE(stream.empty());
    EXPECT_NE(error.find("needs an exact band of 0.05"), std::string::npos) << error;
    EXPECT_NE(error.find("--exact-band-em"), std::string::npos) << error;
}

} // namespace
