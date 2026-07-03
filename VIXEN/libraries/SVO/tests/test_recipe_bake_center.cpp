// VIXEN/libraries/SVO/tests/test_recipe_bake_center.cpp
//
// Proves BakeRecipeInstructionsToSdfWorld currently IGNORES `center` in its
// eval closure (unlike BakeRecipeToSdfWorld/evalSdf, which already applies
// `p - center`). An object-centered sphere instruction (authored at (0,0,0))
// should, after this task's fix, land at the grid's `center` — today it
// lands at raw grid-origin instead, which this test currently asserts AS
// THE (WRONG) OBSERED BEHAVIOR to document the bug, then gets flipped to
// assert the CORRECT behavior once Task 2 lands the fix.
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "SdfBake.h"
#include "Recipe/RecipeStack.h"

using namespace Vixen::SVO;

namespace {
Recipe::SdfInstruction sphereAtOrigin(float r) {
    Recipe::SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Sphere);
    in.data[0] = 0.0f; in.data[1] = 0.0f; in.data[2] = 0.0f; in.data[3] = r;
    return in;
}
} // namespace

// This test's name and assertion describe the CORRECT (post-fix) behavior.
// Run it now to confirm it FAILS (proving the bug), then it becomes the
// permanent regression test after Task 2's fix makes it pass.
TEST(RecipeBakeCenter, ObjectCenteredSphereBakesAtRequestedGridCenter) {
    const int n = 64;
    const glm::vec3 gridCenter(32.0f, 32.0f, 32.0f);
    const float radius = 10.0f;
    const float band = 2.5f;

    Recipe::SdfInstruction prog[] = { sphereAtOrigin(radius) };
    auto baked = BakeRecipeInstructionsToSdfWorld(prog, 1, gridCenter, n, band, 3);

    // A voxel AT the requested grid-center must be solid (inside the sphere,
    // since the sphere is authored with radius 10 about local-origin, and
    // local-origin should map to gridCenter after centering).
    auto atCenter = baked.sampleStored(gridCenter);
    ASSERT_TRUE(atCenter.has_value())
        << "expected a voxel to be allocated at the grid center for an "
           "object-centered sphere baked with center=" << gridCenter.x
        << "," << gridCenter.y << "," << gridCenter.z;
    EXPECT_LT(*atCenter, 0.0f) << "grid-center point should be INSIDE the "
                                   "sphere (negative signed distance)";

    // A voxel at raw grid-origin (0,0,0) — far outside the sphere's radius
    // from gridCenter — must NOT be solid, proving the geometry was placed
    // AT center, not at raw grid coordinates.
    auto atOrigin = baked.sampleStored(glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_FALSE(atOrigin.has_value())
        << "grid-origin should be far outside the centered sphere; if this "
           "voxel IS allocated, `center` is still being ignored";
}
