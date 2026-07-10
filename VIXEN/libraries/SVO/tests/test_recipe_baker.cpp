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

// ---------------------------------------------------------------------------
// Lazy-Procedural-Delta-Baseline Inc0 M1 Task 1/3a — BakeRegistryToPool must
// bake mips (ConcatenateSdfWithMips, not the plain ConcatenateSdf sibling).
// ---------------------------------------------------------------------------
TEST(RecipeBaker, BakesMipPoolWithMonotonicBases) {
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
    ASSERT_EQ(r.pool.count, 2u);
    ASSERT_EQ(r.pool.configs.size(), 2u);
    ASSERT_EQ(r.pool.nodeCounts.size(), 2u);

    EXPECT_GT(r.pool.mipPool.size(), 0u)
        << "BakeRegistryToPool must bake+attach mips (ConcatenateSdfWithMips), not ConcatenateSdf";

    // Per-octree mipPoolBase stride is nodeCount*channelCount, matching
    // ConcatenateSdfWithMips's own bookkeeping (MipBake.h:373).
    EXPECT_EQ(mipPoolBaseOf(r.pool.configs[0]), 0u);
    const uint32_t expectedBase1 =
        r.pool.nodeCounts[0] * r.pool.configs[0].channelCount;
    EXPECT_EQ(mipPoolBaseOf(r.pool.configs[1]), expectedBase1);
    EXPECT_GT(r.pool.configs[1].channelCount, 0u)
        << "Stored-SDF octrees must carry live channels for mip filtering to apply";
    EXPECT_GT(expectedBase1, 0u) << "octree 0's mip stride should be non-zero (non-trivial node count)";
}

// ---------------------------------------------------------------------------
// Lazy-Procedural-Delta-Baseline Inc0 M1 Task 1/3a — the byte-budget check
// must count mipPool bytes: a budget sized to pass without mips must now fail
// once mips are baked in.
// ---------------------------------------------------------------------------
TEST(RecipeBaker, BudgetCheckCountsMipPoolBytes) {
    RecipeRegistry reg;

    RecipeRegistry::RecipeEntry a{};
    a.bytecode = { sphereInstr(glm::vec3(0, 0, 0), 26.0f) };  // object-centered
    ASSERT_EQ(reg.Register(1u, a), RecipeRegistry::RegisterResult::Ok);

    // First, bake unbounded to learn the actual non-mip byte total so we can
    // size a budget that would have passed under the old (pre-mip) accounting.
    RecipeBakeConfig probeCfg{};
    RecipeBakeResult probe = BakeRegistryToPool(reg, probeCfg);
    ASSERT_TRUE(probe.ok) << probe.err;
    ASSERT_GT(probe.pool.mipPool.size(), 0u);

    const uint64_t nonMipBytes =
        static_cast<uint64_t>(probe.pool.nodes.size()) +
        static_cast<uint64_t>(probe.pool.bricks.size()) +
        static_cast<uint64_t>(probe.pool.channelPool.size());
    const uint64_t mipBytes = static_cast<uint64_t>(probe.pool.mipPool.size());

    // Budget sits strictly between "nodes+bricks+channelPool" and the true
    // total (which also includes mipPool) — passes under the old accounting,
    // must fail under the new one.
    RecipeBakeConfig cfg{};
    cfg.byteBudget = nonMipBytes + (mipBytes / 2);

    RecipeBakeResult r = BakeRegistryToPool(reg, cfg);
    EXPECT_FALSE(r.ok) << "Budget check must count mipPool bytes, not just nodes+bricks+channelPool";
    EXPECT_NE(r.err.find("over budget"), std::string::npos) << "err was: " << r.err;
}
