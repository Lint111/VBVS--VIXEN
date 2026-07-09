/**
 * @file test_recipe_baker.cpp
 * @brief I3.3 — BakeRegistryToPool: memory-budgeted fail-loud bake.
 */
#include <gtest/gtest.h>
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeBaker.h"
#include "Recipe/SdfInstruction.h"
#include <glm/glm.hpp>

using namespace Vixen::SVO;
using Recipe::SdfInstruction;
using Recipe::SdfOpCode;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static SdfInstruction sphereInstr(glm::vec3 c, float r) {
    SdfInstruction in{};
    in.opCode  = (uint8_t)SdfOpCode::Sphere;
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
    return in;
}

// ===========================================================================
// Tests
// ===========================================================================

TEST(RecipeBaker, BakesTwoRecipesToDistinctSlotsAndSamples) {
    RecipeRegistry reg;

    RecipeRegistry::RecipeEntry a{};
    a.bytecode = { sphereInstr(glm::vec3(0, 0, 0), 20.0f) };  // object-centered
    RecipeRegistry::RecipeEntry b{};
    b.bytecode = { sphereInstr(glm::vec3(0, 0, 0), 26.0f) };  // object-centered

    ASSERT_EQ(reg.Register(10u, a), RecipeRegistry::RegisterResult::Ok);
    ASSERT_EQ(reg.Register(11u, b), RecipeRegistry::RegisterResult::Ok);

    RecipeBakeConfig cfg{};  // defaults: center=(32,32,32), n=64, band=2.5, depth=3, unbounded
    RecipeBakeResult r = BakeRegistryToPool(reg, cfg);

    ASSERT_TRUE(r.ok) << r.err;
    EXPECT_EQ(r.pool.count, 2u);
    EXPECT_EQ(r.pool.configs.size(), 2u);

    // Slots are stamped in ascending id order: id 10 → slot 0, id 11 → slot 1.
    EXPECT_EQ(reg.Get(10u)->octreeSlot, 0u);
    EXPECT_EQ(reg.Get(11u)->octreeSlot, 1u);

    // Owned storage keeps the octrees alive.
    EXPECT_EQ(r.owned.size(), 2u);
}

TEST(RecipeBaker, FailsLoudOverBudget) {
    RecipeRegistry reg;

    RecipeRegistry::RecipeEntry a{};
    a.bytecode = { sphereInstr(glm::vec3(0, 0, 0), 26.0f) };  // object-centered
    ASSERT_EQ(reg.Register(1u, a), RecipeRegistry::RegisterResult::Ok);

    RecipeBakeConfig cfg{};
    cfg.byteBudget = 1024;  // absurdly small — any real octree exceeds this

    RecipeBakeResult r = BakeRegistryToPool(reg, cfg);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.err.find("over budget"), std::string::npos) << "err was: " << r.err;
}

TEST(RecipeBaker, UnboundedBudgetAlwaysPasses) {
    RecipeRegistry reg;

    RecipeRegistry::RecipeEntry a{};
    a.bytecode = { sphereInstr(glm::vec3(0, 0, 0), 26.0f) };  // object-centered
    ASSERT_EQ(reg.Register(5u, a), RecipeRegistry::RegisterResult::Ok);

    RecipeBakeConfig cfg{};
    cfg.byteBudget = 0;  // 0 = unbounded

    RecipeBakeResult r = BakeRegistryToPool(reg, cfg);
    EXPECT_TRUE(r.ok) << r.err;
}
