// Recipe parity fixture EXPORTER (task #26 cross-repo gate, 2026-08-04).
//
// The kernel-side recipe front-end (Yeroket-Fantasy, RecipeInstructionLowering)
// gates on NUMERIC parity against this repo's CPU reference interpreter
// (SdfRecipeEval.h::evalRecipe — the same ground truth the GLSL/GPU parity
// tests here already trust). This test dumps that ground truth as a JSON
// fixture the kernel tests consume: for every ParityCorpus program, the
// instruction stream (full 132-B fidelity: opCode/inputMask/paramMask/
// data[32]) plus evalRecipe values over the standard sample grid.
//
// Env-gated like the repo's recapture tests: without
// VIXEN_EXPORT_RECIPE_PARITY_FIXTURES=<output.json> it SKIPS loudly (never a
// silent pass); with it, it writes the fixture and asserts the write.
//
// SCHEMA 2 (task #26 S1): two tiers. Tier 1 ("programs") keeps schema 1's
// shape verbatim — ZERO-FILLED params, matching the GLSL/GPU parity harness
// convention. Tier 2 ("paramsTier") re-evaluates EVERY program under ONE
// shared, documented, nonzero all-distinct params array — the non-vacuous
// gate for ReadParam/ReadParamFloat3 (a hardcoded-zero lowering matches
// tier 1 but diverges here) and the compile-once/re-dispatch-different-
// params property proof. Emitting ALL programs (not just ReadParam users)
// is deliberate: programs that never read params must match tier 1
// EXACTLY, a built-in consistency invariant consumers should assert.

#include <gtest/gtest.h>
#include "Recipe/RecipeParityCorpus.h"
#include "Recipe/SdfInstruction.h"
#include "Recipe/SdfRecipeEval.h"

#include <cstdlib>
#include <fstream>
#include <span>
#include <sstream>
#include <vector>

#include <glm/glm.hpp>

namespace {

// Same grid as test_recipe_glsl_numerical_parity's BuildSamplePoints: 5x5x5
// over [-3,3] step 1.5, plus near-surface probes. Kept locally (that builder
// is file-static there); the fixture embeds the points, so drift between the
// two builders cannot silently skew a comparison — consumers use the
// embedded points, never a re-derivation.
std::vector<glm::vec3> BuildFixtureSamplePoints() {
    std::vector<glm::vec3> pts;
    pts.reserve(125 + 8);
    for (int xi = 0; xi < 5; ++xi)
        for (int yi = 0; yi < 5; ++yi)
            for (int zi = 0; zi < 5; ++zi)
                pts.emplace_back(-3.0f + xi * 1.5f,
                                 -3.0f + yi * 1.5f,
                                 -3.0f + zi * 1.5f);
    const float nearR[8] = {0.9f, 0.99f, 1.0f, 1.01f, 1.1f, 1.5f, 2.0f, 2.5f};
    for (float r : nearR) pts.emplace_back(r, 0.0f, 0.0f);
    return pts;
}

void AppendFloat(std::ostringstream& out, float v) {
    // max_digits10 round-trip precision — the consumer compares numerically.
    char buf[48];
    snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    out << buf;
}

} // namespace

