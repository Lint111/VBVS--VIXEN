#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include "SdfBake.h"
#include "SdfRecipes.h"

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
