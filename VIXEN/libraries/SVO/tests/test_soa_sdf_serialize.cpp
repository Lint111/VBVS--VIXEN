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
#include <cstdio>
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

// Every cell NOT in brickGridToBrickView must be the single "no brick" sentinel
// kBrickUnalloc (0xFFFFFFFF). The invariant under test is: no allocated brickView
// index leaks into an empty cell.
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
            EXPECT_TRUE(Vixen::SVO::isBrickUnallocated(lookup[i]))
                << "unallocated cell [" << i << "] must be the 'no brick' sentinel "
                << "(0xFFFFFFFF), got " << lookup[i];
        }
    }
}

// Occupancy-based brick selection (commit 7db15496 — the brick-fleck root-cause fix):
// interior bricks of a signed-distance body are SELECTED BY OCCUPANCY (not density>0), so
// every active brick — including fully-interior ones (all voxels sd<=0) — is allocated.
// Previously querySolidVoxels' density>0 filter dropped all-interior bricks, leaving them
// unallocated and corrupting surface stencils that reached into them. Post-fix every active
// interior brick is allocated, so a surface stencil never reads an unallocated interior
// brick — which is why the single positive sentinel suffices (no sign-aware sentinel needed).
// Bakes the RENDER-scene sphere (n=64, r=26, band=2.5 → bpa=8) whose grid-corner cells are
// unallocated exterior while the (solid) core is fully allocated — a decisive check. (The
// shared 16^3 fixture has bpa=2, too small to have an interior brick distinct from the shell.)
TEST(SoaSdfSerialize, InteriorBricksAllocatedNotDropped) {
    RecipeParams rp{26.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, glm::vec3(32.0f), rp,
                                               /*n=*/64, /*band=*/2.5f, /*brickDepth=*/3);
    SdfBodyOctree body = BuildSdfBodyOctree(baked, 3);
    SerializedOctree out = SerializeSdf(body);
    ASSERT_FALSE(out.brickGridLookup.empty());

    const Vixen::SVO::Octree* oct = body.octree->getOctree();
    const int bpa = oct->bricksPerAxis;
    ASSERT_GE(bpa, 3) << "need an interior brick distinct from the shell";
    const uint32_t tableSize = static_cast<uint32_t>(bpa * bpa * bpa);

    std::vector<uint32_t> lookup(tableSize);
    std::memcpy(lookup.data(), out.brickGridLookup.data(), out.brickGridLookup.size());

    auto flatOf = [bpa](int x, int y, int z) {
        return static_cast<uint32_t>(z * bpa * bpa + y * bpa + x);
    };

    int unallocated = 0, allocated = 0;
    for (uint32_t v : lookup) {
        if (Vixen::SVO::isBrickUnallocated(v)) ++unallocated;
        else ++allocated;
    }
    std::printf("[OCCUPANCY] bpa=%d allocated=%d unallocated=%d\n",
                bpa, allocated, unallocated);

    EXPECT_GT(allocated, 0) << "expected the solid-sphere shell+interior bricks to be allocated";
    EXPECT_GT(unallocated, 0) << "expected some unallocated bricks (grid corners)";

    // The grid CENTRE brick is deep inside the (solid) sphere → must be ALLOCATED. (Pre-fix
    // the density>0 filter dropped it — this is the regression guard for the occupancy fix.)
    const int c = bpa / 2;
    const uint32_t centre = lookup[flatOf(c, c, c)];
    EXPECT_FALSE(Vixen::SVO::isBrickUnallocated(centre))
        << "the solid-sphere grid-centre brick must be ALLOCATED (interior no longer dropped)";

    // A grid CORNER brick is far outside → unallocated, carrying the single sentinel.
    const uint32_t corner = lookup[flatOf(0, 0, 0)];
    ASSERT_TRUE(Vixen::SVO::isBrickUnallocated(corner)) << "corner brick should be empty";
    EXPECT_EQ(corner, Vixen::SVO::kBrickUnalloc)
        << "an unallocated brick must carry the single 'no brick' sentinel (0xFFFFFFFF)";
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

    // 4 channels: sdf(1)+color(3)+roughness(1)+emission(1) = 6 float-lanes/voxel
    // (Sampled Lighting Inc3 M3 added the scalar emissive-intensity channel.)
    EXPECT_EQ(out.channelCount, 4u);
    EXPECT_EQ(out.brickStrideFloats, (1u + 3u + 1u + 1u) * 512u);   // 3072

    // channelBaseFloats: sdf 0, color 512, roughness 2048, emission 2560 (declaration order)
    EXPECT_EQ(out.channelBaseFloats(SEM_SDF),       0u);
    EXPECT_EQ(out.channelBaseFloats(SEM_COLOR),     512u);
    EXPECT_EQ(out.channelBaseFloats(SEM_ROUGHNESS), 2048u);
    EXPECT_EQ(out.channelBaseFloats(SEM_EMISSION),  2560u);

    // Pool byte size = brickCount * brickStrideFloats * sizeof(float)
    EXPECT_EQ(out.channelPool.size(),
              static_cast<size_t>(out.brickCount) * out.brickStrideFloats * sizeof(float));

    // A voxel in the SDF channel must hold a finite value
    float sdf = out.readPoolVoxel(SEM_SDF, 0, 0, 0);
    EXPECT_TRUE(std::isfinite(sdf));
}

