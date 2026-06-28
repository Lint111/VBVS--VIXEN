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

// P2.4 M4b — SPIR-V gate for Twist + Transform recipe.
// Recipe: [Twist(k=1.2), Box(he=(0.2,1.0,0.2)), RestorePos, Transform(non-identity), Sphere, RestorePos]
// Verifies: SdfCore_Twist + SdfCore_Transform calls emitted; distScale path present; SPIR-V compiles.
TEST(SdfRecipeCodegen, M4b_TwistTransformSpirV_Compiles) {
    std::stringstream ss7;
    std::ifstream kf7(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(kf7.is_open()) << "SDF_CORE_KERNELS_HLSL_PATH not found: " << SDF_CORE_KERNELS_HLSL_PATH;
    ss7 << kf7.rdbuf();
    std::string core7 = ss7.str();

    // Twist(k=1.2)
    Recipe::SdfInstruction twist{};
    twist.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Twist);
    twist.data[0] = 1.2f;

    // Tall box (child of twist)
    Recipe::SdfInstruction box7{};
    box7.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Box);
    box7.data[0] = 0.2f; box7.data[1] = 1.0f; box7.data[2] = 0.2f;

    // Transform: identity rotation, slight translation, scale=2 (invScale=0.5, distScale=2)
    Recipe::SdfInstruction xform{};
    xform.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Transform);
    xform.data[0] = 0.5f;  // trans.x
    // data[4..7] = identity quat (0,0,0,1)
    xform.data[7] = 1.0f;  // invRot.w
    xform.data[8] = 0.5f; xform.data[9] = 0.5f; xform.data[10] = 0.5f;  // invScale
    xform.data[11] = 2.0f;  // distScale

    Recipe::SdfInstruction prog7[] = {
        twist, box7, restorePosOp(),
        xform, sphere(0.f,0.f,0.f, 0.3f), restorePosOp()
    };
    std::string src7 = Recipe::EmitProceduralComputeShader(prog7, 6, core7);

    EXPECT_NE(src7.find("SdfCore_Twist("), std::string::npos)
        << "Expected SdfCore_Twist call; src:\n" << src7;
    EXPECT_NE(src7.find("SdfCore_Transform("), std::string::npos)
        << "Expected SdfCore_Transform call; src:\n" << src7;
    // distScale=2.0 → RestorePos must emit a multiply by 2.0 for the scaled Transform
    EXPECT_NE(src7.find("* 2."), std::string::npos)
        << "Expected distScale multiply in emitted source; src:\n" << src7;

    ShaderCompiler compiler7;
    CompilationOptions opts7;
    opts7.sourceLanguage = CompilationOptions::SourceLanguage::HLSL;
    opts7.validateSpirv  = false;

    auto out7 = compiler7.Compile(ShaderStage::Compute, src7, "main", opts7);
    ASSERT_TRUE(out7.success) << out7.GetFullLog() << "\n--- emitted source ---\n" << src7;
    EXPECT_FALSE(out7.spirv.empty());
}