TEST(RecipeParityFixtureExport, ExportsJsonWhenRequested) {
    const char* outPath = std::getenv("VIXEN_EXPORT_RECIPE_PARITY_FIXTURES");
    if (!outPath || !outPath[0]) {
        GTEST_SKIP() << "set VIXEN_EXPORT_RECIPE_PARITY_FIXTURES=<output.json> to export";
    }

    using Vixen::SVO::Recipe::SdfInstruction;
    const auto corpus = Vixen::SVO::Recipe::ParityCorpus::GetAll();
    ASSERT_FALSE(corpus.empty());
    const auto points = BuildFixtureSamplePoints();

    // Tier-2 params: nonzero, all-distinct, sign-varied — six floats matching
    // the reference walker's params[6] signature (float3 reads at idx 0/1,
    // scalar reads at idx 0..5 all resolve to distinct values, so wrong-index
    // reads diverge as loudly as hardcoded zeros).
    const float kTierParams[6] = {1.5f, -2.25f, 7.5f, 0.75f, 42.0f, -0.5f};

    std::ostringstream j;
    j << "{\n"
      << "  \"schema\": 2,\n"
      << "  \"source\": \"VIXEN SdfRecipeEval (test_recipe_parity_fixture_export)\",\n"
      << "  \"opcodeEnum\": \"SDFOpCode (Yeroket SDFInstruction.cs, mirrored in SdfOpCodes.g.h)\",\n"
      << "  \"paramsConvention\": \"tier 1 (programs): zero-filled; tier 2 (paramsTier): the shared nonzero array below\",\n"
      << "  \"points\": [";
    for (size_t i = 0; i < points.size(); ++i) {
        if (i) j << ",";
        j << "[";
        AppendFloat(j, points[i].x); j << ",";
        AppendFloat(j, points[i].y); j << ",";
        AppendFloat(j, points[i].z); j << "]";
    }
    j << "],\n  \"programs\": [\n";

    for (size_t pi = 0; pi < corpus.size(); ++pi) {
        const auto& prog = corpus[pi];
        j << "    {\"name\": \"" << prog.name << "\", \"instructions\": [";
        for (size_t ii = 0; ii < prog.program.size(); ++ii) {
            const SdfInstruction& in = prog.program[ii];
            if (ii) j << ",";
            j << "{\"opCode\": " << static_cast<uint32_t>(in.opCode)
              << ", \"inputMask\": " << static_cast<uint32_t>(in.inputMask)
              << ", \"paramMask\": " << static_cast<uint32_t>(in.paramMask)
              << ", \"data\": [";
            for (int d = 0; d < 32; ++d) {
                if (d) j << ",";
                AppendFloat(j, in.data[d]);
            }
            j << "]}";
        }
        j << "], \"values\": [";
        for (size_t vi = 0; vi < points.size(); ++vi) {
            if (vi) j << ",";
            const float v = Vixen::SVO::Recipe::evalRecipe(
                prog.program.data(), static_cast<uint32_t>(prog.program.size()),
                points[vi]);
            AppendFloat(j, v);
        }
        j << "]}" << (pi + 1 < corpus.size() ? "," : "") << "\n";
    }
    j << "  ],\n";

    // Tier 2: every program re-evaluated under the shared nonzero params array
    // (instruction streams already emitted above — consumers key by name).
    j << "  \"paramsTier\": {\n    \"params\": [";
    for (int pi2 = 0; pi2 < 6; ++pi2) {
        if (pi2) j << ",";
        AppendFloat(j, kTierParams[pi2]);
    }
    j << "],\n    \"programs\": [\n";
    for (size_t pi = 0; pi < corpus.size(); ++pi) {
        const auto& prog = corpus[pi];
        j << "      {\"name\": \"" << prog.name << "\", \"values\": [";
        for (size_t vi = 0; vi < points.size(); ++vi) {
            if (vi) j << ",";
            const float v = Vixen::SVO::Recipe::evalRecipe(
                prog.program.data(), static_cast<uint32_t>(prog.program.size()),
                points[vi], std::span<const float>(kTierParams, 6));
            AppendFloat(j, v);
        }
        j << "]}" << (pi + 1 < corpus.size() ? "," : "") << "\n";
    }
    j << "    ]\n  }\n}\n";

    std::ofstream f(outPath);
    ASSERT_TRUE(f.good()) << "cannot open " << outPath;
    f << j.str();
    f.close();
    ASSERT_TRUE(f.good()) << "write failed for " << outPath;

    RecordProperty("programs", static_cast<int>(corpus.size()));
    RecordProperty("points", static_cast<int>(points.size()));
}

