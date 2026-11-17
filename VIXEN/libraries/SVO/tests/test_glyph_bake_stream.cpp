// Baked glyph rows (R892.14): the image reader, the packed stream and the CPU face of the walk.
//
// The fonts are baked by the build (target vixen_glyph_bake, kernel tool glyph-bake). Beside each
// image the tool writes samples: points and the value its own walk returned for them. The C++
// face must return the same bits for every one of them.

#include "Recipe/GlyphBakeBand.h"
#include "Recipe/GlyphBakeCache.h"
#include "Recipe/GlyphBakeStream.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeServices.h"
#include "Recipe/SdfRecipeEval.h"

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

SdfInstruction Op(SdfOpCode op, float d0 = 0.0f) {
    SdfInstruction in{};
    in.opCode = static_cast<std::uint8_t>(op);
    in.data[0] = d0;
    return in;
}

// World -> row transform as the text scenes write it: positions scaled by 1 / rowScale, the
// distance scaled back by rowScale at RestorePos.
SdfInstruction RowTransform(float rowScale) {
    SdfInstruction in = Op(SdfOpCode::Transform);
    in.data[7] = 1.0f;
    in.data[8] = in.data[9] = in.data[10] = 1.0f / rowScale;
    in.data[11] = rowScale;
    return in;
}

SdfInstruction GlyphRow(std::uint32_t row) { return Op(SdfOpCode::GlyphOutline, std::bit_cast<float>(row)); }

TEST(GlyphBakeBand, TheNeedIsTheLargestOffsetInRowUnits) {
    // Inside the transform the glyph is rounded by 12 row units; outside, by 0.5 world units,
    // which is 0.5 / 0.004 row units.
    SdfInstruction box = Op(SdfOpCode::Box, 100.0f);
    box.data[1] = box.data[2] = 100.0f;
    const std::vector<SdfInstruction> program{
        RowTransform(0.004f), GlyphRow(3), box, Op(SdfOpCode::Intersect), Op(SdfOpCode::Round, 12.0f),
        Op(SdfOpCode::RestorePos), Op(SdfOpCode::Round, 0.5f),
        GlyphRow(5), Op(SdfOpCode::Union)};
    const std::vector<GlyphBandNeed> needs = DeriveGlyphBandNeeds(program);
    ASSERT_EQ(needs.size(), 2u);
    EXPECT_EQ(needs[0].row, 3u);
    EXPECT_EQ(needs[0].instructionIndex, 1u);
    EXPECT_NEAR(needs[0].offsetRowUnits, 12.0f + 0.5f / 0.004f, 1e-3f);
    EXPECT_EQ(needs[1].row, 5u);
    EXPECT_EQ(needs[1].offsetRowUnits, 0.0f);
}

TEST(GlyphBakeBand, AnOnionAndASmoothUnionAddTheirWidth) {
    SdfInstruction smooth = Op(SdfOpCode::SmoothUnion);
    smooth.data[2] = 7.0f;
    const std::vector<SdfInstruction> program{
        GlyphRow(0), Op(SdfOpCode::Onion, 2.0f), Op(SdfOpCode::Sphere, 1.0f), smooth};
    const std::vector<GlyphBandNeed> needs = DeriveGlyphBandNeeds(program);
    ASSERT_EQ(needs.size(), 1u);
    EXPECT_EQ(needs[0].offsetRowUnits, 9.0f);
}

TEST(GlyphBakeBand, AnOperationWithNoKnownBoundNeedsAnExactBake) {
    const std::vector<SdfInstruction> program{GlyphRow(0), Op(SdfOpCode::MathSin)};
    const std::vector<GlyphBandNeed> needs = DeriveGlyphBandNeeds(program);
    ASSERT_EQ(needs.size(), 1u);
    EXPECT_TRUE(std::isinf(needs[0].offsetRowUnits));
}

