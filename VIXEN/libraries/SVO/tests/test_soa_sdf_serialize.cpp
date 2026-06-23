// test_soa_sdf_serialize.cpp — Inc2/Inc3 M2: SoA-SDF pool serialization.
//
// Covers Tasks 3 and 4 (Inc2):
//   Task 3 — SerializeSdf emits a SDF channel in channelPool of the right size;
//             a surface voxel's SoA SDF ≈ its baked Density; descriptor formatId == STORED_SDF.
//   Task 4 — brickGridLookup: every allocated brick's grid cell maps to its
//             brickView index; unallocated cells map to 0xFFFFFFFF.
//
// Inc3 M2 Tasks 3/4 additions:
//   MultiChannelPoolLayout — 3-channel pool (sdf+color+roughness), sizes+bases correct.
//   MultiChannelBakedColorRoughness — per-voxel color/roughness finite and in-range.

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>

// MSVC defines far/near/min/max as macros via <windows.h>.
#undef far
#undef near
#undef min
#undef max

#include "SdfBake.h"
#include "ShellOctreeGpu.h"
#include "SdfRecipes.h"

using namespace Vixen::SVO;

// ---------------------------------------------------------------------------
// Shared fixture: bake a sphere (r=6, n=16, band=2) + build the octree.
// Used by all tests in this file.
// ---------------------------------------------------------------------------
struct SdfFixture {
    SdfBodyOctree body;
    int n    = 16;
    float r  = 6.0f;
    glm::vec3 center{8.0f, 8.0f, 8.0f};

    SdfFixture() {
        RecipeParams rp{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, 2.0f);
        body = BuildSdfBodyOctree(baked, 3);
    }
};

// ---------------------------------------------------------------------------
// Task 3 — SoA-SDF channel in the generic pool
// ---------------------------------------------------------------------------

// The channelPool buffer must be exactly brickCount * brickStrideFloats * sizeof(float) bytes.
TEST(SoaSdfSerialize, BrickBufferSize) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);

    ASSERT_FALSE(out.channelPool.empty()) << "channelPool must be non-empty for a non-trivial bake";
    EXPECT_EQ(out.channelPool.size(),
              static_cast<size_t>(out.brickCount) * out.brickStrideFloats * sizeof(float))
        << "channelPool byte size must equal brickCount * brickStrideFloats * sizeof(float)";
}

// The SoA SDF for a known near-surface voxel should match its baked Density.
// We locate the surface-voxel voxel (14,8,8) in the correct brick+slot and
// compare against evalSdf (within a grid-cell tolerance).
TEST(SoaSdfSerialize, SurfaceVoxelHoldsSignedDistance) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    ASSERT_FALSE(out.channelPool.empty());

    const Vixen::SVO::Octree* oct = f.body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    ASSERT_NE(oct->root, nullptr);

    const auto& brickViews = oct->root->brickViews;
    ASSERT_FALSE(brickViews.empty());

    // Find the brick that contains grid voxel (14,8,8).
    const glm::vec3 targetPos(14.0f, 8.0f, 8.0f);
    bool found = false;
    for (uint32_t bi = 0; bi < static_cast<uint32_t>(brickViews.size()); ++bi) {
        const glm::ivec3 origin = brickViews[bi].getLocalGridOrigin();
        const glm::ivec3 local = glm::ivec3(targetPos) - origin;
        if (local.x < 0 || local.x >= 8 || local.y < 0 || local.y >= 8 ||
            local.z < 0 || local.z >= 8) {
            continue;
        }
        // Voxel slot within this brick: z*64 + y*8 + x
        const uint32_t slot = static_cast<uint32_t>(local.z * 64 + local.y * 8 + local.x);
        float storedSdf = out.readPoolVoxel(SEM_SDF, bi, slot, 0);

        RecipeParams rp{f.r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        const float expected = evalSdf(RECIPE_SPHERE, targetPos, f.center, rp);
        EXPECT_NEAR(storedSdf, expected, 0.6f)
            << "Stored SDF at (14,8,8) should ≈ evalSdf (within one grid-cell tolerance)";
        found = true;
        break;
    }
    EXPECT_TRUE(found) << "No brick contains target voxel (14,8,8)";
}

// The descriptor fields in the OctreeConfig tail must be set correctly.
TEST(SoaSdfSerialize, DescriptorFields) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);

    // formatId must be STORED_SDF (1u) — byte 200
    EXPECT_EQ(formatIdOf(out.config), STORED_SDF)
        << "OctreeConfig.formatId (byte 200) must be STORED_SDF = 1";

    // poolBrickBase must be 0 for a single-octree serialize — byte 208
    EXPECT_EQ(sdfBrickArrayBaseOf(out.config), 0u)
        << "Single-octree poolBrickBase must be 0";

    // sizeof(OctreeConfig) must still be 432 (static_assert already checks this,
    // but a runtime echo is a useful test signal).
    EXPECT_EQ(sizeof(OctreeConfig), 432u);
}