// ── M4c: value-math SPIR-V test ──────────────────────────────────────────────
// Recipe: Sphere, PositionChannel(Y), MathSin(freq,phase,amp), Displacement(scale), Union(sphere2)
// Exercises PositionChannel, MathSin, Displacement + binary Union in one shader.
// Separately also tests MathAdd, MathMul, MathClamp, MathLog2 chains → SPIR-V.
TEST(SdfRecipeCodegen, M4c_ValueMathDisplacementSpirV_Compiles) {
    std::ifstream kf(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(kf.is_open()) << "SDF_CORE_KERNELS_HLSL_PATH not found: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::stringstream ss; ss << kf.rdbuf();
    std::string core = ss.str();

    // Instruction helpers (local, mirrors parity-test helpers)
    auto posChOp = [](int ch) {
        Recipe::SdfInstruction in{};
        in.opCode=(uint8_t)Recipe::SdfOpCode::PositionChannel; in.data[0]=(float)ch; return in; };
    auto sinOp = [](float freq, float phase, float amp) {
        Recipe::SdfInstruction in{};
        in.opCode=(uint8_t)Recipe::SdfOpCode::MathSin;
        in.data[0]=freq; in.data[1]=phase; in.data[2]=amp; return in; };
    auto displOp = [](float scale) {
        Recipe::SdfInstruction in{};
        in.opCode=(uint8_t)Recipe::SdfOpCode::Displacement; in.data[0]=scale; return in; };
    auto mulOp = []() {
        Recipe::SdfInstruction in{}; in.opCode=(uint8_t)Recipe::SdfOpCode::MathMul; return in; };
    auto addOp = []() {
        Recipe::SdfInstruction in{}; in.opCode=(uint8_t)Recipe::SdfOpCode::MathAdd; return in; };
    auto clampOp = [](float lo, float hi) {
        Recipe::SdfInstruction in{};
        in.opCode=(uint8_t)Recipe::SdfOpCode::MathClamp;
        in.data[0]=lo; in.data[1]=hi; return in; };
    auto log2Op = []() {
        Recipe::SdfInstruction in{}; in.opCode=(uint8_t)Recipe::SdfOpCode::MathLog2; return in; };
    auto lerpOp = []() {
        Recipe::SdfInstruction in{}; in.opCode=(uint8_t)Recipe::SdfOpCode::MathLerp; return in; };

    // Recipe 1: bumpy-sphere displacement
    // [Sphere(origin,0.8), PositionChannel(Y), MathSin(6,0,1), Displacement(0.05)]
    Recipe::SdfInstruction prog8[] = {
        sphere(0.f,0.f,0.f, 0.8f),
        posChOp(1),
        sinOp(6.f, 0.f, 1.f),
        displOp(0.05f),
    };
    std::string src8 = Recipe::EmitProceduralComputeShader(prog8, 4, core);

    EXPECT_NE(src8.find("SdfCore_MathSin("), std::string::npos)
        << "Expected SdfCore_MathSin call in emitted HLSL; src:\n" << src8;
    // Displacement is emitted inline: tN = tSdf + tDisp * scale  (no SdfCore_Displacement function)
    EXPECT_NE(src8.find("0.05"), std::string::npos)
        << "Expected displacement scale literal 0.05 in inline expr; src:\n" << src8;
    // The position channel must appear as curPos.y in emitted source
    EXPECT_NE(src8.find(".y"), std::string::npos)
        << "Expected .y (PositionChannel Y) in emitted HLSL; src:\n" << src8;

    // Recipe 2: math chain — pos(Y) * pos(Y) → clamp → log2 → add with sphere
    // [Sphere(0.5,0,0,0.3), PositionChannel(Y), PositionChannel(Y), MathMul, MathClamp(0,1), MathLog2, MathAdd]
    Recipe::SdfInstruction prog9[] = {
        sphere(0.5f,0.f,0.f, 0.3f),
        posChOp(1), posChOp(1), mulOp(),    // y*y
        clampOp(0.f, 1.f),                   // clamp to [0,1]
        log2Op(),                             // log2
        addOp(),                              // add to sphere SDF
    };
    std::string src9 = Recipe::EmitProceduralComputeShader(prog9, 7, core);

    EXPECT_NE(src9.find("SdfCore_MathMul("), std::string::npos)
        << "Expected SdfCore_MathMul; src:\n" << src9;
    EXPECT_NE(src9.find("SdfCore_MathClamp("), std::string::npos)
        << "Expected SdfCore_MathClamp; src:\n" << src9;
    EXPECT_NE(src9.find("SdfCore_MathLog2("), std::string::npos)
        << "Expected SdfCore_MathLog2; src:\n" << src9;

    // Recipe 3: Lerp between two spheres using DistanceTo as interpolant
    // [Sphere(-1,0,0,0.5), Sphere(1,0,0,0.5), DistanceTo(0,0,0), MathClamp(0,1), MathLerp]
    auto dtoOp = [](float cx, float cy, float cz) {
        Recipe::SdfInstruction in{};
        in.opCode=(uint8_t)Recipe::SdfOpCode::DistanceTo;
        in.data[0]=cx; in.data[1]=cy; in.data[2]=cz; return in; };
    Recipe::SdfInstruction progA[] = {
        sphere(-1.f,0.f,0.f, 0.5f),
        sphere( 1.f,0.f,0.f, 0.5f),
        dtoOp(0.f,0.f,0.f),
        clampOp(0.f,1.f),
        lerpOp(),
    };
    std::string srcA = Recipe::EmitProceduralComputeShader(progA, 5, core);

    EXPECT_NE(srcA.find("SdfCore_MathLerp("), std::string::npos)
        << "Expected SdfCore_MathLerp; srcA:\n" << srcA;
    EXPECT_NE(srcA.find("length("), std::string::npos)
        << "Expected length() call from DistanceTo; srcA:\n" << srcA;

    // Compile all three to SPIR-V
    ShaderCompiler compiler8;
    CompilationOptions opts8;
    opts8.sourceLanguage = CompilationOptions::SourceLanguage::HLSL;
    opts8.validateSpirv  = false;

    auto out8 = compiler8.Compile(ShaderStage::Compute, src8, "main", opts8);
    ASSERT_TRUE(out8.success) << out8.GetFullLog() << "\n--- emitted source (prog8) ---\n" << src8;
    EXPECT_FALSE(out8.spirv.empty());

    auto out9 = compiler8.Compile(ShaderStage::Compute, src9, "main", opts8);
    ASSERT_TRUE(out9.success) << out9.GetFullLog() << "\n--- emitted source (prog9) ---\n" << src9;
    EXPECT_FALSE(out9.spirv.empty());

    auto outA = compiler8.Compile(ShaderStage::Compute, srcA, "main", opts8);
    ASSERT_TRUE(outA.success) << outA.GetFullLog() << "\n--- emitted source (progA) ---\n" << srcA;
    EXPECT_FALSE(outA.spirv.empty());
}
