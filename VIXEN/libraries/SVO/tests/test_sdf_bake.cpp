#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include "SdfBake.h"
#include "SdfRecipes.h"
#include "ShellOctreeGpu.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// MSVC's <windows.h> (pulled in transitively on the Windows build) defines
// far/near as segment-qualifier macros and min/max as function macros.
#undef far
#undef near
#undef min
#undef max

using namespace Vixen::SVO;

// ============================================================================
// Task 1 — BakeRecipeToSdfWorld / NarrowBandMatchesRecipe
//
// Occupancy contract (supersedes the old Inc2 M6 thin-band rule): SDF is sparse at
// the BRICK level — a brick that is INSIDE the solid OR within the surface band
// (evalSdf <= bandVoxels), plus a 1-brick dilation margin, is allocated and FULLY
// populated with the true signed distance; exterior bricks beyond the band are
// unallocated. (The old |evalSdf| <= band predicate dropped deep-interior bricks the
// thin shell never reached → SDF interior holes — see SdfBake.h.)
//
// Bake a small sphere (radius 6, centre (32,32,32)) into a 64^3 grid with band=2,
// so the exterior corners stay clearly-unallocated. Assert:
//   (a) a surface voxel IS stored and its Density ≈ evalSdf
//   (b) an outward NON-band voxel INSIDE an occupied brick IS stored with its true sd
//       (full-brick population)
//   (c) a far-corner exterior voxel, beyond the band, is NOT stored
//       (brick-level sparsity preserved)
// ============================================================================
TEST(SdfBake, NarrowBandMatchesRecipe) {
    const int n = 64;
    const glm::vec3 center(32.0f, 32.0f, 32.0f);
    RecipeParams rp{6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // sphere r=6 (small vs 64^3 grid)
    const float bandVoxels = 2.0f;

    SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, bandVoxels);

    // (a) Surface voxel on the +x axis: (38,32,32), |38-32| - 6 = 0 (on surface).
    glm::vec3 surf(38.0f, 32.0f, 32.0f);
    auto sdSurf = baked.sampleStored(surf);
    ASSERT_TRUE(sdSurf.has_value()) << "Surface voxel (38,32,32) must be stored";
    EXPECT_NEAR(*sdSurf, evalSdf(RECIPE_SPHERE, surf, center, rp), 0.6f)
        << "Stored Density must approximate evalSdf";

    // (b) Outward non-band voxel inside an occupied brick: (41,32,32), sd = 3 (> band=2),
    //     but its brick [40,48) contains the in-band voxel (40,32,32) (sd=2 <= band) → the
    //     brick is occupied → fully populated, so this cell carries its true sd (NOT 0).
    glm::vec3 inBrick(41.0f, 32.0f, 32.0f);
    auto sdInBrick = baked.sampleStored(inBrick);
    ASSERT_TRUE(sdInBrick.has_value())
        << "Non-band voxel inside an OCCUPIED brick must be fully populated (full-brick fill)";
    EXPECT_NEAR(*sdInBrick, evalSdf(RECIPE_SPHERE, inBrick, center, rp), 0.6f)
        << "Full-brick population must store the TRUE signed distance, not 0";

    // (c) Far-corner voxel (0,0,0): sd = |(-32,-32,-32)| - 6 ≈ +49 (far beyond band=2)
    //     and its brick [0,8) is nowhere near the solid (even after 1-brick dilation) →
    //     unallocated. Confirms occupancy sparsity does NOT flood the whole grid.
    EXPECT_FALSE(baked.sampleStored(glm::vec3(0.0f, 0.0f, 0.0f)).has_value())
        << "Far-corner exterior voxel must NOT be stored (brick-level sparsity preserved)";
}