// The existing material bricks must also be present and have the right size.
TEST(SoaSdfSerialize, MaterialBricksUnchanged) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);

    EXPECT_EQ(out.bricks.size(),
              static_cast<size_t>(out.brickCount) * SerializedOctree::kBrickStrideBytes)
        << "material bricks byte size must equal brickCount * 512 * sizeof(uint32)";
}

// ---------------------------------------------------------------------------
// Task 4 — Dense grid→brick lookup table
// ---------------------------------------------------------------------------

// brickGridLookup must be non-empty.
TEST(SoaSdfSerialize, LookupNonEmpty) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);

    EXPECT_GT(out.brickGridLookup.size(), 0u)
        << "brickGridLookup must be non-empty for a non-trivial octree";
}

// Every allocated brick's grid cell must map to its brickView index;
// cells outside the surface must map to 0xFFFFFFFF.
TEST(SoaSdfSerialize, LookupAllocatedBricksRoundTrip) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    ASSERT_FALSE(out.brickGridLookup.empty());

    const Vixen::SVO::Octree* oct = f.body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    ASSERT_NE(oct->root, nullptr);

    const int bpa = oct->bricksPerAxis;
    const uint32_t tableSize = static_cast<uint32_t>(bpa * bpa * bpa);
    ASSERT_EQ(out.brickGridLookup.size(), tableSize * sizeof(uint32_t))
        << "lookup table must be bpa^3 uint32s";

    // Load the lookup table into a typed vector for easy indexing.
    std::vector<uint32_t> lookup(tableSize);
    std::memcpy(lookup.data(), out.brickGridLookup.data(), out.brickGridLookup.size());

    // For every entry in brickGridToBrickView, the corresponding flat cell must
    // hold that exact brickView index.
    const auto& gtb = oct->root->brickGridToBrickView;
    ASSERT_FALSE(gtb.empty()) << "octree must have at least one allocated brick";

    for (const auto& kv : gtb) {
        const uint32_t key  = kv.first;
        const uint32_t idx  = kv.second;
        const uint32_t gx   = (key)       & 0x3FFu;
        const uint32_t gy   = (key >> 10) & 0x3FFu;
        const uint32_t gz   = (key >> 20) & 0x3FFu;
        const uint32_t flat = gx
                            + gy * static_cast<uint32_t>(bpa)
                            + gz * static_cast<uint32_t>(bpa) * static_cast<uint32_t>(bpa);
        ASSERT_LT(flat, tableSize) << "brick grid coord out of expected range";
        EXPECT_EQ(lookup[flat], idx)
            << "lookup[" << flat << "] should be brickView index " << idx
            << " (brick grid " << gx << "," << gy << "," << gz << ")";
    }
}

// Every cell NOT in brickGridToBrickView must be 0xFFFFFFFF.
TEST(SoaSdfSerialize, LookupUnallocatedCellsSentinel) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    ASSERT_FALSE(out.brickGridLookup.empty());

    const Vixen::SVO::Octree* oct = f.body.octree->getOctree();
    const int bpa = oct->bricksPerAxis;
    const uint32_t tableSize = static_cast<uint32_t>(bpa * bpa * bpa);

    std::vector<uint32_t> lookup(tableSize);
    std::memcpy(lookup.data(), out.brickGridLookup.data(), out.brickGridLookup.size());

    // Build an expected set of allocated flat indices.
    std::vector<bool> allocated(tableSize, false);
    for (const auto& kv : oct->root->brickGridToBrickView) {
        const uint32_t key = kv.first;
        const uint32_t gx  = (key)       & 0x3FFu;
        const uint32_t gy  = (key >> 10) & 0x3FFu;
        const uint32_t gz  = (key >> 20) & 0x3FFu;
        const uint32_t flat = gx
                            + gy * static_cast<uint32_t>(bpa)
                            + gz * static_cast<uint32_t>(bpa) * static_cast<uint32_t>(bpa);
        if (flat < tableSize) allocated[flat] = true;
    }

    for (uint32_t i = 0; i < tableSize; ++i) {
        if (!allocated[i]) {
            EXPECT_EQ(lookup[i], 0xFFFFFFFFu)
                << "unallocated cell [" << i << "] must be sentinel 0xFFFFFFFF";
        }
    }
}

