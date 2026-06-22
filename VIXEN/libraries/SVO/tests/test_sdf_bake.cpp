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
// Bake a sphere (radius 6, centre (8,8,8)) into a 16^3 grid with band=2.
// Assert:
//   (a) a near-surface voxel IS stored and its Density ≈ evalSdf (±grid tolerance)
//   (b) a far-exterior voxel (0,0,0) is NOT stored (outside the band)
// ============================================================================
TEST(SdfBake, NarrowBandMatchesRecipe) {
    const int n = 16;
    const glm::vec3 center(8.0f, 8.0f, 8.0f);
    RecipeParams rp{6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // sphere r=6
    const float bandVoxels = 2.0f;

    SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, bandVoxels);

    // (a) Near-surface voxel on the +x axis at distance ~6 from centre
    //     Grid pos (14,8,8): dist = |14-8| - 6 = 6 - 6 = 0.0 (exactly on surface).
    glm::vec3 p(14.0f, 8.0f, 8.0f);
    auto sd = baked.sampleStored(p);
    ASSERT_TRUE(sd.has_value()) << "Surface voxel at (14,8,8) must be in the narrow band";
    EXPECT_NEAR(*sd, evalSdf(RECIPE_SPHERE, p, center, rp), 0.6f)
        << "Stored Density must approximate evalSdf within one grid-cell tolerance";

    // (b) Far-exterior corner (0,0,0) — distance ≈ 13.9, well outside band=2
    EXPECT_FALSE(baked.sampleStored(glm::vec3(0.0f, 0.0f, 0.0f)).has_value())
        << "Far-exterior voxel must NOT be stored (outside narrow band)";
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