struct RegisteredGlyph {
    Vixen::SVO::RecipeRegistry registry;
    Vixen::SVO::RecipeRegistry::RegisterResult result{};
    std::shared_ptr<const GlyphBakeImage> image;
    std::uint32_t glyphId = 0;
    float fontPerRow = 2000.0f / 1536.0f;
};

// A recipe of one Lato glyph rounded by `roundRowUnits`, with or without its baked row.
RegisteredGlyph RegisterRoundedGlyph(float roundRowUnits, bool baked) {
    RegisteredGlyph out;
    out.image = LoadImage(GLYPH_BAKE_LATO_IMAGE);
    if (!out.image) return out;
    out.glyphId = OutlinedGlyphs(*out.image, 40).back();
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    entry.bytecode = {RowTransform(0.004f), GlyphRow(0), Op(SdfOpCode::Round, roundRowUnits), Op(SdfOpCode::RestorePos)};
    entry.glyphInstances.emplace_back();
    entry.glyphInstances.back().SegmentCount = 1; // a row with an outline
    if (baked) entry.glyphBakedRows.push_back({FaceOf(out.image), out.fontPerRow, {{out.glyphId, 0.0f, 0.0f}}});
    out.result = out.registry.Register(7u, entry);
    return out;
}

TEST(GlyphBakeRegistry, TheCpuEvaluatesTheBakedRowTheGpuWalks) {
    RegisteredGlyph glyph = RegisterRoundedGlyph(4.0f, true);
    ASSERT_NE(glyph.image, nullptr);
    ASSERT_EQ(glyph.result, Vixen::SVO::RecipeRegistry::RegisterResult::Ok) << glyph.registry.GlyphDiagnostic();
    EXPECT_TRUE(glyph.registry.UnbakedGlyphRows().empty());
    std::vector<std::uint32_t> stream;
    std::string error;
    ASSERT_TRUE(glyph.registry.BuildGlyphBakeStream(stream, &error)) << error;
    ASSERT_TRUE(GlyphBakeStreamHasRow(stream, 0));
    const auto* entry = glyph.registry.Get(7u);
    ASSERT_NE(entry, nullptr);
    float minX, minY, maxX, maxY;
    ASSERT_TRUE(glyph.registry.GlyphBakedBlob()[0].Bounds(*glyph.image, minX, minY, maxX, maxY));
    std::uint32_t state = 99u, inside = 0;
    for (int sample = 0; sample < 5000; ++sample) {
        auto next = [&state]() { state = state * 1664525u + 1013904223u; return static_cast<float>(state >> 8) / 16777216.0f; };
        // World units: the row is 0.004 world units per row unit.
        const glm::vec3 p((minX - 60.0f + next() * (maxX - minX + 120.0f)) * 0.004f,
                          (minY - 60.0f + next() * (maxY - minY + 120.0f)) * 0.004f, 0.0f);
        const float value = evalRecipe(entry->bytecode.data(), static_cast<std::uint32_t>(entry->bytecode.size()), p, {},
                                       nullptr, &glyph.registry, glyph.registry.GlyphOutlineBlob(), {}, nullptr, 0u, nullptr, 0u, stream);
        const glm::vec3 rowPoint = p * (1.0f / 0.004f);
        const float expected = (EvaluateGlyphBakeRow(stream, 0, rowPoint.x, rowPoint.y) - 4.0f) * 0.004f;
        ASSERT_NEAR(value, expected, 1e-6f) << p.x << ", " << p.y;
        inside += value < 0.0f ? 1u : 0u;
    }
    EXPECT_GT(inside, 100u);
}