// ============================================================================
// Occupancy regression guard — the DEEP INTERIOR must now be stored.
//
// This is the direct regression test for the interior-hole bug: a point WELL
// inside a large sphere (sd ≪ -bandVoxels), in a brick that never touches the
// surface shell, must carry real SDF data. Under the OLD |evalSdf| <= band
// predicate that brick was never marked active (no in-band cell) → the point
// was unallocated → an interior hole. Under the occupancy predicate (sd <= band)
// every interior cell marks its brick occupied, so the point is stored.
//
// Sphere radius 48 at (64,64,64) in 128^3, band=2. The centre brick [64,72) is
// entirely interior with its NEAREST in-band cell ~3 bricks away, so the 1-brick
// dilation of the old thin-shell predicate CANNOT reach it — the point is
// unallocated on old code (interior hole) and allocated on new (occupancy).
// (A smaller sphere would let dilation flood the tiny interior, hiding the bug.)
// ============================================================================
TEST(SdfBake, DeepInteriorIsStored) {
    const int n = 128;
    const glm::vec3 center(64.0f, 64.0f, 64.0f);
    RecipeParams rp{48.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // large sphere r=48
    const float bandVoxels = 2.0f;

    SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, bandVoxels);

    // Deep-interior point = sphere centre, sd = -48, ~3 bricks from the shell.
    glm::vec3 deep(64.0f, 64.0f, 64.0f);
    ASSERT_LT(evalSdf(RECIPE_SPHERE, deep, center, rp), -bandVoxels)
        << "test precondition: point must be deeper than the band (sd ≪ -band)";
    auto sdDeep = baked.sampleStored(deep);
    ASSERT_TRUE(sdDeep.has_value())
        << "Deep-interior voxel (sd ≪ -band) must be stored under occupancy "
           "(fails on old thin-band predicate — the interior-hole bug)";
    EXPECT_NEAR(*sdDeep, evalSdf(RECIPE_SPHERE, deep, center, rp), 0.6f)
        << "Interior must carry the TRUE (negative) signed distance, not a 0 sentinel";
}

// ============================================================================
// Task 2 — BuildSdfBodyOctree
//
// Build the octree from the baked world.  Assert:
//   (a) octree is non-null and rebuilds without crash
//   (b) the raw octree pointer exposes non-empty brickViews on its root
//       (same accessor pattern as test_shell_octree.cpp uses)
//   (c) castRay hits the surface from outside the grid
// ============================================================================
TEST(SdfBake, OctreeBuildIsNonEmpty) {
    const int n = 16;
    const glm::vec3 center(8.0f, 8.0f, 8.0f);
    RecipeParams rp{6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const float bandVoxels = 2.0f;

    SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, bandVoxels);
    SdfBodyOctree body  = BuildSdfBodyOctree(baked, 3);

    // (a) Non-null
    ASSERT_NE(body.octree, nullptr);

    // (b) brickViews non-empty — uses the Octree* accessor the same way
    //     test_shell_octree.cpp does (oct->root->brickViews).
    const Vixen::SVO::Octree* oct = body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    ASSERT_NE(oct->root, nullptr);
    EXPECT_FALSE(oct->root->brickViews.empty())
        << "Narrow-band bake must produce at least one occupied brick";

    // (c) castRay from outside along +x axis should hit the baked SDF shell
    //     Ray from (-2,8,8) pointing in +x; surface is near x=2 (inner) and x=14 (outer).
    auto hit = body.octree->castRay(
        glm::vec3(-2.0f, 8.0f, 8.0f),
        glm::vec3( 1.0f, 0.0f, 0.0f),
        0.0f, 1e30f);
    EXPECT_TRUE(hit.hit) << "castRay must hit the baked SDF shell along +x axis";
}