// D1 extension (task #33, W-KGLSL): the hot-cold demo's recipes are NOT ParityCorpus
// programs — confirmed by inspection (BuildRenderGraph.cpp's VIXEN_RECIPE_HOT_COLD_DEMO
// branch builds each recipe's bytecode inline, one Sphere opcode, never touching
// ParityCorpus). This dumps that SAME bytecode construction directly (no app boot, no
// RecipeRegistry needed — a standalone SdfInstruction is sufficient, exactly what the
// registry would have stored), keyed by real recipeId (the D1/D2 ruling: demo recipes have
// no meaningful name, so "programs" here are named "recipe_<id>"), covering the full
// HOT_RECIPES+COLD_RECIPES default range (recipeId 2..7) plus the same env-var overrides
// BuildRenderGraph.cpp honors, so this stays correct if the demo's mix is overridden.
// [Explicit]: never runs in a normal suite pass, matches ExportsJsonWhenRequested's own
// env-gate discipline (VIXEN_EXPORT_HOTCOLD_RECIPE_FIXTURES=<path>).
TEST(RecipeParityFixtureExport, ExportsHotColdDemoRecipesWhenRequested) {
    const char* outPath = std::getenv("VIXEN_EXPORT_HOTCOLD_RECIPE_FIXTURES");
    if (!outPath || !outPath[0]) {
        GTEST_SKIP() << "set VIXEN_EXPORT_HOTCOLD_RECIPE_FIXTURES=<output.json> to export";
    }

    auto envOr = [](const char* name, int fallback) -> int {
        const char* v = std::getenv(name);
        if (!v) return fallback;
        int parsed = std::atoi(v);
        return parsed > 0 ? parsed : fallback;
    };
    const int hotRecipes  = envOr("VIXEN_RECIPE_HOT_COLD_DEMO_HOT_RECIPES", 3);
    const int coldRecipes = envOr("VIXEN_RECIPE_HOT_COLD_DEMO_COLD_RECIPES", 3);
    constexpr float kSpacingX = 30.0f;
    constexpr float kBaseZ    = 30.0f;

    using Vixen::SVO::Recipe::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;

    std::ostringstream j;
    j << "{\n  \"schema\": 1,\n"
      << "  \"source\": \"VIXEN hot-cold demo bytecode (test_recipe_parity_fixture_export, "
      << "mirrors BuildRenderGraph.cpp's registerAndSeed)\",\n  \"recipes\": [\n";

    const int total = hotRecipes + coldRecipes;
    uint32_t recipeId = 2;  // matches BuildRenderGraph.cpp's nextRecipeId start
    for (int idx = 0; idx < total; ++idx, ++recipeId) {
        const float centerX = 40.0f + static_cast<float>(idx) * kSpacingX;
        SdfInstruction sphere{};
        sphere.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
        sphere.data[0] = centerX; sphere.data[1] = 64.0f; sphere.data[2] = kBaseZ;
        sphere.data[3] = 6.0f;

        j << "    {\"recipeId\": " << recipeId << ", \"instructions\": [{"
          << "\"opCode\": " << static_cast<uint32_t>(sphere.opCode)
          << ", \"inputMask\": " << static_cast<uint32_t>(sphere.inputMask)
          << ", \"paramMask\": " << static_cast<uint32_t>(sphere.paramMask)
          << ", \"data\": [";
        for (int d = 0; d < 32; ++d) {
            if (d) j << ",";
            AppendFloat(j, sphere.data[d]);
        }
        j << "]}]}" << (idx + 1 < total ? "," : "") << "\n";
    }
    j << "  ]\n}\n";

    std::ofstream f(outPath);
    ASSERT_TRUE(f.good()) << "cannot open " << outPath;
    f << j.str();
    f.close();
    ASSERT_TRUE(f.good()) << "write failed for " << outPath;

    RecordProperty("recipes", total);
}
