#include <gtest/gtest.h>
#include "Recipe/RecipeBounds.h"
#include "Recipe/RecipeServices.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/SdfRecipeEval.h"
#include <array>
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

struct BoundAnalysis {
    RecipeRegistry::RecipeEntry entry;
    RecipeRegistrationAnalysis result;
};

BoundAnalysis AnalyzeBounds(const std::vector<SdfInstruction>& program) {
    BoundAnalysis analysis;
    analysis.entry.bytecode = program;
    analysis.result = AnalyzeRecipeRegistration(analysis.entry, 24.0f, 0.9f);
    return analysis;
}

} // namespace

// --- Kernel analysis service: conservative bound contract -----------------

TEST(RecipeBounds, SingleSphereBoundIsExact) {
    std::vector<SdfInstruction> prog = { sphere({1.f, 2.f, 3.f}, 5.f) };
    const auto r = AnalyzeBounds(prog);
    ASSERT_EQ(r.result.boundSource, RecipeRegistrationAnalysis::BoundSource::Derived);
    EXPECT_FLOAT_EQ(r.entry.boundCenter.x, 1.f);
    EXPECT_FLOAT_EQ(r.entry.boundCenter.y, 2.f);
    EXPECT_FLOAT_EQ(r.entry.boundCenter.z, 3.f);
    EXPECT_FLOAT_EQ(r.entry.boundRadius, 5.f);
}

TEST(RecipeBounds, UnionOfDisjointSpheresBoundsBoth) {
    // Two unit spheres far apart — the union bound must actually contain both.
    std::vector<SdfInstruction> prog = {
        sphere({0.f, 0.f, 0.f}, 1.f),
        sphere({10.f, 0.f, 0.f}, 1.f),
        combine(SdfOpCode::Union),
    };
    const auto r = AnalyzeBounds(prog);
    ASSERT_EQ(r.result.boundSource, RecipeRegistrationAnalysis::BoundSource::Derived);
    // Every point of both source spheres must lie within [center, radius].
    EXPECT_LE(glm::length((glm::vec3(0,0,0)) - r.entry.boundCenter) + 1.f, r.entry.boundRadius + 1e-4f);
    EXPECT_LE(glm::length((glm::vec3(10,0,0)) - r.entry.boundCenter) + 1.f, r.entry.boundRadius + 1e-4f);
}

TEST(RecipeBounds, SmoothUnionBoundContainsSampledFieldOutsideExtent) {
    // The blend band is part of the occupied extent.  Sample just outside the derived
    // sphere in several directions; every sample must be outside the actual smooth field.
    std::vector<SdfInstruction> prog = {
        sphere({0.f, 0.f, 0.f}, 1.f),
        sphere({3.f, 0.f, 0.f}, 1.f),
        combine(SdfOpCode::SmoothUnion, 0.75f),
    };
    const auto analysis = AnalyzeBounds(prog);
    ASSERT_EQ(analysis.result.boundSource, RecipeRegistrationAnalysis::BoundSource::Derived);
    const struct { glm::vec3 center; float radius; } r{analysis.entry.boundCenter, analysis.entry.boundRadius};
    const std::array<glm::vec3, 6> directions = {
        glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0),
        glm::vec3(0, 1, 0), glm::vec3(0, -1, 0),
        glm::normalize(glm::vec3(1, 1, 1)), glm::normalize(glm::vec3(-1, 1, -1)),
    };
    for (const glm::vec3 direction : directions) {
        const glm::vec3 sample = r.center + direction * (r.radius + 1e-3f);
        EXPECT_GT(evalRecipe(prog.data(), static_cast<uint32_t>(prog.size()), sample), 0.0f)
            << "sample=" << sample.x << "," << sample.y << "," << sample.z;
    }
}

TEST(RecipeBounds, SmoothSubtractBoundIsConservativeUnion) {
    // Subtract's TRUE extent is a subset of A alone, but this helper deliberately unions
    // (over-estimates) rather than trying to shrink — never clips, only over-covers.
    std::vector<SdfInstruction> prog = {
        sphere({0.f, 0.f, 0.f}, 5.f),
        sphere({0.f, 0.f, 0.f}, 1.f),
        combine(SdfOpCode::SmoothSubtract, 0.2f),
    };
    const auto r = AnalyzeBounds(prog);
    ASSERT_EQ(r.result.boundSource, RecipeRegistrationAnalysis::BoundSource::Derived);
    EXPECT_GE(r.entry.boundRadius, 5.f - 1e-4f);  // must still cover the larger operand
}

TEST(RecipeBounds, RoundInflatesTopOfStackRadius) {
    std::vector<SdfInstruction> prog = {
        sphere({0.f, 0.f, 0.f}, 3.f),
        roundOp(0.5f),
    };
    const auto r = AnalyzeBounds(prog);
    ASSERT_EQ(r.result.boundSource, RecipeRegistrationAnalysis::BoundSource::Derived);
    EXPECT_FLOAT_EQ(r.entry.boundRadius, 3.5f);
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
    const auto r = AnalyzeBounds(prog);
    ASSERT_EQ(r.result.boundSource, RecipeRegistrationAnalysis::BoundSource::Derived);
    EXPECT_GT(r.entry.boundRadius, 0.f);
}

// --- Kernel analysis service: unproved programs use the engine default ------

TEST(RecipeBounds, TwistBendBailsOut) {
    std::vector<SdfInstruction> prog = {
        sphere({0.f, 0.f, 0.f}, 1.f),
        twist(1.0f),
    };
    const auto r = AnalyzeBounds(prog);
    EXPECT_EQ(r.result.boundSource, RecipeRegistrationAnalysis::BoundSource::EngineDefault);
}

