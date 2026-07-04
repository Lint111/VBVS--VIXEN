#include <gtest/gtest.h>
#include "SdfBake.h"
#include "Recipe/SdfInstruction.h"
#include <glm/glm.hpp>

using namespace Vixen::SVO;

static Recipe::SdfInstruction sphereInstr(glm::vec3 c, float r) {
    Recipe::SdfInstruction in{};
    in.opCode  = (uint8_t)Recipe::SdfOpCode::Sphere;
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
    return in;
}

TEST(RecipeBake, RecipeSphereEqualsAnalyticSphere) {
    const int n = 64;
    const glm::vec3 c(32, 32, 32);   // grid center — passed as `center`, not baked into the instruction
    const float r = 26.0f, band = 2.5f;

    auto analytic = BakeRecipeToSdfWorld(RECIPE_SPHERE, c, RecipeParams{r, 0, 0, 0, 0, 0}, n, band, 3);
    Recipe::SdfInstruction prog[] = { sphereInstr(glm::vec3(0, 0, 0), r) };  // object-centered
    auto recipe = BakeRecipeInstructionsToSdfWorld(prog, 1, c, n, band, 3);

    int checked = 0;
    for (glm::vec3 p : { glm::vec3(32, 32, 6), glm::vec3(32, 32, 32),
                         glm::vec3(48, 32, 32), glm::vec3(10, 10, 10) }) {
        auto a = analytic.sampleStored(p);
        auto b = recipe.sampleStored(p);
        ASSERT_EQ(a.has_value(), b.has_value())
            << "allocation differs at " << p.x << "," << p.y << "," << p.z;
        if (a.has_value()) {
            EXPECT_NEAR(a.value(), b.value(), 1e-4f);
            ++checked;
        }
    }
    EXPECT_GT(checked, 0);
}