TEST(GlyphBakeRegistry, ARecipeThatOffsetsPastEveryBandIsRefusedWithTheFix) {
    // 0.03 em at 24 px is 46 row units; a 60-unit round needs more than any bake here has.
    RegisteredGlyph glyph = RegisterRoundedGlyph(60.0f, true);
    ASSERT_NE(glyph.image, nullptr);
    EXPECT_EQ(glyph.result, Vixen::SVO::RecipeRegistry::RegisterResult::GlyphBandUnavailable);
    const std::string& diagnostic = glyph.registry.GlyphDiagnostic();
    EXPECT_NE(diagnostic.find("recipe 7"), std::string::npos) << diagnostic;
    EXPECT_NE(diagnostic.find("glyph row 0"), std::string::npos) << diagnostic;
    EXPECT_NE(diagnostic.find("VIXEN_GLYPH_BAKE_BANDS_EM"), std::string::npos) << diagnostic;
    EXPECT_EQ(glyph.registry.Get(7u), nullptr);
    // 30 row units is 0.0195 em, and the margin keeps it inside 0.03.
    EXPECT_EQ(RegisterRoundedGlyph(30.0f, true).result, Vixen::SVO::RecipeRegistry::RegisterResult::Ok);
}

TEST(GlyphBakeRegistry, ARowWithAnOutlineAndNoBakedFormIsReported) {
    RegisteredGlyph glyph = RegisterRoundedGlyph(4.0f, false);
    ASSERT_EQ(glyph.result, Vixen::SVO::RecipeRegistry::RegisterResult::Ok);
    EXPECT_EQ(glyph.registry.UnbakedGlyphRows(), std::vector<std::uint32_t>{0u});
    std::vector<std::uint32_t> stream;
    ASSERT_TRUE(glyph.registry.BuildGlyphBakeStream(stream));
    EXPECT_FALSE(GlyphBakeStreamHasRow(stream, 0));
}

// ---------------------------------------------------------------------------------------------
// The image in the shared cache (R892.19): found by content key, imported on a miss.
// ---------------------------------------------------------------------------------------------

std::filesystem::path FreshCacheDirectory(const char* name) {
    const std::filesystem::path directory = std::filesystem::path(::testing::TempDir()) / "vixen-glyph-bake-cache" / name;
    std::filesystem::remove_all(directory);
    return directory;
}

GlyphBakeManifestEntry ManifestEntryOf(const char* face) {
    std::vector<GlyphBakeManifestEntry> entries;
    std::string error;
    EXPECT_TRUE(ReadGlyphBakeManifest(GLYPH_BAKE_MANIFEST, entries, &error)) << error;
    for (const GlyphBakeManifestEntry& entry : entries)
        if (entry.face == face) return entry;
    ADD_FAILURE() << "the manifest lists no bake of " << face;
    return {};
}

TEST(GlyphBakeCache, TheManifestNamesTheKeyOfEveryImageTheBuildWrote) {
    std::vector<GlyphBakeManifestEntry> entries;
    std::string error;
    ASSERT_TRUE(ReadGlyphBakeManifest(GLYPH_BAKE_MANIFEST, entries, &error)) << error;
    ASSERT_GE(entries.size(), 2u);
    bool lato = false, noto = false;
    for (const GlyphBakeManifestEntry& entry : entries) {
        const auto image = GlyphBakeImage::Load(entry.image, &error);
        ASSERT_NE(image, nullptr) << error;
        EXPECT_EQ(GlyphBakeKeyText(*image), entry.key) << entry.image;
        EXPECT_TRUE(std::filesystem::exists(entry.pack)) << entry.pack;
        lato = lato || entry.face == "LatoLatin-Regular";
        noto = noto || entry.face == "NotoSansArabic-VF";
    }
    EXPECT_TRUE(lato && noto);
}