// ---------------------------------------------------------------------------
// ConcatenateSdf — poolBrickBase advances correctly for >1 octree.
// ---------------------------------------------------------------------------
TEST(SoaSdfSerialize, ConcatenateSdfBrickArrayBase) {
    SdfFixture f;
    // Use the same body twice (simulates two identical octrees).
    const SdfBodyOctree* bodies[2] = {&f.body, &f.body};
    std::vector<const SdfBodyOctree*> vec(bodies, bodies + 2);

    ConcatenatedOctrees cat = ConcatenateSdf(vec);
    ASSERT_EQ(cat.count, 2u);

    // configs[0].poolBrickBase must be 0.
    EXPECT_EQ(sdfBrickArrayBaseOf(cat.configs[0]), 0u)
        << "First octree poolBrickBase must be 0";

    // configs[1].poolBrickBase must be configs[0].brickCount * brickStrideFloats[0].
    // Since both are the same body, brickStrideFloats is equal across octrees.
    const uint32_t stride0  = cat.configs[0].brickStrideFloats;
    const uint32_t expectedBase = cat.brickCounts[0] * stride0;
    EXPECT_EQ(sdfBrickArrayBaseOf(cat.configs[1]), expectedBase)
        << "Second octree poolBrickBase must be brickCount[0] * brickStrideFloats";

    // channelPool total size must be (brickCounts[0]+brickCounts[1]) * brickStrideFloats * sizeof(float).
    const uint32_t totalBricks = cat.brickCounts[0] + cat.brickCounts[1];
    EXPECT_EQ(cat.channelPool.size(),
              static_cast<size_t>(totalBricks) * stride0 * sizeof(float));

    // Both configs must carry formatId = STORED_SDF.
    EXPECT_EQ(formatIdOf(cat.configs[0]), STORED_SDF);
    EXPECT_EQ(formatIdOf(cat.configs[1]), STORED_SDF);
}

// ---------------------------------------------------------------------------
// Inc3 M2 Task 3 — multi-channel SoA pool layout
// ---------------------------------------------------------------------------
TEST(SoaSdfSerialize, MultiChannelPoolLayout) {
    RecipeParams rp{6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    Vixen::SVO::SdfBakeResult baked =
        Vixen::SVO::BakeRecipeToSdfWorld(RECIPE_SPHERE, glm::vec3(32), rp, 64, 2.5f);
    Vixen::SVO::SdfBodyOctree body = Vixen::SVO::BuildSdfBodyOctree(baked, 3);
    Vixen::SVO::SerializedOctree out = Vixen::SVO::SerializeSdf(body);

    // 3 channels: sdf(1)+color(3)+roughness(1) = 5 float-lanes/voxel
    EXPECT_EQ(out.channelCount, 3u);
    EXPECT_EQ(out.brickStrideFloats, (1u + 3u + 1u) * 512u);   // 2560

    // channelBaseFloats: sdf 0, color 512, roughness 2048 (declaration order)
    EXPECT_EQ(out.channelBaseFloats(SEM_SDF),       0u);
    EXPECT_EQ(out.channelBaseFloats(SEM_COLOR),     512u);
    EXPECT_EQ(out.channelBaseFloats(SEM_ROUGHNESS), 2048u);

    // Pool byte size = brickCount * brickStrideFloats * sizeof(float)
    EXPECT_EQ(out.channelPool.size(),
              static_cast<size_t>(out.brickCount) * out.brickStrideFloats * sizeof(float));

    // A voxel in the SDF channel must hold a finite value
    float sdf = out.readPoolVoxel(SEM_SDF, 0, 0, 0);
    EXPECT_TRUE(std::isfinite(sdf));
}

// Inc3 M2 Task 4 — baked color+roughness values round-trip through the pool
TEST(SoaSdfSerialize, MultiChannelBakedColorRoughness) {
    RecipeParams rp{6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    Vixen::SVO::SdfBakeResult baked =
        Vixen::SVO::BakeRecipeToSdfWorld(RECIPE_SPHERE, glm::vec3(32), rp, 64, 2.5f);
    Vixen::SVO::SdfBodyOctree body = Vixen::SVO::BuildSdfBodyOctree(baked, 3);
    Vixen::SVO::SerializedOctree out = Vixen::SVO::SerializeSdf(body);

    ASSERT_EQ(out.channelCount, 3u);
    ASSERT_FALSE(out.channelPool.empty());

    // Check brick 0, voxel 0 — color and roughness must be finite and in [0,1]
    float r0    = out.readPoolVoxel(SEM_COLOR, 0, 0, 0);
    float g0    = out.readPoolVoxel(SEM_COLOR, 0, 0, 1);
    float b0    = out.readPoolVoxel(SEM_COLOR, 0, 0, 2);
    float rough0 = out.readPoolVoxel(SEM_ROUGHNESS, 0, 0, 0);

    EXPECT_TRUE(std::isfinite(r0));
    EXPECT_TRUE(std::isfinite(g0));
    EXPECT_TRUE(std::isfinite(b0));
    EXPECT_GE(r0, 0.0f);  EXPECT_LE(r0, 1.0f);
    EXPECT_GE(g0, 0.0f);  EXPECT_LE(g0, 1.0f);
    EXPECT_GE(b0, 0.0f);  EXPECT_LE(b0, 1.0f);
    EXPECT_GE(rough0, 0.0f);  EXPECT_LE(rough0, 1.0f);
}
