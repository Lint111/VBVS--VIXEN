#include <gtest/gtest.h>
#include "Recipe/SdfRecipeCodegen.h"
#include "ShaderCompiler.h"
#include <fstream>
#include <sstream>

using namespace Vixen::SVO;
using namespace ShaderManagement;

// SDF_CORE_KERNELS_HLSL_PATH is injected by CMake (same pattern as test_hlsl_ingestion.cpp).
#ifndef SDF_CORE_KERNELS_HLSL_PATH
#  error "SDF_CORE_KERNELS_HLSL_PATH must be defined via CMake compile_definitions"
#endif

// Helper: build a Sphere instruction (mirrors test_recipe_bake.cpp helper style)
static Recipe::SdfInstruction sphere(float cx, float cy, float cz, float r) {
    Recipe::SdfInstruction in{};
    in.opCode  = static_cast<uint8_t>(Recipe::SdfOpCode::Sphere);
    in.data[0] = cx; in.data[1] = cy; in.data[2] = cz; in.data[3] = r;
    return in;
}

static Recipe::SdfInstruction unionOp() {
    Recipe::SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Union);
    return in;
}

TEST(RecipeCodegen, EmitsCompilableProceduralShader) {
    // Read the vendored generated HLSL kernels (same as test_hlsl_ingestion.cpp).
    std::ifstream f(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(f.good()) << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string core = ss.str();

    // sphere∪sphere recipe: two spheres at ±1 on X, radius 1.
    Recipe::SdfInstruction prog[] = { sphere(-1, 0, 0, 1), sphere(1, 0, 0, 1), unionOp() };
    std::string src = Recipe::EmitProceduralComputeShader(prog, 3, core);

    // Verify straight-line emission (not a for-loop over instructions).
    EXPECT_NE(src.find("SdfCore_Sphere(p, float3(-1"), std::string::npos)
        << "Expected inlined Sphere call for sphere at -1; src:\n" << src;
    EXPECT_NE(src.find("SdfCore_Union(t0, t1)"), std::string::npos)
        << "Expected inlined Union(t0, t1); src:\n" << src;

    // Compile through the HLSL path → valid SPIR-V.
    ShaderCompiler compiler;
    CompilationOptions opts;
    opts.sourceLanguage = CompilationOptions::SourceLanguage::HLSL;
    opts.validateSpirv  = true;

    auto out = compiler.Compile(ShaderStage::Compute, src, "main", opts);
    ASSERT_TRUE(out.success) << out.GetFullLog() << "\n--- emitted source ---\n" << src;
    EXPECT_FALSE(out.spirv.empty());
}
