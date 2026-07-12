#include <gtest/gtest.h>
#include "Recipe/RecipeBounds.h"
#include "Recipe/RecipeRegistry.h"
using namespace Vixen::SVO;
using namespace Vixen::SVO::Recipe;

namespace {

SdfInstruction sphere(glm::vec3 c, float r) {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Sphere;
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z;
    in.data[3] = r;
    return in;
}

SdfInstruction combine(SdfOpCode op, float k = 0.f) {
    SdfInstruction in{};
    in.opCode = (uint8_t)op;
    in.data[2] = k;
    return in;
}

SdfInstruction roundOp(float r) {   // not 'round' — collides with std::round on MSVC (ambiguous overload)
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Round;
    in.data[0] = r;
    return in;
}

SdfInstruction twist(float k) {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Twist;
    in.data[0] = k;
    return in;
}

} // namespace

// --- DeriveConservativeBounds: whitelist opcodes -------------------------

TEST(RecipeBounds, SingleSphereBoundIsExact) {
    std::vector<SdfInstruction> prog = { sphere({1.f, 2.f, 3.f}, 5.f) };
    auto r = DeriveConservativeBounds(prog.data(), (uint32_t)prog.size());
    ASSERT_TRUE(r.ok);
    EXPECT_FLOAT_EQ(r.center.x, 1.f);
    EXPECT_FLOAT_EQ(r.center.y, 2.f);
    EXPECT_FLOAT_EQ(r.center.z, 3.f);
    EXPECT_FLOAT_EQ(r.radius, 5.f);
}

TEST(RecipeBounds, UnionOfDisjointSpheresBoundsBoth) {
    // Two unit spheres far apart — the union bound must actually contain both.
    std::vector<SdfInstruction> prog = {
        sphere({0.f, 0.f, 0.f}, 1.f),
        sphere({10.f, 0.f, 0.f}, 1.f),
        combine(SdfOpCode::Union),
    };
    auto r = DeriveConservativeBounds(prog.data(), (uint32_t)prog.size());
    ASSERT_TRUE(r.ok);
    // Every point of both source spheres must lie within [center, radius].
    EXPECT_LE(glm::length((glm::vec3(0,0,0)) - r.center) + 1.f, r.radius + 1e-4f);
    EXPECT_LE(glm::length((glm::vec3(10,0,0)) - r.center) + 1.f, r.radius + 1e-4f);
}

TEST(RecipeBounds, SmoothSubtractBoundIsConservativeUnion) {
    // Subtract's TRUE extent is a subset of A alone, but this helper deliberately unions
    // (over-estimates) rather than trying to shrink — never clips, only over-covers.
    std::vector<SdfInstruction> prog = {
        sphere({0.f, 0.f, 0.f}, 5.f),
        sphere({0.f, 0.f, 0.f}, 1.f),
        combine(SdfOpCode::SmoothSubtract, 0.2f),
    };
    auto r = DeriveConservativeBounds(prog.data(), (uint32_t)prog.size());
    ASSERT_TRUE(r.ok);
    EXPECT_GE(r.radius, 5.f - 1e-4f);  // must still cover the larger operand
}

TEST(RecipeBounds, RoundInflatesTopOfStackRadius) {
    std::vector<SdfInstruction> prog = {
        sphere({0.f, 0.f, 0.f}, 3.f),
        roundOp(0.5f),
    };
    auto r = DeriveConservativeBounds(prog.data(), (uint32_t)prog.size());
    ASSERT_TRUE(r.ok);
    EXPECT_FLOAT_EQ(r.radius, 3.5f);
}

TEST(RecipeBounds, MultiOpCsgAndModifierChainStaysConservative) {
    // Box + Round + Sphere + SmoothUnion — exercises leaf + inflation + CSG together,
    // still fully within the whitelist.
    SdfInstruction box{};
    box.opCode = (uint8_t)SdfOpCode::Box;
    box.data[0] = 2.f; box.data[1] = 2.f; box.data[2] = 2.f;
    std::vector<SdfInstruction> prog = {
        box,
        roundOp(0.3f),
        sphere({5.f, 0.f, 0.f}, 1.f),
        combine(SdfOpCode::SmoothUnion, 0.1f),
    };
    auto r = DeriveConservativeBounds(prog.data(), (uint32_t)prog.size());
    ASSERT_TRUE(r.ok);
    EXPECT_GT(r.radius, 0.f);
}

// --- DeriveConservativeBounds: outside-whitelist opcodes MUST bail -------

TEST(RecipeBounds, TwistBendBailsOut) {
    std::vector<SdfInstruction> prog = {
        sphere({0.f, 0.f, 0.f}, 1.f),
        twist(1.0f),
    };
    auto r = DeriveConservativeBounds(prog.data(), (uint32_t)prog.size());
    EXPECT_FALSE(r.ok);
}

TEST(RecipeBounds, RepeatInfiniteBailsOut) {
    SdfInstruction repeat{};
    repeat.opCode = (uint8_t)SdfOpCode::RepeatInfinite;
    repeat.data[0] = 4.f; repeat.data[1] = 4.f; repeat.data[2] = 4.f;
    std::vector<SdfInstruction> prog = { sphere({0,0,0}, 1.f), repeat };
    auto r = DeriveConservativeBounds(prog.data(), (uint32_t)prog.size());
    EXPECT_FALSE(r.ok);
}

