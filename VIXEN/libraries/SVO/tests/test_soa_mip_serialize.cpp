// test_soa_mip_serialize.cpp — Sparse-Mip ESVO LOD Inc1, M1 Task 3.
//
// Mirrors test_soa_sdf_serialize.cpp: round-trip a baked octree's MipPool
// through SerializedOctree::mipPool and ConcatenatedOctrees::mipPool, assert
// mipPoolBase advances correctly across multiple octrees, and assert the
// bytes at the expected ordinal offset decode back to the exact bake-time
// MipSample values.

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#undef far
#undef near
#undef min
#undef max

#include "SdfBake.h"
#include "ShellOctreeGpu.h"
#include "SdfRecipes.h"
#include "MipBake.h"

using namespace Vixen::SVO;

namespace {

struct SdfFixture {
    SdfBodyOctree body;
    int n = 16;
    float r = 6.0f;
    glm::vec3 center{8.0f, 8.0f, 8.0f};

    SdfFixture() {
        RecipeParams rp{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, 2.0f);
        body = BuildSdfBodyOctree(baked, 3);
    }
};

MipSample DecodeMipSampleAt(const std::vector<uint8_t>& pool, size_t index) {
    MipSample out{};
    const size_t byteOff = index * sizeof(MipSample);
    if (byteOff + sizeof(MipSample) > pool.size()) return out;
    std::memcpy(&out, pool.data() + byteOff, sizeof(MipSample));
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Single-octree: SerializedOctree::mipPool round-trips BakeMipPool's samples.
// ---------------------------------------------------------------------------
TEST(SoaMipSerialize, MipPoolByteSizeMatchesNodeCountTimesChannelCount) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    const Octree* oct = f.body.octree->getOctree();
    ASSERT_NE(oct, nullptr);

    BakeAndAttachMipPool(*oct, out);

    ASSERT_FALSE(out.mipPool.empty());
    EXPECT_EQ(out.mipPool.size(),
              static_cast<size_t>(out.nodeCount) * out.channelCount * sizeof(MipSample))
        << "mipPool byte size must equal nodeCount * channelCount * sizeof(MipSample)";
}

TEST(SoaMipSerialize, MipPoolBytesMatchBakeTimeSamplesAtExpectedOrdinal) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    const Octree* oct = f.body.octree->getOctree();
    ASSERT_NE(oct, nullptr);

    // Compute the MipPool independently (same call BakeAndAttachMipPool makes
    // internally) so we have the exact expected samples to compare against.
    MipPool expectedPool = BakeMipPool(*oct, out);
    BakeAndAttachMipPool(*oct, out);

    ASSERT_EQ(out.mipPool.size(), expectedPool.samples.size() * sizeof(MipSample));

    // Spot-check every (node, channel) ordinal: bytes at that offset must
    // decode to exactly the same MipSample the bake computed.
    for (uint32_t nodeIdx = 0; nodeIdx < out.nodeCount; ++nodeIdx) {
        for (uint32_t ch = 0; ch < out.channelCount; ++ch) {
            const size_t ordinal = static_cast<size_t>(nodeIdx) * out.channelCount + ch;
            MipSample decoded = DecodeMipSampleAt(out.mipPool, ordinal);
            MipSample expected = expectedPool.Get(nodeIdx, ch);

            EXPECT_NEAR(decoded.value, expected.value, 1e-6f)
                << "node " << nodeIdx << " channel " << ch << " value mismatch";
            EXPECT_NEAR(decoded.coverage, expected.coverage, 1e-6f)
                << "node " << nodeIdx << " channel " << ch << " coverage mismatch";
        }
    }
}

// Root node's SDF mip sample must have non-zero coverage (matches
// test_mip_sample_bake.cpp's independent check, verified here via the
// serialized byte form specifically — proves the byte round-trip, not just
// the in-memory MipPool).
TEST(SoaMipSerialize, RootSdfByteDecodeHasCoverage) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    const Octree* oct = f.body.octree->getOctree();
    BakeAndAttachMipPool(*oct, out);

    ASSERT_EQ(out.channels[0].semanticId, static_cast<uint32_t>(SEM_SDF));
    MipSample rootSdf = DecodeMipSampleAt(out.mipPool, /*nodeIdx=*/0 * out.channelCount + 0);
    EXPECT_GT(rootSdf.coverage, 0.0f);
}