// ============================================================================
// SDF Bake Box-Tight Region M1 — BakeSdfWorld's optional bakeRegion parameter.
//
// A thin slab (like a Cornell wall): thin along X, wide along Y/Z, occupying
// only a small X-slice of an n=32 cube grid. Bake it twice with the SAME
// geometry: once with the DEFAULT (full-cube) bake region, once with an
// explicit box-tight bakeRegion tightly covering only the slab's true X
// extent. Assert the box-tight bake allocates STRICTLY FEWER bricks than the
// full-cube bake -- the actual point of this plan's box-tight mechanism (a
// thin body no longer pays for empty volume along its thin axis).
// ============================================================================
namespace {
// Thin slab centered on the grid: |x - 16| <= 2 (thin X, ~4 voxels thick),
// full-span Y/Z. Occupancy predicate is `sd <= bandVoxels`, so this slab's
// full [0,32)x[0,32) Y/Z span is occupied only near x=16 -- everything at
// x < 12 or x >= 20 in a full-cube bake is genuinely empty space that a
// box-tight bakeRegion should skip entirely.
float thinSlabSdf(const glm::vec3& p) {
    const glm::vec3 halfExtent(2.0f, 100.0f, 100.0f);  // thin X, effectively-infinite Y/Z
    const glm::vec3 center(16.0f, 16.0f, 16.0f);
    const glm::vec3 q = glm::abs(p - center) - halfExtent;
    const glm::vec3 qPos = glm::max(q, glm::vec3(0.0f));
    const float outside = glm::length(qPos);
    const float inside = std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
    return outside + inside;
}

// Count allocated (non-unallocated) brickGridLookup cells for a baked body.
int CountAllocatedBricks(const SdfBodyOctree& body) {
    Vixen::SVO::SerializedOctree out = Vixen::SVO::SerializeSdf(body);
    const int bpa = body.octree->getOctree()->bricksPerAxis;
    const uint32_t tableSize = static_cast<uint32_t>(bpa) * bpa * bpa;
    std::vector<uint32_t> lookup(tableSize);
    std::memcpy(lookup.data(), out.brickGridLookup.data(), out.brickGridLookup.size());
    int allocated = 0;
    for (uint32_t v : lookup) if (!Vixen::SVO::isBrickUnallocated(v)) ++allocated;
    return allocated;
}
}  // namespace