TEST(RecipeBounds, RepeatInfiniteBailsOut) {
    SdfInstruction repeat{};
    repeat.opCode = (uint8_t)SdfOpCode::RepeatInfinite;
    repeat.data[0] = 4.f; repeat.data[1] = 4.f; repeat.data[2] = 4.f;
    std::vector<SdfInstruction> prog = { sphere({0,0,0}, 1.f), repeat };
    const auto r = AnalyzeBounds(prog);
    EXPECT_EQ(r.result.boundSource, RecipeRegistrationAnalysis::BoundSource::EngineDefault);
}

TEST(RecipeBounds, TransformWithScaleBailsOut) {
    SdfInstruction xf{};
    xf.opCode = (uint8_t)SdfOpCode::Transform;
    xf.data[7] = 1.f;    // invRot.w
    xf.data[8] = 2.f; xf.data[9] = 2.f; xf.data[10] = 2.f; // invScale
    xf.data[11] = 2.f;   // distScale
    std::vector<SdfInstruction> prog = { sphere({0,0,0}, 1.f), xf };
    const auto r = AnalyzeBounds(prog);
    EXPECT_EQ(r.result.boundSource, RecipeRegistrationAnalysis::BoundSource::EngineDefault);
}

TEST(RecipeBounds, DisplacementBailsOut) {
    SdfInstruction disp{};
    disp.opCode = (uint8_t)SdfOpCode::Displacement;
    std::vector<SdfInstruction> prog = {
        sphere({0,0,0}, 1.f), sphere({0,0,0}, 0.1f), disp,
    };
    const auto r = AnalyzeBounds(prog);
    EXPECT_EQ(r.result.boundSource, RecipeRegistrationAnalysis::BoundSource::EngineDefault);
}

TEST(RecipeBounds, PlaneIsUnboundedBailsOut) {
    SdfInstruction plane{};
    plane.opCode = (uint8_t)SdfOpCode::Plane;
    plane.data[1] = 1.f; // normal.y
    std::vector<SdfInstruction> prog = { plane };
    const auto r = AnalyzeBounds(prog);
    EXPECT_EQ(r.result.boundSource, RecipeRegistrationAnalysis::BoundSource::EngineDefault);
}

// --- Registration defaults are applied from the service analysis ------------

TEST(RecipeBounds, ApplyDefaultsPrefersDerivedForWhitelistedProgram) {
    RecipeRegistry::RecipeEntry entry{};
    entry.bytecode = { sphere({2.f, 0.f, 0.f}, 4.f) };
    auto result = AnalyzeRecipeRegistration(entry, /*defaultBoundRadius=*/24.f, /*defaultStepRelaxation=*/0.9f);
    EXPECT_EQ(result.boundSource, RecipeRegistrationAnalysis::BoundSource::Derived);
    EXPECT_FLOAT_EQ(entry.boundRadius, 4.f);
    EXPECT_FLOAT_EQ(entry.boundCenter.x, 2.f);
    EXPECT_FLOAT_EQ(entry.stepRelaxation, 0.9f);
}

TEST(RecipeBounds, ApplyDefaultsFallsBackForNonWhitelistedProgram) {
    RecipeRegistry::RecipeEntry entry{};
    entry.bytecode = { sphere({0,0,0}, 1.f), twist(1.f) };
    auto result = AnalyzeRecipeRegistration(entry, 24.f, 0.9f);
    EXPECT_EQ(result.boundSource, RecipeRegistrationAnalysis::BoundSource::EngineDefault);
    EXPECT_FLOAT_EQ(entry.boundRadius, 24.f);
}

TEST(RecipeBounds, ApplyDefaultsNeverOverwritesAuthoredValues) {
    RecipeRegistry::RecipeEntry entry{};
    entry.bytecode = { sphere({0,0,0}, 1.f) };  // would derive radius=1
    entry.boundRadius = 99.f;                    // caller authored something else
    entry.stepRelaxation = 0.5f;
    auto result = AnalyzeRecipeRegistration(entry, 24.f, 0.9f);
    EXPECT_EQ(result.boundSource, RecipeRegistrationAnalysis::BoundSource::Authored);
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

// A document authored about the bake-grid centre, not the origin: the sampled bound must follow
// the body (a fixed origin-centred sphere would cull it entirely).
TEST(RecipeSampledBounds, FollowsABodyAuthoredAboutTheGridCentre) {
    const std::array<SdfInstruction, 1> program{sphere({32.0f, 32.0f, 32.0f}, 12.0f)};
    const auto bounds = DeriveSampledBounds(program.data(), 1, {}, 64);
    ASSERT_TRUE(bounds.ok);
    EXPECT_NEAR(bounds.center.x, 32.0f, 2.0f);
    EXPECT_NEAR(bounds.center.y, 32.0f, 2.0f);
    EXPECT_NEAR(bounds.center.z, 32.0f, 2.0f);
    EXPECT_GE(bounds.radius, 12.0f * 1.7320508f * 0.9f);  // contains the sphere's box corners' sphere
    EXPECT_LE(bounds.radius, 30.0f);                      // and is not the whole sampled domain
}

TEST(RecipeSampledBounds, EmptyFieldAndInvokeRecipeAreRefused) {
    const std::array<SdfInstruction, 1> far{sphere({500.0f, 0.0f, 0.0f}, 1.0f)};
    EXPECT_FALSE(DeriveSampledBounds(far.data(), 1, {}, 64).ok);
    SdfInstruction invoke{};
    invoke.opCode = static_cast<uint8_t>(SdfOpCode::InvokeRecipe);
    EXPECT_FALSE(DeriveSampledBounds(&invoke, 1, {}, 64).ok);
}
