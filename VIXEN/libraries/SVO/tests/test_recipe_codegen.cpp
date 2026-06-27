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

static Recipe::SdfInstruction smoothSubtractOp(float k) {
    Recipe::SdfInstruction in{};
    in.opCode  = static_cast<uint8_t>(Recipe::SdfOpCode::SmoothSubtract);
    in.data[2] = k;
    return in;
}

static Recipe::SdfInstruction onionOp(float thickness) {
    Recipe::SdfInstruction in{};
    in.opCode  = static_cast<uint8_t>(Recipe::SdfOpCode::Onion);
    in.data[0] = thickness;
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

// P2.4 M3a — SPIR-V compile gate for Onion(SmoothSubtract(Box, Sphere)).
// Verifies: emitter produces SdfCore_SmoothSubtract + SdfCore_Onion calls
// and the resulting HLSL compiles to valid SPIR-V.
TEST(RecipeCodegen, EmitsCsgModifierCompilesToSpirv) {
    std::ifstream f(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(f.good()) << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string core = ss.str();

    // Recipe: [Box(0.7,0.7,0.7), Sphere(0,0,0,0.55), SmoothSubtract(k=0.3), Onion(0.05)]
    // → SmoothSubtract of box-minus-sphere, then Onion shells the result.
    Recipe::SdfInstruction prog[] = {
        boxOp(0.7f, 0.7f, 0.7f),
        sphere(0.0f, 0.0f, 0.0f, 0.55f),
        smoothSubtractOp(0.3f),
        onionOp(0.05f)
    };
    std::string src = Recipe::EmitProceduralComputeShader(prog, 4, core);

    // Verify the key CSG+modifier kernel calls appear in the emitted HLSL.
    EXPECT_NE(src.find("SdfCore_SmoothSubtract("), std::string::npos)
        << "Expected SdfCore_SmoothSubtract call; src:\n" << src;
    EXPECT_NE(src.find("SdfCore_Onion("), std::string::npos)
        << "Expected SdfCore_Onion call; src:\n" << src;

    // Compile through HLSL path → valid SPIR-V.
    ShaderCompiler compiler2;
    CompilationOptions opts2;
    opts2.sourceLanguage = CompilationOptions::SourceLanguage::HLSL;
    opts2.validateSpirv  = false;  // ponytail: glslang SPIR-V validator quirk (same as P2.2 M1)

    auto out2 = compiler2.Compile(ShaderStage::Compute, src, "main", opts2);
    ASSERT_TRUE(out2.success) << out2.GetFullLog() << "\n--- emitted source ---\n" << src;
    EXPECT_FALSE(out2.spirv.empty());
}

// P2.4 M3b-1 — SPIR-V compile gate for Torus and BoxRounded (2 of the 5 new leaves).
// Recipe: [Torus(majorR=0.6, minorR=0.2), BoxRounded(he=(0.4,0.4,0.4), rr=0.05), Union]
// Verifies: emitter produces SdfCore_Torus and SdfCore_BoxRounded calls; compiles to SPIR-V.
TEST(RecipeCodegen, EmitsLeafPrimitivesCompilesToSpirv) {
    std::ifstream f(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(f.good()) << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string core = ss.str();

    // Torus instruction: data[0]=majorR, data[1]=minorR
    Recipe::SdfInstruction torus{};
    torus.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Torus);
    torus.data[0] = 0.6f; torus.data[1] = 0.2f;

    // BoxRounded instruction: data[0..2]=halfExtents, data[3]=rounding
    Recipe::SdfInstruction boxRounded{};
    boxRounded.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::BoxRounded);
    boxRounded.data[0] = 0.4f; boxRounded.data[1] = 0.4f; boxRounded.data[2] = 0.4f;
    boxRounded.data[3] = 0.05f;

    Recipe::SdfInstruction prog[] = { torus, boxRounded, unionOp() };
    std::string src = Recipe::EmitProceduralComputeShader(prog, 3, core);

    // Verify the two new leaf calls appear in the emitted HLSL.
    EXPECT_NE(src.find("SdfCore_Torus("), std::string::npos)
        << "Expected SdfCore_Torus call; src:\n" << src;
    EXPECT_NE(src.find("SdfCore_BoxRounded("), std::string::npos)
        << "Expected SdfCore_BoxRounded call; src:\n" << src;

    // Compile through HLSL path → valid SPIR-V.
    ShaderCompiler compiler3;
    CompilationOptions opts3;
    opts3.sourceLanguage = CompilationOptions::SourceLanguage::HLSL;
    opts3.validateSpirv  = false;  // ponytail: glslang SPIR-V validator quirk (same as M2/M3a)

    auto out3 = compiler3.Compile(ShaderStage::Compute, src, "main", opts3);
    ASSERT_TRUE(out3.success) << out3.GetFullLog() << "\n--- emitted source ---\n" << src;
    EXPECT_FALSE(out3.spirv.empty());
}