// Inc3 M2 Task 4 — baked color+roughness values round-trip through the pool.
// TIGHTENED (Inc3 M4 T10): verify the EXACT formula (not just finite+in-range),
// using a known surface voxel found via brickViews.  This closes the color/roughness
// swap gap: if the channels were swapped the EXPECT_NEAR would fail.
TEST(SoaSdfSerialize, MultiChannelBakedColorRoughness) {
    RecipeParams rp{6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    Vixen::SVO::SdfBakeResult baked =
        Vixen::SVO::BakeRecipeToSdfWorld(RECIPE_SPHERE, glm::vec3(32), rp, 64, 2.5f);
    Vixen::SVO::SdfBodyOctree body = Vixen::SVO::BuildSdfBodyOctree(baked, 3);
    Vixen::SVO::SerializedOctree out = Vixen::SVO::SerializeSdf(body);

    ASSERT_EQ(out.channelCount, 4u);
    ASSERT_FALSE(out.channelPool.empty());

    // ------------------------------------------------------------------
    // Locate a known surface voxel: p = (38, 32, 32) lies on the +X surface
    // of the sphere (center=32, r=6). The bake dilates by 1 brick so this brick
    // is guaranteed to be active.
    // ------------------------------------------------------------------
    const glm::vec3 targetPos(38.0f, 32.0f, 32.0f);

    const Vixen::SVO::Octree* oct = body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    ASSERT_NE(oct->root, nullptr);
    const auto& brickViews = oct->root->brickViews;
    ASSERT_FALSE(brickViews.empty());

    bool found = false;
    for (uint32_t bi = 0; bi < static_cast<uint32_t>(brickViews.size()); ++bi) {
        const glm::ivec3 origin = brickViews[bi].getLocalGridOrigin();
        const glm::ivec3 local  = glm::ivec3(targetPos) - origin;
        if (local.x < 0 || local.x >= 8 || local.y < 0 || local.y >= 8 ||
            local.z < 0 || local.z >= 8) {
            continue;
        }
        const uint32_t slot = static_cast<uint32_t>(local.z * 64 + local.y * 8 + local.x);

        // --- Exact bake formula (from SdfBake.h BakeRecipeToSdfWorld) ---
        // color = 0.5 + 0.5*cos(p*0.12 + {0, 2.094, 4.188})
        const glm::vec3 colExpected = 0.5f + 0.5f * glm::cos(
            glm::vec3(targetPos.x, targetPos.y, targetPos.z) * 0.12f
            + glm::vec3(0.0f, 2.094f, 4.188f));
        // roughness = clamp(0.2 + 0.6*fract(p.y*0.0625), 0, 1)
        const float roughExpected = glm::clamp(
            0.2f + 0.6f * glm::fract(targetPos.y * 0.0625f), 0.0f, 1.0f);

        const float storedR  = out.readPoolVoxel(SEM_COLOR,     bi, slot, 0);
        const float storedG  = out.readPoolVoxel(SEM_COLOR,     bi, slot, 1);
        const float storedB  = out.readPoolVoxel(SEM_COLOR,     bi, slot, 2);
        const float storedRg = out.readPoolVoxel(SEM_ROUGHNESS, bi, slot, 0);

        EXPECT_NEAR(storedR,  colExpected.r, 0.01f)
            << "Color.R at p=(38,32,32) must match bake formula";
        EXPECT_NEAR(storedG,  colExpected.g, 0.01f)
            << "Color.G at p=(38,32,32) must match bake formula";
        EXPECT_NEAR(storedB,  colExpected.b, 0.01f)
            << "Color.B at p=(38,32,32) must match bake formula";
        EXPECT_NEAR(storedRg, roughExpected, 0.01f)
            << "Roughness at p=(38,32,32) must match bake formula";

        found = true;
        break;
    }
    EXPECT_TRUE(found) << "No active brick contains surface voxel (38,32,32)";
}

// ---------------------------------------------------------------------------
// Sampled Lighting Inc3 M3 — scalar emissive channel
// ---------------------------------------------------------------------------

// A bake that never supplies an emission function (default NoEmission) must
// produce an all-zero emissive channel — the byte-identity escape hatch this
// whole increment relies on (an emissive-absent scene reproduces M1/M2 output).
TEST(SoaSdfSerialize, EmissionChannelDefaultsToZero) {
    SdfFixture fx;
    Vixen::SVO::SerializedOctree out = Vixen::SVO::SerializeSdf(fx.body);

    ASSERT_NE(out.channelBaseFloats(SEM_EMISSION), 0xFFFFFFFFu)
        << "SEM_EMISSION must be present in the channel table";

    bool anyNonzero = false;
    for (uint32_t bi = 0; bi < out.brickCount; ++bi) {
        for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; ++voxel) {
            if (out.readPoolVoxel(SEM_EMISSION, bi, voxel, 0) != 0.0f) {
                anyNonzero = true;
            }
        }
    }
    EXPECT_FALSE(anyNonzero) << "Emission channel must default to all-zero (no emit fn supplied)";
}