TEST(GlyphBakeCache, AMissImportsTheImageAndAHitDoesNotReadTheImportFile) {
    GlyphBakeManifestEntry entry = ManifestEntryOf("LatoLatin-Regular");
    ASSERT_FALSE(entry.key.empty());
    const std::filesystem::path cache = FreshCacheDirectory("miss-then-hit");
    std::string error;
    GlyphBakeCacheSource source{};
    const auto imported = LoadCachedGlyphBakeImage(entry, cache, &error, &source);
    ASSERT_NE(imported, nullptr) << error;
    EXPECT_EQ(source, GlyphBakeCacheSource::Imported);
    const std::filesystem::path cached = cache / (entry.key + ".glyphimage");
    ASSERT_TRUE(std::filesystem::exists(cached));
    EXPECT_EQ(std::filesystem::file_size(cached), std::filesystem::file_size(entry.image));
    // Nothing but the published image is left in the cache directory.
    std::size_t files = 0;
    for (const auto& item : std::filesystem::directory_iterator(cache)) { (void)item; ++files; }
    EXPECT_EQ(files, 1u);

    entry.image = cache / "no-such-import-file.glyphimage";
    const auto hit = LoadCachedGlyphBakeImage(entry, cache, &error, &source);
    ASSERT_NE(hit, nullptr) << error;
    EXPECT_EQ(source, GlyphBakeCacheSource::Cache);
    EXPECT_EQ(GlyphBakeKeyText(*hit), entry.key);
    EXPECT_EQ(hit->GlyphCount(), imported->GlyphCount());
}

TEST(GlyphBakeCache, ACachedFileThatIsNotTheKeysImageIsReplaced) {
    const GlyphBakeManifestEntry entry = ManifestEntryOf("LatoLatin-Regular");
    const GlyphBakeManifestEntry other = ManifestEntryOf("NotoSansArabic-VF");
    ASSERT_FALSE(entry.key.empty());
    ASSERT_FALSE(other.key.empty());
    const std::filesystem::path cache = FreshCacheDirectory("replaced");
    std::filesystem::create_directories(cache);
    const std::filesystem::path cached = cache / (entry.key + ".glyphimage");
    std::string error;
    GlyphBakeCacheSource source{};

    { std::ofstream truncated(cached, std::ios::binary); truncated << "cut short"; }
    ASSERT_NE(LoadCachedGlyphBakeImage(entry, cache, &error, &source), nullptr) << error;
    EXPECT_EQ(source, GlyphBakeCacheSource::Imported);
    EXPECT_EQ(std::filesystem::file_size(cached), std::filesystem::file_size(entry.image));

    // A sound image of another font under this key.
    std::filesystem::copy_file(other.image, cached, std::filesystem::copy_options::overwrite_existing);
    const auto image = LoadCachedGlyphBakeImage(entry, cache, &error, &source);
    ASSERT_NE(image, nullptr) << error;
    EXPECT_EQ(source, GlyphBakeCacheSource::Imported);
    EXPECT_EQ(GlyphBakeKeyText(*image), entry.key);
    ASSERT_NE(LoadCachedGlyphBakeImage(entry, cache, &error, &source), nullptr) << error;
    EXPECT_EQ(source, GlyphBakeCacheSource::Cache);
}

TEST(GlyphBakeCache, AnImportImageOfAnotherKeyIsRefusedWithTheFix) {
    GlyphBakeManifestEntry entry = ManifestEntryOf("LatoLatin-Regular");
    const GlyphBakeManifestEntry other = ManifestEntryOf("NotoSansArabic-VF");
    ASSERT_FALSE(entry.key.empty());
    entry.image = other.image;
    const std::filesystem::path cache = FreshCacheDirectory("refused");
    std::string error;
    EXPECT_EQ(LoadCachedGlyphBakeImage(entry, cache, &error), nullptr);
    EXPECT_NE(error.find("manifest names key"), std::string::npos) << error;
    EXPECT_NE(error.find("vixen_glyph_bake"), std::string::npos) << error;
    EXPECT_FALSE(std::filesystem::exists(cache / (entry.key + ".glyphimage")));

    entry.image = cache / "no-such-import-file.glyphimage";
    EXPECT_EQ(LoadCachedGlyphBakeImage(entry, cache, &error), nullptr);
    EXPECT_NE(error.find("cannot be imported"), std::string::npos) << error;
}

