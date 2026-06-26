#include <gtest/gtest.h>
#include "Recipe/SdfRecipeEval.h"
#include <glm/glm.hpp>
using namespace Vixen::SVO::Recipe;

static SdfInstruction sphere(glm::vec3 c, float r) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere;
    in.data[0]=c.x; in.data[1]=c.y; in.data[2]=c.z; in.data[3]=r; return in;
}
static SdfInstruction unionOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Union; return in; }

TEST(RecipeEvalParity, SphereUnionMatchesAnalytic) {
    const glm::vec3 c0(-1,0,0), c1(1,0,0); const float r0=1.0f, r1=1.0f;
    SdfInstruction prog[] = { sphere(c0,r0), sphere(c1,r1), unionOp() };
    auto golden = [&](glm::vec3 p){ return std::min(glm::length(p-c0)-r0, glm::length(p-c1)-r1); };
    for (glm::vec3 p : { glm::vec3(-1,0,0), glm::vec3(1,0,0), glm::vec3(0,0,0), glm::vec3(0,2,0), glm::vec3(3,1,-1) })
        EXPECT_NEAR(evalRecipe(prog, 3, p), golden(p), 1e-5f) << "at " << p.x << "," << p.y << "," << p.z;
}
