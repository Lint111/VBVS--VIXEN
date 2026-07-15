#include <gtest/gtest.h>
#include "SdfBake.h"
#include "Recipe/SdfInstruction.h"
#include <glm/glm.hpp>
#include <array>
#include <span>

using namespace Vixen::SVO;

static Recipe::SdfInstruction sphereInstr(glm::vec3 c, float r) {
    Recipe::SdfInstruction in{};
    in.opCode  = (uint8_t)Recipe::SdfOpCode::Sphere;
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
    return in;
}

static Recipe::SdfInstruction readParamInstr(int idx) {
    Recipe::SdfInstruction in{};
    in.opCode    = (uint8_t)Recipe::SdfOpCode::ReadParam;
    in.paramMask = 1;
    in.data[0]   = static_cast<float>(idx);
    return in;
}

static Recipe::SdfInstruction mathSubInstr() {
    Recipe::SdfInstruction in{};
    in.opCode = (uint8_t)Recipe::SdfOpCode::MathSub;
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

// ---------------------------------------------------------------------------
// Recipe-Parameterization M3 Task 10 — bake-time snapshot semantics for a ReadParam
// recipe. Baking produces a static voxel grid, so a "dynamic" runtime param becomes
// whatever value was current AT BAKE TIME (a snapshot, not dynamic) — these two cases
// prove BOTH of the plan's documented snapshot sources actually work:
//   (1) a call site that doesn't care about ReadParam passes no params array at all
//       (the default empty span) and gets evalRecipe's own well-defined zero-fill
//       fail-safe — NOT garbage/unset memory, NOT a crash.
//   (2) a call site baking a REAL ReadParam recipe passes an explicit snapshot value
//       and the baked geometry reflects that exact value, matching what
//       evalRecipe(..., params) would produce live for the same params array.
// ---------------------------------------------------------------------------
TEST(RecipeBake, ReadParamBakeDefaultsToZeroFillFailSafe) {
    // sd = sphereSD(baseRadius) - params[0]. With NO params array supplied at all (the
    // default empty span), evalRecipe's own out-of-range fail-safe reads params[0] as 0.0f
    // — so the baked geometry must equal a plain (non-parameterized) sphere of baseRadius,
    // proving the empty-span default is a well-defined, correct fallback, not garbage.
    const int n = 64;
    const glm::vec3 c(32, 32, 32);
    const float baseRadius = 10.0f, band = 2.5f;

    Recipe::SdfInstruction prog[] = {
        sphereInstr(glm::vec3(0, 0, 0), baseRadius), readParamInstr(0), mathSubInstr()
    };
    // No params argument supplied — exercises the default std::span<const float>{} path.
    auto baked = BakeRecipeInstructionsToSdfWorld(prog, 3, c, n, band, 3);

    Recipe::SdfInstruction plainProg[] = { sphereInstr(glm::vec3(0, 0, 0), baseRadius) };
    auto plainBaked = BakeRecipeInstructionsToSdfWorld(plainProg, 1, c, n, band, 3);

    int checked = 0;
    for (glm::vec3 p : { glm::vec3(32, 32, 22), glm::vec3(32, 32, 32),
                         glm::vec3(38, 32, 32), glm::vec3(20, 20, 20) }) {
        auto a = baked.sampleStored(p);
        auto b = plainBaked.sampleStored(p);
        ASSERT_EQ(a.has_value(), b.has_value())
            << "allocation differs at " << p.x << "," << p.y << "," << p.z;
        if (a.has_value()) {
            EXPECT_NEAR(a.value(), b.value(), 1e-4f);
            ++checked;
        }
    }
    EXPECT_GT(checked, 0);
}

TEST(RecipeBake, ReadParamBakeWithExplicitSnapshotMatchesEvalRecipe) {
    // Same program, but this call site DOES care about ReadParam and passes a real
    // bake-time snapshot (params[0] = 4.0f) — the baked geometry must reflect that exact
    // value: sd = sphereSD(baseRadius) - 4.0f, i.e. an effective radius of baseRadius+4.0f.
    // Verified two ways: (a) against evalRecipe() called directly with the same params
    // array (the ground truth this whole plan is about), and (b) against a plain sphere
    // baked at the equivalent combined radius (an independent geometric cross-check).
    const int n = 64;
    const glm::vec3 c(32, 32, 32);
    const float baseRadius = 10.0f, band = 2.5f;
    const std::array<float, 1> snapshot = { 4.0f };

    Recipe::SdfInstruction prog[] = {
        sphereInstr(glm::vec3(0, 0, 0), baseRadius), readParamInstr(0), mathSubInstr()
    };
    auto baked = BakeRecipeInstructionsToSdfWorld(prog, 3, c, n, band, 3,
                                                   std::span<const float>(snapshot));

    Recipe::SdfInstruction equivProg[] = { sphereInstr(glm::vec3(0, 0, 0), baseRadius + snapshot[0]) };
    auto equivBaked = BakeRecipeInstructionsToSdfWorld(equivProg, 1, c, n, band, 3);

    int checked = 0;
    for (glm::vec3 p : { glm::vec3(32, 32, 18), glm::vec3(32, 32, 32),
                         glm::vec3(42, 32, 32), glm::vec3(20, 20, 20) }) {
        // (a) ground truth: evalRecipe called directly with the identical params array.
        const glm::vec3 gridToObject = p - c;
        const float direct = Recipe::evalRecipe(prog, 3, gridToObject, std::span<const float>(snapshot));

        // (b) baked-world sample.
        auto bakedSample = baked.sampleStored(p);
        // (c) independent cross-check: an equivalent plain sphere baked at the combined radius.
        auto equivSample = equivBaked.sampleStored(p);

        ASSERT_EQ(bakedSample.has_value(), equivSample.has_value())
            << "allocation differs at " << p.x << "," << p.y << "," << p.z;
        if (bakedSample.has_value()) {
            EXPECT_NEAR(bakedSample.value(), direct, 1e-3f)
                << "baked value diverges from direct evalRecipe(..., snapshot) at "
                << p.x << "," << p.y << "," << p.z;
            EXPECT_NEAR(bakedSample.value(), equivSample.value(), 1e-4f)
                << "baked ReadParam value diverges from the equivalent-radius plain sphere at "
                << p.x << "," << p.y << "," << p.z;
            ++checked;
        }
    }
    EXPECT_GT(checked, 0);
}