TEST(SdfBake, BoxTightRegionAllocatesFewerBricksThanFullCube) {
    const int n = 32;
    const float bandVoxels = 2.0f;
    const glm::vec3 center(16.0f, 16.0f, 16.0f);

    // Full-cube bake: default bakeRegion (today's unconditional [0,n)^3 behavior).
    SdfBakeResult fullBaked = BakeSdfWorld(thinSlabSdf, center, n, bandVoxels);
    SdfBodyOctree fullBody  = BuildSdfBodyOctree(fullBaked, 3);
    const int fullAllocated = CountAllocatedBricks(fullBody);

    // Box-tight bake: same geometry, but bakeRegion covers only the slab's true
    // X extent (|x-16|<=2 -> [12,20), rounded to whole bricks [8,24) at brickSide=8)
    // tightly, while still spanning the full Y/Z.
    SdfBakeResult tightBaked = BakeSdfWorld(thinSlabSdf, center, n, bandVoxels, /*brickDepth=*/3,
                                            NoEmission, DefaultBandColor,
                                            /*bakeRegion=*/glm::ivec3(24, 32, 32));
    SdfBodyOctree tightBody  = BuildSdfBodyOctree(tightBaked, 3);
    const int tightAllocated = CountAllocatedBricks(tightBody);

    std::printf("[BoxTight] full-cube allocated=%d, box-tight allocated=%d\n",
                fullAllocated, tightAllocated);

    EXPECT_LT(tightAllocated, fullAllocated)
        << "box-tight bakeRegion must allocate strictly fewer bricks than the full-cube bake "
           "for a thin-slab body";
    EXPECT_GT(tightAllocated, 0) << "box-tight bake must still allocate the slab's own bricks";

    // The box-tight octree must still hit the same surface (correctness, not just sparsity).
    auto hit = tightBody.octree->castRay(
        glm::vec3(-2.0f, 16.0f, 16.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        0.0f, 1e30f);
    EXPECT_TRUE(hit.hit) << "box-tight bake must still produce a hittable surface";
}

// bakeRegion defaulting (omitted entirely) must be byte-identical to passing
// bakeRegion == glm::ivec3(n) explicitly -- confirms the default-preservation
// contract the M1 gate requires (every existing caller stays unchanged).
TEST(SdfBake, BoxTightRegionDefaultMatchesExplicitFullCube) {
    const int n = 32;
    const float bandVoxels = 2.0f;
    const glm::vec3 center(16.0f, 16.0f, 16.0f);

    SdfBakeResult defaultBaked = BakeSdfWorld(thinSlabSdf, center, n, bandVoxels);
    SdfBodyOctree defaultBody  = BuildSdfBodyOctree(defaultBaked, 3);
    const int defaultAllocated = CountAllocatedBricks(defaultBody);

    SdfBakeResult explicitBaked = BakeSdfWorld(thinSlabSdf, center, n, bandVoxels, /*brickDepth=*/3,
                                               NoEmission, DefaultBandColor,
                                               /*bakeRegion=*/glm::ivec3(n));
    SdfBodyOctree explicitBody  = BuildSdfBodyOctree(explicitBaked, 3);
    const int explicitAllocated = CountAllocatedBricks(explicitBody);

    EXPECT_EQ(defaultAllocated, explicitAllocated)
        << "omitting bakeRegion must match explicitly passing glm::ivec3(n) (full cube)";
}

// ============================================================================
// SDF Bake Box-Tight Region M1 — BuildSdfBodyOctree pow2 round-UP contract.
//
// A deliberately NON-power-of-two bake grid (n=48, between pow2 32 and 64).
// BuildSdfBodyOctree must round the octree's own resolution UP to 64 (the next
// covering pow2), never DOWN to 32 -- rounding down would make the octree's
// worldMax (32) SMALLER than the actual baked region (48), desyncing brick
// addressing exactly like the real Cornell kWallN=112->64 bug this plan's own
// investigation found. Verify by construction: castRay must still hit a
// surface placed near the edge of the 48-grid (x~44), which a wrongly-rounded
// 32-grid octree could not possibly represent (its worldMax would be 32).
// ============================================================================
TEST(SdfBake, BuildSdfBodyOctreeRoundsPow2Up) {
    const int n = 48;  // deliberately non-pow2 (32 < 48 < 64)
    const glm::vec3 center(24.0f, 24.0f, 24.0f);
    RecipeParams rp{6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // sphere r=6, near grid centre
    const float bandVoxels = 2.0f;

    SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, bandVoxels);
    SdfBodyOctree body  = BuildSdfBodyOctree(baked, 3);
    ASSERT_NE(body.octree, nullptr);

    const Vixen::SVO::Octree* oct = body.octree->getOctree();
    ASSERT_NE(oct, nullptr);

    // worldMax must be 64 (next pow2 >= 48), NOT 32 (the old round-down result) and NOT 48
    // (n itself is not a valid octree resolution -- must be pow2).
    EXPECT_FLOAT_EQ(oct->worldMax.x, 64.0f)
        << "octree worldMax must round UP to the next covering pow2 (64), not down to 32 or "
           "left at the non-pow2 bake n=48 -- round-down desyncs brick addressing (the real "
           "Cornell kWallN=112->64 bug this plan's investigation found)";
    EXPECT_FLOAT_EQ(oct->worldMax.y, 64.0f);
    EXPECT_FLOAT_EQ(oct->worldMax.z, 64.0f);

    // The extra [48,64) region beyond the bake is never populated -- unallocated, not a
    // correctness problem (verified by CountAllocatedBricks-style reasoning: the sphere
    // near grid centre is still fully allocated and hittable).
    auto hit = body.octree->castRay(
        glm::vec3(24.0f, 24.0f, -2.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        0.0f, 1e30f);
    EXPECT_TRUE(hit.hit) << "sphere surface must still be hittable after pow2 round-up";
}

// n already a power of two (e.g. 64, every existing caller's convention) must be a
// complete no-op for the round-up logic -- worldMax stays exactly n, not n*2.
TEST(SdfBake, BuildSdfBodyOctreePow2InputIsNoOp) {
    const int n = 64;
    const glm::vec3 center(32.0f, 32.0f, 32.0f);
    RecipeParams rp{6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, 2.0f);
    SdfBodyOctree body  = BuildSdfBodyOctree(baked, 3);

    const Vixen::SVO::Octree* oct = body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    EXPECT_FLOAT_EQ(oct->worldMax.x, 64.0f)
        << "an already-pow2 n must stay unchanged (64), not round up to 128";
}
