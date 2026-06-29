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
// Inc2 M6 contract: narrow-band SDF is sparse at the BRICK level — a brick the
// iso-surface passes through (plus a 1-brick dilation margin) is allocated and
// FULLY populated with the true signed distance; bricks far from the surface are
// unallocated. (Storing only band cells left the rest as 0 → false iso-surfaces
// that the GPU trilinear march hit as brick facets — see SdfBake.h.)
//
// Bake a small sphere (radius 6, centre (32,32,32)) into a 64^3 grid with band=2,
// so the surface shell leaves clearly-unallocated bricks at the corners. Assert:
//   (a) a surface voxel IS stored and its Density ≈ evalSdf
//   (b) a NON-band voxel INSIDE an active brick IS stored with its true sd
//       (full-brick population — the M6 fix)
//   (c) a far-corner voxel, in a brick the surface never reaches, is NOT stored
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

    // (b) Non-band voxel still inside an active brick: (41,32,32), sd = 3 (> band=2),
    //     but its brick [40,48) contains band voxel (40,32,32) (sd=2) → active → fully
    //     populated, so this cell now carries its true sd (NOT left empty/0).
    glm::vec3 inBrick(41.0f, 32.0f, 32.0f);
    auto sdInBrick = baked.sampleStored(inBrick);
    ASSERT_TRUE(sdInBrick.has_value())
        << "Non-band voxel inside an ACTIVE brick must be fully populated (M6 full-brick fix)";
    EXPECT_NEAR(*sdInBrick, evalSdf(RECIPE_SPHERE, inBrick, center, rp), 0.6f)
        << "Full-brick population must store the TRUE signed distance, not 0";

    // (c) Far-corner voxel (0,0,0): its brick [0,8) is several bricks from the surface
    //     shell (even after the 1-brick dilation) → unallocated.
    EXPECT_FALSE(baked.sampleStored(glm::vec3(0.0f, 0.0f, 0.0f)).has_value())
        << "Far-corner voxel must NOT be stored (brick-level sparsity preserved)";
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
