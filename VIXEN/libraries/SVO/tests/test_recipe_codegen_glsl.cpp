// Lazy-Procedural-Delta-Baseline Inc1 M4 Task 8: compile + basic-shape smoke test for
// EmitProceduralFieldFunctionGlsl. Pure CPU (string output only) — no GPU/glslang needed.
// Numerical correctness (vs CPU evalRecipe, on real GPU) is Task 9's job; this test only
// proves the emitter runs, produces well-formed function shape, and honours the
// float-literal guard.

#include "Recipe/SdfRecipeCodegenGlsl.h"
#include "Recipe/generated/SdfOpCodes.g.h"

#include <gtest/gtest.h>
#include <regex>

using namespace Vixen::SVO::Recipe;

namespace {
SdfInstruction MakeSphere(float cx, float cy, float cz, float r) {
    SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
    in.data[0] = cx; in.data[1] = cy; in.data[2] = cz; in.data[3] = r;
    return in;
}
SdfInstruction MakeOp(SdfOpCode code) {
    SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(code);
    return in;
}
} // namespace

TEST(EmitProceduralFieldFunctionGlsl, SingleSphereProducesComposableFunction) {
    SdfInstruction prog[] = { MakeSphere(0.0f, 0.0f, 0.0f, 2.5f) };
    const std::string glsl = EmitProceduralFieldFunctionGlsl(prog, 1, /*recipeId=*/7);

    // Composable function, not a trace main: exactly one function, named by id, takes vec3
    // + the per-instance params[6] array (Recipe-Parameterization M2 Task 5), returns float,
    // no [numthreads]/main()/RWTexture/cbuffer trace-shader scaffolding.
    EXPECT_NE(glsl.find("float sdfRecipe_7(vec3 p, float params[6]) {"), std::string::npos);
    EXPECT_EQ(glsl.find("void main"), std::string::npos);
    EXPECT_EQ(glsl.find("numthreads"), std::string::npos);
    EXPECT_EQ(glsl.find("cbuffer"), std::string::npos);
    EXPECT_EQ(glsl.find("RWTexture"), std::string::npos);
    EXPECT_NE(glsl.find("SdfCore_Sphere(p, vec3("), std::string::npos);
}

TEST(EmitProceduralFieldFunctionGlsl, UnionOfTwoSpheresChainsCorrectly) {
    SdfInstruction prog[] = {
        MakeSphere(-2.0f, 0.0f, 0.0f, 2.5f),
        MakeSphere(2.0f, 0.0f, 0.0f, 2.5f),
        MakeOp(SdfOpCode::Union),
    };
    const std::string glsl = EmitProceduralFieldFunctionGlsl(prog, 3, /*recipeId=*/0);

    EXPECT_NE(glsl.find("SdfCore_Union(t0, t1)"), std::string::npos);
    EXPECT_NE(glsl.find("return t2;"), std::string::npos);
}

// Recipe-Diversity-Stress-Scene-Inc6 M1 — spatial-contract meta/resolve prototype. Proves
// EmitProceduralFieldFunctionGlsl's new emitDeclaredPositionOutParam=true path produces the
// expected `out vec3 declaredPos` signature + inline assignment shape (pure string-shape
// check; GPU numerical parity + the "declared position actually moves the shape" claim live
// in test_recipe_glsl_numerical_parity.cpp's GPU-gated harness).
TEST(EmitProceduralFieldFunctionGlsl, DeclaredPositionEmitsOutParamAndInlineAssignment) {
    SdfInstruction prog[] = {
        MakeOp(SdfOpCode::ReadParamFloat3), // idx defaults to data[0]=0 via zero-init
        MakeOp(SdfOpCode::DeclarePosition),
        MakeSphere(0.0f, 0.0f, 0.0f, 0.5f),
    };
    const std::string glsl = EmitProceduralFieldFunctionGlsl(prog, 3, /*recipeId=*/9,
                                                              /*emitDeclaredPositionOutParam=*/true);

    // Signature carries the out-param.
    EXPECT_NE(glsl.find("float sdfRecipe_9(vec3 p, float params[6], out vec3 declaredPos) {"),
              std::string::npos) << glsl;
    // Inline assignment happens BEFORE the resolve segment's sphere call — i.e. the meta
    // segment's out-param write precedes the SdfCore_Sphere call textually, proving the
    // direction doc's "assign inline mid-walk, keep walking to the resolve segment" argument
    // produces the expected single linear function body (no early-exit/branch machinery).
    size_t assignPos = glsl.find("declaredPos = vec3(");
    size_t spherePos = glsl.find("SdfCore_Sphere(");
    EXPECT_NE(assignPos, std::string::npos) << glsl;
    EXPECT_NE(spherePos, std::string::npos) << glsl;
    EXPECT_LT(assignPos, spherePos) << "declaredPos assignment must precede the resolve "
                                        "segment's field evaluation:\n" << glsl;
    // The resolve segment samples the TRANSLATED position (curPos rewritten to p - declaredPos),
    // not the raw input p — otherwise the declared position would be a disconnected reported
    // value that never actually moves the shape.
    EXPECT_NE(glsl.find("SdfCore_Sphere(pp"), std::string::npos)
        << "resolve segment must sample the DeclarePosition-translated point, not raw p:\n" << glsl;
}

TEST(EmitProceduralFieldFunctionGlsl, EveryNumericLiteralHasDecimalPoint) {
    // Float-literal guard: a program exercising an integer-valued float (radius 6, a value
    // that would silently become the GLSL/HLSL int literal `6` without the guard) — mirrors
    // the documented `1/6` int-div failure class the guard exists to prevent.
    SdfInstruction prog[] = { MakeSphere(1.0f, 2.0f, 3.0f, 6.0f) };
    const std::string glsl = EmitProceduralFieldFunctionGlsl(prog, 1, /*recipeId=*/1);

    // Every bare integer token (a run of digits NOT adjacent to '.', and not part of an
    // identifier like sdfRecipe_1 or t0) would indicate a literal emitted without ".0".
    // std::regex's ECMAScript flavor has no lookbehind, so scan manually instead.
    // Array-size/index brackets ([...]) are the one deliberate exception (Recipe-
    // Parameterization M2 Task 5's params[6] argument/params[N] indexed reads) — those
    // digits are compile-time array dimensions/indices, never GLSL float-typed values, so
    // GLSL's int/float overload split (the guard's whole reason to exist) doesn't apply.
    for (size_t i = 0; i < glsl.size(); ) {
        if (!std::isdigit(static_cast<unsigned char>(glsl[i]))) { ++i; continue; }
        const size_t start = i;
        while (i < glsl.size() && std::isdigit(static_cast<unsigned char>(glsl[i]))) ++i;
        const bool precededByIdentChar = start > 0 &&
            (std::isalpha(static_cast<unsigned char>(glsl[start - 1])) || glsl[start - 1] == '_' || glsl[start - 1] == '.');
        const bool followedByIdentChar = i < glsl.size() &&
            (std::isalpha(static_cast<unsigned char>(glsl[i])) || glsl[i] == '_' || glsl[i] == '.');
        const bool isArrayBracketDigit = start > 0 && glsl[start - 1] == '[' &&
            i < glsl.size() && glsl[i] == ']';
        if (!precededByIdentChar && !followedByIdentChar && !isArrayBracketDigit) {
            FAIL() << "Bare integer literal (no decimal point) found: '" << glsl.substr(start, i - start)
                   << "' in:\n" << glsl;
        }
    }
}