TEST(RecipeBounds, TransformWithScaleBailsOut) {
    SdfInstruction xf{};
    xf.opCode = (uint8_t)SdfOpCode::Transform;
    xf.data[7] = 1.f;    // invRot.w
    xf.data[8] = 2.f; xf.data[9] = 2.f; xf.data[10] = 2.f; // invScale
    xf.data[11] = 2.f;   // distScale
    std::vector<SdfInstruction> prog = { sphere({0,0,0}, 1.f), xf };
    auto r = DeriveConservativeBounds(prog.data(), (uint32_t)prog.size());
    EXPECT_FALSE(r.ok);
}

TEST(RecipeBounds, DisplacementBailsOut) {
    SdfInstruction disp{};
    disp.opCode = (uint8_t)SdfOpCode::Displacement;
    std::vector<SdfInstruction> prog = {
        sphere({0,0,0}, 1.f), sphere({0,0,0}, 0.1f), disp,
    };
    auto r = DeriveConservativeBounds(prog.data(), (uint32_t)prog.size());
    EXPECT_FALSE(r.ok);
}

TEST(RecipeBounds, PlaneIsUnboundedBailsOut) {
    SdfInstruction plane{};
    plane.opCode = (uint8_t)SdfOpCode::Plane;
    plane.data[1] = 1.f; // normal.y
    std::vector<SdfInstruction> prog = { plane };
    auto r = DeriveConservativeBounds(prog.data(), (uint32_t)prog.size());
    EXPECT_FALSE(r.ok);
}

// --- ApplyRecipeBoundsDefaults ---------------------------------------------

TEST(RecipeBounds, ApplyDefaultsPrefersDerivedForWhitelistedProgram) {
    RecipeRegistry::RecipeEntry entry{};
    entry.bytecode = { sphere({2.f, 0.f, 0.f}, 4.f) };
    auto result = ApplyRecipeBoundsDefaults(entry, /*defaultBoundRadius=*/24.f, /*defaultStepRelaxation=*/0.9f);
    EXPECT_EQ(result.boundSource, RecipeBoundsSource::Derived);
    EXPECT_FLOAT_EQ(entry.boundRadius, 4.f);
    EXPECT_FLOAT_EQ(entry.boundCenter.x, 2.f);
    EXPECT_EQ(result.relaxationSource, RecipeBoundsSource::EngineDefault);
    EXPECT_FLOAT_EQ(entry.stepRelaxation, 0.9f);
}

TEST(RecipeBounds, ApplyDefaultsFallsBackForNonWhitelistedProgram) {
    RecipeRegistry::RecipeEntry entry{};
    entry.bytecode = { sphere({0,0,0}, 1.f), twist(1.f) };
    auto result = ApplyRecipeBoundsDefaults(entry, 24.f, 0.9f);
    EXPECT_EQ(result.boundSource, RecipeBoundsSource::EngineDefault);
    EXPECT_FLOAT_EQ(entry.boundRadius, 24.f);
}

TEST(RecipeBounds, ApplyDefaultsNeverOverwritesAuthoredValues) {
    RecipeRegistry::RecipeEntry entry{};
    entry.bytecode = { sphere({0,0,0}, 1.f) };  // would derive radius=1
    entry.boundRadius = 99.f;                    // caller authored something else
    entry.stepRelaxation = 0.5f;
    auto result = ApplyRecipeBoundsDefaults(entry, 24.f, 0.9f);
    EXPECT_EQ(result.boundSource, RecipeBoundsSource::Authored);
    EXPECT_EQ(result.relaxationSource, RecipeBoundsSource::Authored);
    EXPECT_FLOAT_EQ(entry.boundRadius, 99.f);
    EXPECT_FLOAT_EQ(entry.stepRelaxation, 0.5f);
}

// --- RecipeRegistry::Register validation of the new fields ----------------

TEST(RecipeRegistry, RegisterRejectsNonPositiveBoundRadius) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{};
    e.bytecode = { sphere({0,0,0}, 1.f) };
    e.boundRadius = -1.f;
    EXPECT_EQ(reg.Register(1u, e), RecipeRegistry::RegisterResult::BadBoundRadius);
}

TEST(RecipeRegistry, RegisterRejectsOutOfRangeStepRelaxation) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{};
    e.bytecode = { sphere({0,0,0}, 1.f) };
    e.stepRelaxation = 1.5f;  // must be in (0,1]
    EXPECT_EQ(reg.Register(1u, e), RecipeRegistry::RegisterResult::BadStepRelaxation);

    RecipeRegistry::RecipeEntry e2{};
    e2.bytecode = { sphere({0,0,0}, 1.f) };
    e2.stepRelaxation = -0.2f;
    EXPECT_EQ(reg.Register(2u, e2), RecipeRegistry::RegisterResult::BadStepRelaxation);
}

TEST(RecipeRegistry, RegisterAcceptsZeroBoundsAsEngineDefaultSentinel) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{};
    e.bytecode = { sphere({0,0,0}, 1.f) };
    // boundRadius/stepRelaxation left at 0 — "engine default", not an error.
    EXPECT_EQ(reg.Register(1u, e), RecipeRegistry::RegisterResult::Ok);
}

TEST(RecipeRegistry, RegisterAcceptsValidAuthoredBounds) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry e{};
    e.bytecode = { sphere({0,0,0}, 1.f) };
    e.boundCenter = glm::vec3(1.f, 2.f, 3.f);
    e.boundRadius = 10.f;
    e.stepRelaxation = 1.0f;  // upper bound of (0,1] is inclusive
    ASSERT_EQ(reg.Register(1u, e), RecipeRegistry::RegisterResult::Ok);
    const auto* got = reg.Get(1u);
    ASSERT_NE(got, nullptr);
    EXPECT_FLOAT_EQ(got->boundRadius, 10.f);
    EXPECT_FLOAT_EQ(got->stepRelaxation, 1.0f);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