// ---------------------------------------------------------------------------
// ConcatenateSdf (unchanged, no-regression) — plain ConcatenateSdf does NOT
// bake mip pools (mip-baking is opt-in via ConcatenateSdfWithMips below, so
// ConcatenateSdf's existing non-mip callers are unaffected by Task 3).
// ---------------------------------------------------------------------------
TEST(SoaMipSerialize, PlainConcatenateSdfLeavesMipPoolEmpty) {
    SdfFixture f;
    const SdfBodyOctree* bodies[2] = {&f.body, &f.body};
    std::vector<const SdfBodyOctree*> vec(bodies, bodies + 2);

    ConcatenatedOctrees cat = ConcatenateSdf(vec);
    ASSERT_EQ(cat.count, 2u);

    EXPECT_EQ(mipPoolBaseOf(cat.configs[0]), 0u)
        << "mipPoolBase defaults to 0 when mip-baking was never invoked";
    EXPECT_EQ(mipPoolBaseOf(cat.configs[1]), 0u);
    EXPECT_TRUE(cat.mipPool.empty())
        << "concatenated mipPool stays empty when ConcatenateSdf (not ConcatenateSdfWithMips) is used";
}

// ---------------------------------------------------------------------------
// ConcatenateSdfWithMips — mipPoolBase advances correctly for >1 octree,
// mirroring the existing ConcatenateSdfBrickArrayBase test for poolBrickBase.
// ---------------------------------------------------------------------------
TEST(SoaMipSerialize, ConcatenateSdfWithMipsMipPoolBaseAdvances) {
    SdfFixture f;
    // Use the same body twice (mirrors ConcatenateSdfBrickArrayBase's pattern).
    const SdfBodyOctree* bodies[2] = {&f.body, &f.body};
    std::vector<const SdfBodyOctree*> vec(bodies, bodies + 2);

    ConcatenatedOctrees cat = ConcatenateSdfWithMips(vec);
    ASSERT_EQ(cat.count, 2u);

    EXPECT_EQ(mipPoolBaseOf(cat.configs[0]), 0u)
        << "first octree's mipPoolBase must be 0";

    // configs[1].mipPoolBase must be nodeCounts[0] * channelCount[0] (MipSample units).
    const uint32_t channelCount0 = cat.configs[0].channelCount;
    const uint32_t expectedSecondBase = cat.nodeCounts[0] * channelCount0;
    EXPECT_EQ(mipPoolBaseOf(cat.configs[1]), expectedSecondBase)
        << "second octree's mipPoolBase must be nodeCount[0] * channelCount[0]";

    // Concatenated mipPool byte size must equal the sum of both octrees'
    // (nodeCount * channelCount) * sizeof(MipSample).
    const uint32_t totalUnits = (cat.nodeCounts[0] + cat.nodeCounts[1]) * channelCount0;
    EXPECT_EQ(cat.mipPool.size(), static_cast<size_t>(totalUnits) * sizeof(MipSample));

    ASSERT_FALSE(cat.mipPool.empty());

    // The second octree's mip pool bytes (starting at mipPoolBase, in
    // MipSample units) must match its own standalone bake exactly (identical
    // bodies -> identical mip samples).
    SerializedOctree standalone = SerializeSdf(f.body);
    const Octree* oct = f.body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    BakeAndAttachMipPool(*oct, standalone);
    ASSERT_EQ(standalone.mipPool.size(), cat.mipPool.size() - expectedSecondBase * sizeof(MipSample));

    const size_t secondOctreeByteOffset = static_cast<size_t>(expectedSecondBase) * sizeof(MipSample);
    for (size_t i = 0; i < standalone.mipPool.size(); ++i) {
        ASSERT_EQ(cat.mipPool[secondOctreeByteOffset + i], standalone.mipPool[i])
            << "byte " << i << " of second octree's mip pool must match a standalone bake";
    }
}

// ---------------------------------------------------------------------------
// OctreeConfig tail: mipPoolBase lives at byte 352 (verified via
// codegraph_explore against the generated OctreeConfig.g.h — NOT the plan
// doc's guessed ">= 200" range), sizeof(OctreeConfig) stays 432.
// ---------------------------------------------------------------------------
TEST(SoaMipSerialize, OctreeConfigMipPoolBaseFieldOffsetAndStructSize) {
    EXPECT_EQ(sizeof(OctreeConfig), 432u);
    EXPECT_EQ(offsetof(OctreeConfig, mipPoolBase), 352u)
        << "mipPoolBase must occupy the first 4 bytes of the previously-unused "
        << "tail pad range (352..432), confirmed via the generated OctreeConfig.g.h "
        << "static_assert battery, not guessed";

    OctreeConfig c{};
    setMipPoolBase(c, 12345u);
    EXPECT_EQ(mipPoolBaseOf(c), 12345u);
}