TEST(GlyphBakeCache, AFaceLoadsEveryBandOfTheManifestAndAnUnknownFaceFails) {
    const std::filesystem::path cache = FreshCacheDirectory("face");
    std::string error;
    const auto face = LoadGlyphBakeFace("LatoLatin-Regular", GLYPH_BAKE_MANIFEST, cache, &error);
    ASSERT_NE(face, nullptr) << error;
    std::vector<GlyphBakeManifestEntry> entries;
    ASSERT_TRUE(ReadGlyphBakeManifest(GLYPH_BAKE_MANIFEST, entries, &error)) << error;
    std::size_t bands = 0;
    for (const GlyphBakeManifestEntry& entry : entries) bands += entry.face == "LatoLatin-Regular" ? 1u : 0u;
    EXPECT_EQ(face->images.size(), bands);
    EXPECT_EQ(LoadGlyphBakeFace("NoSuchFace", GLYPH_BAKE_MANIFEST, cache, &error), nullptr);
    EXPECT_NE(error.find("lists no bake of face NoSuchFace"), std::string::npos) << error;
    EXPECT_EQ(LoadGlyphBakeFace("LatoLatin-Regular", cache / "no-manifest.tsv", cache, &error), nullptr);
    EXPECT_NE(error.find("cannot open glyph bake manifest"), std::string::npos) << error;
}

// ---------------------------------------------------------------------------------------------
// Glyph facts and the shell without glyph rows (R892.21, R892.23).
// ---------------------------------------------------------------------------------------------

TEST(GlyphBakeFacts, EveryOutlinedGlyphHasAnInsideDistanceAboveTheWalkAndACoverage) {
    for (const BakedFont& font : kFonts) {
        const auto image = LoadImage(font.image);
        ASSERT_NE(image, nullptr);
        std::uint32_t outlined = 0;
        for (std::uint32_t glyphId = 0; glyphId < image->GlyphCount(); ++glyphId) {
            const auto block = image->Block(glyphId);
            const float inside = image->InsideDistanceUnits(glyphId), coverage = image->InkCoverage(glyphId);
            if (block.empty()) {
                EXPECT_EQ(inside, 0.0f) << font.name << " glyph " << glyphId;
                continue;
            }
            ++outlined;
            EXPECT_GT(inside, 0.0f) << font.name << " glyph " << glyphId;
            EXPECT_GT(coverage, 0.0f) << font.name << " glyph " << glyphId;
            EXPECT_LE(coverage, 1.0f) << font.name << " glyph " << glyphId;
            // The walk never returns a value below minus the stored bound, anywhere in the bounds.
            const float minX = std::bit_cast<float>(block[4]), minY = std::bit_cast<float>(block[5]);
            const float maxX = std::bit_cast<float>(block[6]), maxY = std::bit_cast<float>(block[7]);
            if (glyphId % 7u != 0u) continue; // every seventh glyph is sampled
            std::uint32_t state = 4242u + glyphId;
            auto next = [&state]() { state = state * 1664525u + 1013904223u; return static_cast<float>(state >> 8) / 16777216.0f; };
            for (int sample = 0; sample < 400; ++sample) {
                const float value = EvaluateGlyphBakeBlock(block.data(), minX + next() * (maxX - minX), minY + next() * (maxY - minY));
                ASSERT_GE(value, -inside) << font.name << " glyph " << glyphId;
            }
        }
        EXPECT_GT(outlined, 100u) << font.name;
    }
}

// The sign scenes' shape: a plaque, and one glyph row raised out of it (Union) or cut into it
// (Subtract) by `depth` world units around the plaque's front face.
struct SignRecipe {
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    float rowScale = 0.004f;
    float plaqueHalfDepth = 0.5f;
};

