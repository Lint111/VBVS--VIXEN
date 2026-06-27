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

static Recipe::SdfInstruction boxOp(float bx, float by, float bz) {
    Recipe::SdfInstruction in{};
    in.opCode  = static_cast<uint8_t>(Recipe::SdfOpCode::Box);
    in.data[0] = bx; in.data[1] = by; in.data[2] = bz;
    return in;
}

static Recipe::SdfInstruction smoothUnionOp(float k) {
    Recipe::SdfInstruction in{};
    in.opCode  = static_cast<uint8_t>(Recipe::SdfOpCode::SmoothUnion);
    in.data[2] = k;  // k = Data0.z
    return in;
}

static Recipe::SdfInstruction mirrorXOp() {
    Recipe::SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::MirrorX);
    return in;
}

static Recipe::SdfInstruction restorePosOp() {
    Recipe::SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::RestorePos);
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

// P2.4 M2b — SPIR-V compile gate for MirrorX(SmoothUnion(Box, Sphere)).
// Verifies: (1) emit contains MirrorX/Box/SmoothUnion calls, (2) compiles to valid SPIR-V.
TEST(RecipeCodegen, EmitsMirrorCsgCompilesToSpirv) {
    std::ifstream f(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(f.good()) << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string core = ss.str();

    // Recipe: [MirrorX, Box(0.8,0.5,0.5), Sphere(1.5,0,0,0.5), SmoothUnion(k=0.3), RestorePos]
    Recipe::SdfInstruction prog[] = {
        mirrorXOp(),
        boxOp(0.8f, 0.5f, 0.5f),
        sphere(1.5f, 0.0f, 0.0f, 0.5f),
        smoothUnionOp(0.3f),
        restorePosOp()
    };
    std::string src = Recipe::EmitProceduralComputeShader(prog, 5, core);

    // Verify the three new kernel calls are present in the emitted HLSL.
    EXPECT_NE(src.find("SdfCore_MirrorX(p)"), std::string::npos)
        << "Expected SdfCore_MirrorX call; src:\n" << src;
    EXPECT_NE(src.find("SdfCore_Box(pp"), std::string::npos)
        << "Expected SdfCore_Box(pp...) — box sampling the mirrored pos; src:\n" << src;
    EXPECT_NE(src.find("SdfCore_SmoothUnion("), std::string::npos)
        << "Expected SdfCore_SmoothUnion call; src:\n" << src;

    // Compile through HLSL path → valid SPIR-V.
    ShaderCompiler compiler;
    CompilationOptions opts;
    opts.sourceLanguage = CompilationOptions::SourceLanguage::HLSL;
    opts.validateSpirv  = false;  // ponytail: glslang SPIR-V validator quirk (same as P2.2 M1)

    auto out = compiler.Compile(ShaderStage::Compute, src, "main", opts);
    ASSERT_TRUE(out.success) << out.GetFullLog() << "\n--- emitted source ---\n" << src;
    EXPECT_FALSE(out.spirv.empty());
}
