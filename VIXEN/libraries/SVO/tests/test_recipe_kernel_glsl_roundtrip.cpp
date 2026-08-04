// Recipe-compiler uplift S1 step 4 (task #26, 2026-08-04): the CPU-side
// compile-round-trip gate for the KERNEL-FRAMEWORK-emitted GLSL face.
//
// The kernel repo's new GlslAstVisitor lowers recipe bytecode (and any
// [VMKernel] body) to composable `float sdfRecipe_<id>(vec3 p, float
// params[6])` functions — the same signature contract VIXEN's own local
// walker (SdfRecipeCodegenGlsl.h, the eventual retirement target) emits.
// This test proves those kernel-emitted functions round-trip VIXEN's ACTUAL
// glslang-backed ShaderCompiler — the real acceptance criterion, not a
// generic glslang: composed exactly the way production composes (version
// header + SdfCoreKernels.glsl + function + wrapper main), one program per
// compile, pure host-side, no GPU.
//
// Env-gated like the fixture exporter: without VIXEN_S1_EMITTED_GLSL=<json>
// it SKIPS loudly (never a silent pass). The JSON is the cross-repo handoff
// (schema 1): {"schema":1, "emitter":"...", "functions":[{"name":"<corpus
// program name>", "text":"<full sdfRecipe function>"}]} — produced by the
// kernel side's real Lower<T> -> GlslAstVisitor pipeline. All failures are
// collected and reported per-function (not fail-fast), so one run returns
// the complete verdict.

#include <gtest/gtest.h>
#include "ShaderCompiler.h"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef SDF_CORE_KERNELS_GLSL_PATH
#  error "SDF_CORE_KERNELS_GLSL_PATH must be defined via CMake compile_definitions"
#endif

namespace {

// Mirrors test_recipe_glsl_numerical_parity's ComposeComputeShader (the
// production composition shape), parameterized on the emitted function's
// declared name — the kernel side owns the <id> choice, the wrapper calls
// whatever the function declares.
std::string ComposeComputeShader(const std::string& sdfCoreGlsl,
                                  const std::string& emittedFieldFn,
                                  const std::string& fnName) {
    std::ostringstream ss;
    ss << "#version 450\n";
    ss << sdfCoreGlsl << "\n";
    ss << emittedFieldFn << "\n";
    ss << "layout(local_size_x = 64) in;\n"
       << "layout(set = 0, binding = 0, std430) readonly buffer InPoints { vec4 points[]; };\n"
       << "layout(set = 0, binding = 1, std430) writeonly buffer OutValues { float values[]; };\n"
       << "layout(set = 0, binding = 2, std430) readonly buffer InParams { float params[6]; };\n"
       << "void main() {\n"
       << "    if (gl_GlobalInvocationID.x >= points.length()) return;\n"
       << "    float p[6] = float[6](params[0], params[1], params[2], params[3], params[4], params[5]);\n"
       << "    values[gl_GlobalInvocationID.x] = " << fnName
       << "(points[gl_GlobalInvocationID.x].xyz, p);\n"
       << "}\n";
    return ss.str();
}

} // namespace

TEST(RecipeKernelGlslRoundtrip, EmittedFunctionsCompileThroughRealShaderCompiler) {
    const char* jsonPath = std::getenv("VIXEN_S1_EMITTED_GLSL");
    if (!jsonPath || !jsonPath[0]) {
        GTEST_SKIP() << "set VIXEN_S1_EMITTED_GLSL=<s1-emitted-glsl.json> to run the round-trip";
    }

    std::ifstream jf(jsonPath);
    ASSERT_TRUE(jf.good()) << "cannot open " << jsonPath;
    nlohmann::json doc = nlohmann::json::parse(jf, nullptr, /*allow_exceptions=*/false);
    ASSERT_FALSE(doc.is_discarded()) << "JSON parse failed for " << jsonPath;
    ASSERT_TRUE(doc.contains("functions") && doc["functions"].is_array())
        << "schema violation: top-level 'functions' array required";

    std::ifstream kernelFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_TRUE(kernelFile.good())
        << "Cannot open vendored GLSL: " << SDF_CORE_KERNELS_GLSL_PATH;
    std::ostringstream kss;
    kss << kernelFile.rdbuf();
    const std::string sdfCoreGlsl = kss.str();

    const std::regex fnNameRe(R"(float\s+(sdfRecipe_\w+)\s*\()");

    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::GLSL;

    size_t total = 0, failed = 0;
    for (const auto& fn : doc["functions"]) {
        const std::string name = fn.value("name", "<unnamed>");
        const std::string text = fn.value("text", "");
        SCOPED_TRACE("emitted function: " + name);
        ++total;

        std::smatch m;
        if (text.empty() || !std::regex_search(text, m, fnNameRe)) {
            ADD_FAILURE() << "'" << name << "': no `float sdfRecipe_<id>(` declaration found "
                          << "in emitted text (" << text.size() << " chars)";
            ++failed;
            continue;
        }

        const std::string shaderSrc = ComposeComputeShader(sdfCoreGlsl, text, m[1].str());
        auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute,
                                        shaderSrc, "main", opts);
        if (!compOut.success || compOut.spirv.empty()) {
            ADD_FAILURE() << "'" << name << "' failed VIXEN ShaderCompiler round-trip:\n"
                          << compOut.GetFullLog()
                          << "\n--- emitted function ---\n" << text;
            ++failed;
        }
    }

    RecordProperty("total", static_cast<int>(total));
    RecordProperty("failed", static_cast<int>(failed));
    std::cout << "[RecipeKernelGlslRoundtrip] compiled " << (total - failed)
              << "/" << total << " kernel-emitted functions through the real ShaderCompiler"
              << std::endl;
    ASSERT_GT(total, 0u) << "empty functions array — nothing was gated";
}
