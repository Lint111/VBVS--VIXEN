#include <gtest/gtest.h>
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeStack.h"
using namespace Vixen::SVO;
using Recipe::SdfInstruction;
using Recipe::SdfOpCode;
using Recipe::RecipeStackArity;
using Recipe::StackArity;

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

// --- Link regression: previously missing from arity table ---------------

static SdfInstruction link() {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Link;
    in.data[0] = 5.0f; in.data[1] = 2.0f; in.data[2] = 0.5f; // halfLen, majorR, minorR
    return in;
}

static SdfInstruction makeUnion() {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Union; return in;
}

TEST(RecipeRegistry, LinkAcceptsValidRecipe) {
    // {Link, Link, Union} is balanced (2 pushes, 1 binary pop) → Ok
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{};
    e.bytecode = { link(), link(), makeUnion() };
    EXPECT_EQ(reg.Register(20u, e), RecipeRegistry::RegisterResult::Ok);
}

TEST(RecipeRegistry, LinkRejectsStackOverflow) {
    // 65 Link pushes, never popped → StackOverflow
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{};
    for (int i = 0; i < 65; ++i) e.bytecode.push_back(link());
    EXPECT_EQ(reg.Register(21u, e), RecipeRegistry::RegisterResult::StackOverflow);
}

// --- Drift-guard: every valid opcode must have non-trivial arity ---------
// Catches missing entries in RecipeStackArity before they become bad accepts/rejects.
// Legitimate no-ops (Output, ComposeFloat3) are explicitly excluded.

TEST(RecipeRegistry, ArityTableCoversAllValidOpcodes) {
    constexpr uint8_t kLegitimateNoOps[] = {
        (uint8_t)SdfOpCode::Output,
        (uint8_t)SdfOpCode::ComposeFloat3,
    };
    auto isLegitNoOp = [&](uint8_t v) {
        for (auto n : kLegitimateNoOps) if (v == n) return true;
        return false;
    };

    std::vector<uint8_t> missing;
    for (int raw = 0; raw < 256; ++raw) {
        uint8_t v = static_cast<uint8_t>(raw);
        if (!IsValidSdfOpCode(v)) continue;
        if (isLegitNoOp(v)) continue;
        auto a = RecipeStackArity(static_cast<SdfOpCode>(v));
        if (a.vPop == 0 && a.vPush == 0 && a.pPop == 0 && a.pPush == 0)
            missing.push_back(v);
    }

    for (uint8_t v : missing)
        ADD_FAILURE() << "opcode " << (int)v << " is valid but has zero arity — add to RecipeStackArity";
    EXPECT_TRUE(missing.empty());
}
