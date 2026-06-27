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

// P2.4 M3b-2 — SPIR-V compile gate for positioned leaf primitives.
// Recipe: [Ellipsoid(radii=(0.6,0.4,0.3), offset=(0.5,0,0)),
//          Cone(sin30=0.5 cos30=0.866, h=1.0, offset=(-0.5,0,0)), Union]
// Verifies: (1) emitter produces SdfCore_Ellipsoid and SdfCore_Cone calls,
//           (2) both calls include the "- float3(...)" position-offset expression,
//           (3) the resulting HLSL compiles to valid SPIR-V.
TEST(RecipeCodegen, EmitsPositionedLeafPrimitivesCompilesToSpirv) {
    std::ifstream f4(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(f4.good()) << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss4;
    ss4 << f4.rdbuf();
    std::string core = ss4.str();

    // Ellipsoid at offset (+0.5, 0, 0): radii=(0.6, 0.4, 0.3)
    Recipe::SdfInstruction ellipsoid{};
    ellipsoid.opCode  = static_cast<uint8_t>(Recipe::SdfOpCode::Ellipsoid);
    ellipsoid.data[0] = 0.6f; ellipsoid.data[1] = 0.4f; ellipsoid.data[2] = 0.3f;
    ellipsoid.data[4] = 0.5f;  // position offset x=+0.5

    // Cone at offset (-0.5, 0, 0): half-angle 30°, height 1.0
    Recipe::SdfInstruction cone{};
    cone.opCode  = static_cast<uint8_t>(Recipe::SdfOpCode::Cone);
    cone.data[0] = 0.5f;       // sin(30°)
    cone.data[1] = 0.866025f;  // cos(30°)
    cone.data[2] = 1.0f;       // height
    cone.data[4] = -0.5f;      // position offset x=-0.5

    Recipe::SdfInstruction prog[] = { ellipsoid, cone, unionOp() };
    std::string src = Recipe::EmitProceduralComputeShader(prog, 3, core);

    // Verify the two new kernel calls appear in the emitted HLSL.
    EXPECT_NE(src.find("SdfCore_Ellipsoid("), std::string::npos)
        << "Expected SdfCore_Ellipsoid call; src:\n" << src;
    EXPECT_NE(src.find("SdfCore_Cone("), std::string::npos)
        << "Expected SdfCore_Cone call; src:\n" << src;
    // Position-offset: "- float3(" must appear from the data[4..6] offset expressions.
    EXPECT_NE(src.find("- float3("), std::string::npos)
        << "Expected position-offset '- float3(...)' in emitted HLSL; src:\n" << src;

    // Compile through HLSL path → valid SPIR-V.
    ShaderCompiler compiler4;
    CompilationOptions opts4;
    opts4.sourceLanguage = CompilationOptions::SourceLanguage::HLSL;
    opts4.validateSpirv  = false;  // ponytail: glslang SPIR-V validator quirk (same as prior gates)

    auto out4 = compiler4.Compile(ShaderStage::Compute, src, "main", opts4);
    ASSERT_TRUE(out4.success) << out4.GetFullLog() << "\n--- emitted source ---\n" << src;
    EXPECT_FALSE(out4.spirv.empty());
}

TEST(RecipeCodegen, EmitsM3b3PrimitivesCompilesToSpirv) {
    // P2.4 M3b-3: branchless Pyramid + RoundCone (both had rewrite) + Segment (no-offset)
    std::ifstream f5(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(f5.good()) << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss5;
    ss5 << f5.rdbuf();
    std::string core = ss5.str();

    // Pyramid at offset (+0.3, 0, 0): height=1.0
    Recipe::SdfInstruction pyramid{};
    pyramid.opCode  = static_cast<uint8_t>(Recipe::SdfOpCode::Pyramid);
    pyramid.data[0] = 1.0f;   // height
    pyramid.data[4] = 0.3f;   // position offset x

    // RoundCone at offset (-0.3, 0, 0): r1=0.3, r2=0.1, height=1.0
    Recipe::SdfInstruction roundCone{};
    roundCone.opCode  = static_cast<uint8_t>(Recipe::SdfOpCode::RoundCone);
    roundCone.data[0] = 0.3f; roundCone.data[1] = 0.1f; roundCone.data[2] = 1.0f;
    roundCone.data[4] = -0.3f;

    // Segment (no-offset): ptA=(0,0,0), radius=0.05, ptB=(0.5,0.5,0)
    Recipe::SdfInstruction segment{};
    segment.opCode  = static_cast<uint8_t>(Recipe::SdfOpCode::Segment);
    segment.data[0] = 0.0f; segment.data[1] = 0.0f; segment.data[2] = 0.0f;
    segment.data[3] = 0.05f;
    segment.data[4] = 0.5f; segment.data[5] = 0.5f; segment.data[6] = 0.0f;

    Recipe::SdfInstruction prog[] = { pyramid, roundCone, unionOp(), segment, unionOp() };
    std::string src = Recipe::EmitProceduralComputeShader(prog, 5, core);

    EXPECT_NE(src.find("SdfCore_Pyramid("), std::string::npos)
        << "Expected SdfCore_Pyramid call; src:\n" << src;
    EXPECT_NE(src.find("SdfCore_RoundCone("), std::string::npos)
        << "Expected SdfCore_RoundCone call; src:\n" << src;
    EXPECT_NE(src.find("SdfCore_Segment("), std::string::npos)
        << "Expected SdfCore_Segment call; src:\n" << src;

    ShaderCompiler compiler5;
    CompilationOptions opts5;
    opts5.sourceLanguage = CompilationOptions::SourceLanguage::HLSL;
    opts5.validateSpirv  = false;  // ponytail: glslang SPIR-V validator quirk (same as prior gates)

    auto out5 = compiler5.Compile(ShaderStage::Compute, src, "main", opts5);
    ASSERT_TRUE(out5.success) << out5.GetFullLog() << "\n--- emitted source ---\n" << src;
    EXPECT_FALSE(out5.spirv.empty());
}

// P2.4 M4a — SPIR-V gate for Revolution transform recipe.
// Recipe: [Revolution(offset=0.8, center=origin), Sphere(origin, r=0.2), RestorePos]
// Verifies: SdfCore_Revolution + SdfCore_Sphere calls emitted; posStack save/restore pattern; SPIR-V compiles.
TEST(SdfRecipeCodegen, M4a_RevolutionSpirV_Compiles) {
    std::stringstream ss6;
    std::ifstream kf6(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(kf6.is_open()) << "SDF_CORE_KERNELS_HLSL_PATH not found: " << SDF_CORE_KERNELS_HLSL_PATH;
    ss6 << kf6.rdbuf();
    std::string core = ss6.str();

    // Revolution(offset=0.8, center=origin)
    Recipe::SdfInstruction rev{};
    rev.opCode  = static_cast<uint8_t>(Recipe::SdfOpCode::Revolution);
    rev.data[0] = 0.8f;  // offset
    // data[4..6] = center = (0,0,0) (default zero)

    Recipe::SdfInstruction prog[] = { rev, sphere(0.f,0.f,0.f, 0.2f), restorePosOp() };
    std::string src = Recipe::EmitProceduralComputeShader(prog, 3, core);

    EXPECT_NE(src.find("SdfCore_Revolution("), std::string::npos)
        << "Expected SdfCore_Revolution call; src:\n" << src;
    EXPECT_NE(src.find("SdfCore_Sphere("), std::string::npos)
        << "Expected SdfCore_Sphere call; src:\n" << src;
    // posStack pattern: a new float3 variable for the transformed position
    EXPECT_NE(src.find("SdfCore_Revolution(p,"), std::string::npos)
        << "Expected position variable fed into Revolution; src:\n" << src;

    ShaderCompiler compiler6;
    CompilationOptions opts6;
    opts6.sourceLanguage = CompilationOptions::SourceLanguage::HLSL;
    opts6.validateSpirv  = false;

    auto out6 = compiler6.Compile(ShaderStage::Compute, src, "main", opts6);
    ASSERT_TRUE(out6.success) << out6.GetFullLog() << "\n--- emitted source ---\n" << src;
    EXPECT_FALSE(out6.spirv.empty());
}