// A bake WITH an emission function must round-trip a known intensity through
// the channel pool at a known surface voxel (mirrors MultiChannelBakedColorRoughness's
// exact-formula methodology).
TEST(SoaSdfSerialize, EmissionChannelRoundTrips) {
    RecipeParams rp{6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec3 center(32.0f, 32.0f, 32.0f);
    constexpr float kEmit = 7.5f;
    Vixen::SVO::SdfBakeResult baked = Vixen::SVO::BakeRecipeToSdfWorldWithEmission(
        RECIPE_SPHERE, center, rp, 64, 2.5f,
        [](const glm::vec3&) { return kEmit; });
    Vixen::SVO::SdfBodyOctree body = Vixen::SVO::BuildSdfBodyOctree(baked, 3);
    Vixen::SVO::SerializedOctree out = Vixen::SVO::SerializeSdf(body);

    ASSERT_EQ(out.channelCount, 4u);

    const glm::vec3 targetPos(38.0f, 32.0f, 32.0f);
    const Vixen::SVO::Octree* oct = body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    ASSERT_NE(oct->root, nullptr);
    const auto& brickViews = oct->root->brickViews;
    ASSERT_FALSE(brickViews.empty());

    bool found = false;
    for (uint32_t bi = 0; bi < static_cast<uint32_t>(brickViews.size()); ++bi) {
        const glm::ivec3 origin = brickViews[bi].getLocalGridOrigin();
        const glm::ivec3 local  = glm::ivec3(targetPos) - origin;
        if (local.x < 0 || local.x >= 8 || local.y < 0 || local.y >= 8 ||
            local.z < 0 || local.z >= 8) {
            continue;
        }
        const uint32_t slot = static_cast<uint32_t>(local.z * 64 + local.y * 8 + local.x);
        const float storedEmit = out.readPoolVoxel(SEM_EMISSION, bi, slot, 0);
        EXPECT_NEAR(storedEmit, kEmit, 1e-5f)
            << "Emission at p=(38,32,32) must round-trip the emit fn's constant value";
        found = true;
        break;
    }
    EXPECT_TRUE(found) << "No active brick contains surface voxel (38,32,32)";
}