SignRecipe MakeSign(const std::shared_ptr<const GlyphBakeImage>& image, std::uint32_t glyphId, bool carved, float depth) {
    SignRecipe sign;
    const float fontPerRow = 2000.0f / 1536.0f;
    float minX, minY, maxX, maxY;
    GlyphBakedRow row{FaceOf(image), fontPerRow, {{glyphId, 0.0f, 0.0f}}};
    EXPECT_TRUE(row.Bounds(*image, minX, minY, maxX, maxY));
    SdfInstruction plaque = Op(SdfOpCode::BoxRounded, 8.0f);
    plaque.data[1] = 8.0f; plaque.data[2] = sign.plaqueHalfDepth; plaque.data[3] = 0.05f;
    SdfInstruction transform = RowTransform(sign.rowScale);
    transform.data[0] = -0.5f * (minX + maxX) * sign.rowScale;
    transform.data[1] = -0.5f * (minY + maxY) * sign.rowScale;
    transform.data[2] = sign.plaqueHalfDepth;
    SdfInstruction boxCentre = RowTransform(1.0f);
    boxCentre.data[0] = 0.5f * (minX + maxX);
    boxCentre.data[1] = 0.5f * (minY + maxY);
    SdfInstruction box = Op(SdfOpCode::Box, 0.5f * (maxX - minX) + 30.0f);
    box.data[1] = 0.5f * (maxY - minY) + 30.0f;
    box.data[2] = depth / sign.rowScale;
    sign.entry.bytecode = {plaque, transform, GlyphRow(0), boxCentre, box, Op(SdfOpCode::Intersect),
        Op(SdfOpCode::Round, 0.02f / sign.rowScale), Op(SdfOpCode::RestorePos), Op(SdfOpCode::RestorePos),
        Op(carved ? SdfOpCode::Subtract : SdfOpCode::Union)};
    sign.entry.glyphInstances.emplace_back();
    sign.entry.glyphInstances.back().SegmentCount = 1;
    sign.entry.glyphBakedRows.push_back(std::move(row));
    sign.entry.boundRadius = 14.0f;
    return sign;
}

TEST(GlyphBakeShell, RegistrationStampsTheInsideDistanceOfEveryBakedRow) {
    const auto image = LoadImage(GLYPH_BAKE_LATO_IMAGE);
    ASSERT_NE(image, nullptr);
    const std::uint32_t glyphId = OutlinedGlyphs(*image, 40).back();
    SignRecipe sign = MakeSign(image, glyphId, false, 0.3f);
    EXPECT_EQ(StampGlyphInsideDistances(sign.entry), 1u);
    const float expected = image->InsideDistanceUnits(glyphId) / (2000.0f / 1536.0f);
    EXPECT_GE(sign.entry.bytecode[2].data[1], expected);
    EXPECT_LE(sign.entry.bytecode[2].data[1], expected * 1.0001f);
    // A row without a baked form keeps 0: its leaf never vanishes.
    sign.entry.glyphBakedRows.clear();
    EXPECT_EQ(StampGlyphInsideDistances(sign.entry), 0u);
    EXPECT_EQ(sign.entry.bytecode[2].data[1], 0.0f);
}

class GlyphBakeShellSign : public ::testing::TestWithParam<bool> {};

