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
// Params are ZERO-FILLED, matching the existing harness convention
// (test_recipe_glsl_numerical_parity's own zero-filled params[6] note);
// the schema carries an explicit paramsConvention field so a future
// param-exercising tier extends rather than breaks it.

#include <gtest/gtest.h>
#include "Recipe/RecipeParityCorpus.h"
#include "Recipe/SdfInstruction.h"
#include "Recipe/SdfRecipeEval.h"

#include <cstdlib>
#include <fstream>
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

    std::ostringstream j;
    j << "{\n"
      << "  \"schema\": 1,\n"
      << "  \"source\": \"VIXEN SdfRecipeEval (test_recipe_parity_fixture_export)\",\n"
      << "  \"opcodeEnum\": \"SDFOpCode (Yeroket SDFInstruction.cs, mirrored in SdfOpCodes.g.h)\",\n"
      << "  \"paramsConvention\": \"zero-filled (matches the GLSL/GPU parity harness)\",\n"
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
    j << "  ]\n}\n";

    std::ofstream f(outPath);
    ASSERT_TRUE(f.good()) << "cannot open " << outPath;
    f << j.str();
    f.close();
    ASSERT_TRUE(f.good()) << "write failed for " << outPath;

    RecordProperty("programs", static_cast<int>(corpus.size()));
    RecordProperty("points", static_cast<int>(points.size()));
}
