#include <gtest/gtest.h>
#include "Recipe/RecipeRegistry.h"
using namespace Vixen::SVO;
using Recipe::SdfInstruction;
using Recipe::SdfOpCode;

static SdfInstruction sphere(float r) {
    SdfInstruction in{};
    in.opCode  = (uint8_t)SdfOpCode::Sphere;
    in.data[3] = r;
    return in;
}

TEST(RecipeRegistry, RegisterAndGetRoundTrips) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{};
    e.bytecode = { sphere(26.0f) };
    e.bakeResolution = 64; e.bandVoxels = 2.5f; e.brickDepth = 3;
    e.octreeSlot = RecipeRegistry::kUnbakedSlot;

    ASSERT_EQ(reg.Register(7u, e), RecipeRegistry::RegisterResult::Ok);
    const auto* got = reg.Get(7u);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->bytecode.size(), 1u);
    EXPECT_EQ(got->bakeResolution, 64u);
    EXPECT_EQ(reg.Get(8u), nullptr);
}

TEST(RecipeRegistry, IdsAscending) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{}; e.bytecode = { sphere(1.0f) };
    reg.Register(5u, e);
    reg.Register(2u, e);
    reg.Register(9u, e);
    auto ids = reg.Ids();
    ASSERT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids[0], 2u);
    EXPECT_EQ(ids[1], 5u);
    EXPECT_EQ(ids[2], 9u);
}

TEST(RecipeRegistry, RejectsDuplicateAndEmptyAndBadOpcode) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{}; e.bytecode = { sphere(1.0f) };
    EXPECT_EQ(reg.Register(1u, e), RecipeRegistry::RegisterResult::Ok);
    EXPECT_EQ(reg.Register(1u, e), RecipeRegistry::RegisterResult::DuplicateId);

    RecipeRegistry::RecipeEntry empty{};
    EXPECT_EQ(reg.Register(2u, empty), RecipeRegistry::RegisterResult::EmptyProgram);

    RecipeRegistry::RecipeEntry bad{};
    SdfInstruction badInstr{}; badInstr.opCode = 254; // not a valid SdfOpCode
    bad.bytecode = { badInstr };
    EXPECT_EQ(reg.Register(3u, bad), RecipeRegistry::RegisterResult::BadOpCode);

    RecipeRegistry::RecipeEntry pm{}; SdfInstruction pmi = sphere(1.0f); pmi.paramMask = 1;
    pm.bytecode = { pmi };
    EXPECT_EQ(reg.Register(4u, pm), RecipeRegistry::RegisterResult::ParamMaskUnsupported);
}

TEST(RecipeRegistry, RejectsStackOverflow) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{};
    for (int i = 0; i < 65; ++i) e.bytecode.push_back(sphere(1.0f)); // 65 pushes, never popped
    EXPECT_EQ(reg.Register(9u, e), RecipeRegistry::RegisterResult::StackOverflow);
}

TEST(RecipeRegistry, AcceptsBalancedRecipe) {
    // 2 Sphere pushes + 1 Union (pop 2, push 1) = 1 value on stack — valid
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{};
    e.bytecode = { sphere(10.0f), sphere(20.0f) };
    SdfInstruction u{}; u.opCode = (uint8_t)SdfOpCode::Union;
    e.bytecode.push_back(u);
    EXPECT_EQ(reg.Register(10u, e), RecipeRegistry::RegisterResult::Ok);
}

TEST(RecipeRegistry, GetMutableStampsSlot) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{}; e.bytecode = { sphere(1.0f) };
    reg.Register(42u, e);
    auto* mut = reg.GetMutable(42u);
    ASSERT_NE(mut, nullptr);
    mut->octreeSlot = 0u;
    EXPECT_EQ(reg.Get(42u)->octreeSlot, 0u);
}