TEST_P(GlyphBakeShellSign, TheCoarsestShellHasNoGlyphRowAndStaysWithinItsCertifiedError) {
    const bool carved = GetParam();
    if (!GetRecipeServiceApis().available) GTEST_SKIP() << "the kernel recipe services are not available";
    const auto image = LoadImage(GLYPH_BAKE_LATO_IMAGE);
    ASSERT_NE(image, nullptr);
    const std::uint32_t glyphId = OutlinedGlyphs(*image, 40).back();
    const float depth = 0.3f;
    SignRecipe sign = MakeSign(image, glyphId, carved, depth);
    Vixen::SVO::RecipeRegistry::RecipeEntry full = sign.entry;
    RecipeRegistrationOptions options;
    options.preferDerivedBound = true;
    options.shells = true;
    const RecipeRegistrationAnalysis analysis = AnalyzeRecipeRegistration(full, 14.0f, 0.9f, 64u, 16u, options);
    ASSERT_EQ(analysis.status, static_cast<std::uint32_t>(YK_RECIPE_ANALYSIS_OK)) << analysis.diagnostic;
    ASSERT_FALSE(full.derivedShells.empty()) << "a baked glyph row must leave a shell";
    const auto& shell = full.derivedShells.back();
    for (const SdfInstruction& in : shell.bytecode)
        EXPECT_NE(static_cast<SdfOpCode>(in.opCode), SdfOpCode::GlyphOutline);
    EXPECT_EQ(shell.bytecode.size(), 1u) << "the plaque alone";
    EXPECT_NE(shell.rules & YK_RECIPE_ANALYSIS_RULE_DROP_VANISHING_ROW, 0u);
    EXPECT_EQ(shell.certificate, static_cast<std::uint32_t>(YK_RECIPE_ANALYSIS_CERTIFICATE_RULE_BOUNDS));
    // The error is the depth of the row in the plaque (plus the round and the search margin).
    EXPECT_GE(shell.certifiedError, depth);
    EXPECT_LE(shell.certifiedError, (depth + 0.02f) * 1.25f);
    std::printf("[GLYPH_SHELL] %s depth=%.4f certified_error=%.6f bound_radius=%.4f records=%zu of %zu\n",
                carved ? "carved" : "embossed", depth, shell.certifiedError, full.derivedShellBoundRadius,
                shell.bytecode.size(), full.bytecode.size());

    // Where the recipe and the shell disagree about inside and outside, the shell's value is
    // within the certified error: the glyph evaluated here, on the CPU face of the walk.
    Vixen::SVO::RecipeRegistry registry;
    ASSERT_EQ(registry.Register(7u, full), Vixen::SVO::RecipeRegistry::RegisterResult::Ok) << registry.GlyphDiagnostic();
    std::vector<std::uint32_t> stream;
    std::string error;
    ASSERT_TRUE(registry.BuildGlyphBakeStream(stream, &error)) << error;
    const auto* entry = registry.Get(7u);
    ASSERT_NE(entry, nullptr);
    std::uint32_t state = 1357u, disagree = 0;
    float worst = 0.0f;
    auto next = [&state]() { state = state * 1664525u + 1013904223u; return static_cast<float>(state >> 8) / 16777216.0f; };
    for (int sample = 0; sample < 200000; ++sample) {
        const glm::vec3 p(-4.0f + 8.0f * next(), -4.0f + 8.0f * next(), sign.plaqueHalfDepth - 0.45f + 0.9f * next());
        const float recipe = evalRecipe(entry->bytecode.data(), static_cast<std::uint32_t>(entry->bytecode.size()), p, {},
                                        nullptr, &registry, registry.GlyphOutlineBlob(), {}, nullptr, 0u, nullptr, 0u, stream);
        const float simple = evalRecipe(shell.bytecode.data(), static_cast<std::uint32_t>(shell.bytecode.size()), p);
        if ((recipe <= 0.0f) == (simple <= 0.0f)) continue;
        ++disagree;
        worst = std::max(worst, std::fabs(simple));
        ASSERT_LE(std::fabs(simple), shell.certifiedError) << "at (" << p.x << ", " << p.y << ", " << p.z << ")";
    }
    EXPECT_GT(disagree, 200u) << "the glyph must be sampled";
    EXPECT_GT(worst, 0.5f * shell.certifiedError) << "the certified error is not vacuous";
    std::printf("[GLYPH_SHELL] %s disagreeing_samples=%u worst_shell_value=%.6f\n", carved ? "carved" : "embossed", disagree, worst);
}

INSTANTIATE_TEST_SUITE_P(EmbossedAndCarved, GlyphBakeShellSign, ::testing::Bool());

} // namespace
