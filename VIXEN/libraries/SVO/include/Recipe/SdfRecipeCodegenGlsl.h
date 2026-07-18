#pragma once
#include "Recipe/SdfInstruction.h"
#include "Recipe/RecipeRegistry.h"  // IsValidSdfOpCode — the exact opcode set this emitter must cover
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace Vixen::SVO::Recipe {

// GLSL sibling of EmitProceduralComputeShader (SdfRecipeCodegen.h) — Lazy-Procedural-Delta-
// Baseline Inc1 M4 Task 8. Same emit-time simulation (value stack, position stack, DistScale
// stack with the |scale-1|>1e-4 multiply on RestorePos) but emits GLSL and returns ONLY a
// composable `float sdfRecipe_<id>(vec3 p, float params[6]) { ... }` function — no trace main
// (that's the caller's job: composing this + SdfCoreKernels.glsl + a wrapper compute shader,
// per Task 9's numerical-parity harness). Covers exactly the opcode set
// RecipeRegistry::IsValidSdfOpCode accepts (asserted at the call site by that same predicate,
// mirroring the HLSL emitter's `assert(paramMask == 0)`). `params` (Recipe-Parameterization
// M2 Task 5) is the per-instance dynamic-parameter array backing ReadParam/ReadParamFloat3 —
// every emitted function takes it uniformly (even one that never uses it), since
// evalRecipeField's switch dispatches to whichever sdfRecipe_<id> by recipeId and needs one
// call shape.
//
// Float-literal guard (mandatory, kernel-framework discipline): every numeric literal must
// emit with a decimal point/exponent — GLSL, like HLSL, has an int/float overload split, and
// `1/6` (no decimal point) silently integer-divides to 0 on the GPU where the CPU VM's C++
// `1.0f/6.0f` doesn't (the documented failure class this guard exists for). `f()` below
// enforces that for every literal this emitter writes. ReadParam/ReadParamFloat3 (M2 Task 5)
// are the one deliberate exception — see their case sites below for why.
// emitDeclaredPositionOutParam (Recipe-Diversity-Stress-Scene-Inc6 M1 — spatial-contract
// meta/resolve prototype): when true, the emitted function gains a trailing `out vec3
// declaredPos` parameter, assigned inline the moment a DeclarePosition instruction is walked
// (mirroring UberShaderSplice.h's getRecipeBoundSphere out-param convention, but position-
// DEPENDENT rather than CPU-baked-constant — the gap Recipe-Spatial-Contract-Two-Pass-
// Culling-Direction-2026-07.md's "suggested first step" calls out as unproven). Defaults to
// false so every pre-Inc6 call site's composed shader (which hardcodes a call shape with no
// out-param, e.g. test_recipe_glsl_numerical_parity.cpp's ComposeComputeShader) keeps
// compiling unchanged — this is opt-in per the direction doc's own "contract is opt-in, not
// universal" framing, not a change to the shared function's default shape.
inline std::string EmitProceduralFieldFunctionGlsl(
    const SdfInstruction* prog,
    uint32_t count,
    uint32_t recipeId,
    bool emitDeclaredPositionOutParam = false)
{
    std::vector<std::string> stk;
    std::string body;
    int n = 0;

    // emit-time position stack: mirrors pos/posStack in evalRecipe (C# VM ctx.Pos/PosStack)
    std::string curPos = "p";
    std::vector<std::string> posSaveStk;
    // emit-time distScale stack: 1.0f for non-scaling transforms; M4b Transform pushes data[11]
    // At RestorePos: if |scale-1|>1e-4 emit a multiply to scale the TOS distance before popping.
    std::vector<float> distScaleSaveStk;

    // ponytail: std::to_string for floats — sufficient for GLSL literals; no locale issues.
    // Ensures a decimal point so GLSL sees it as a float literal (the float-literal guard).
    auto f = [](float v) {
        std::string s = std::to_string(v);
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
            s += ".0";
        return s;
    };

    for (uint32_t i = 0; i < count; ++i) {
        const SdfInstruction& in = prog[i];
        assert(IsValidSdfOpCode(in.opCode) && "EmitProceduralFieldFunctionGlsl: unknown opcode");
        // paramMask!=0 is now legal exactly for ReadParam/ReadParamFloat3 (Recipe-
        // Parameterization M1 Task 2's registry allow-list) — every other opcode still
        // requires paramMask==0, mirroring RecipeRegistry::Register's own narrowed check.
        assert((in.paramMask == 0 ||
                static_cast<SdfOpCode>(in.opCode) == SdfOpCode::ReadParam ||
                static_cast<SdfOpCode>(in.opCode) == SdfOpCode::ReadParamFloat3) &&
               "ParamMask!=0 only valid on ReadParam/ReadParamFloat3");
        switch (static_cast<SdfOpCode>(in.opCode)) {
            case SdfOpCode::Sphere: {
                // data[0..2] = center xyz, data[3] = radius (mirrors SdfRecipeEval.h)
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Sphere(" + curPos + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2])
                    + "), " + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Box: {
                // data[0..2] = halfExtents xyz (mirrors SdfRecipeEval.h)
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Box(" + curPos + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "));\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Union: {
                assert(stk.size() >= 2 && "Union: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Union(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::SmoothUnion: {
                // data[2] = k (Data0.z), mirrors evalRecipe
                assert(stk.size() >= 2 && "SmoothUnion: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothUnion(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Binary CSG (non-smooth) ---
            case SdfOpCode::Subtract: {           // non-commutative: A minus B
                assert(stk.size() >= 2 && "Subtract: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Subtract(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Intersect: {
                assert(stk.size() >= 2 && "Intersect: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Intersect(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Xor: {
                assert(stk.size() >= 2 && "Xor: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Xor(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Binary CSG (smooth linear): k = data[2] ---
            case SdfOpCode::SmoothSubtract: {     // non-commutative
                assert(stk.size() >= 2 && "SmoothSubtract: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothSubtract(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::SmoothIntersect: {
                assert(stk.size() >= 2 && "SmoothIntersect: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothIntersect(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::SmoothMax: {
                assert(stk.size() >= 2 && "SmoothMax: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothMax(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Binary CSG (smooth cubic): k = data[2] ---
            case SdfOpCode::SmoothUnionCubic: {
                assert(stk.size() >= 2 && "SmoothUnionCubic: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothUnionCubic(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::SmoothSubtractCubic: { // non-commutative
                assert(stk.size() >= 2 && "SmoothSubtractCubic: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothSubtractCubic(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::SmoothIntersectCubic: {
                assert(stk.size() >= 2 && "SmoothIntersectCubic: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothIntersectCubic(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Unary modifiers: pop-and-replace (TOS), radius/thickness = data[0] ---
            case SdfOpCode::Round: {
                assert(!stk.empty() && "Round: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Round(" + a + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Onion: {
                assert(!stk.empty() && "Onion: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Onion(" + a + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Leaf primitives (no-position, pos-off=NO) ---
            case SdfOpCode::Capsule: {
                // data[0]=halfHeight, data[1]=radius
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Capsule(" + curPos + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Cylinder: {
                // data[0]=halfHeight, data[1]=radius
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Cylinder(" + curPos + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Torus: {
                // data[0]=majorRadius, data[1]=minorRadius
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Torus(" + curPos + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::BoxRounded: {
                // data[0..2]=halfExtents, data[3]=rounding
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_BoxRounded(" + curPos + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Plane: {
                // data[0..2]=normal, data[3]=distance
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Plane(" + curPos + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Leaf primitives (position-offset, pos-off=YES) ---
            // Offset baked into position expression: curPos + " - vec3(dx,dy,dz)"
            case SdfOpCode::Ellipsoid: {
                // data[0..2]=radii, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_Ellipsoid(" + q + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "));\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::HollowCylinder: {
                // data[0]=halfLen, data[1]=outerR, data[2]=wall, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_HollowCylinder(" + q + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::TaperedCylinder: {
                // data[0]=height, data[1]=r1 (base), data[2]=r2 (top), data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_TaperedCylinder(" + q + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Cone: {
                // data[0]=sinAngle, data[1]=cosAngle, data[2]=height, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_Cone(" + q + ", vec2("
                    + f(in.data[0]) + ", " + f(in.data[1]) + "), " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::CappedTorus: {
                // data[0]=sinA, data[1]=cosA, data[2]=majorR, data[3]=minorR, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_CappedTorus(" + q + ", vec2("
                    + f(in.data[0]) + ", " + f(in.data[1]) + "), " + f(in.data[2]) + ", " + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Link: {
                // data[0]=halfLen, data[1]=majorR, data[2]=minorR, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_Link(" + q + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            // Panel/Plank/RoundedBox: positioned BoxRounded (same math, opcode differs)
            case SdfOpCode::Panel: {
                // data[0..2]=halfExtents, data[3]=rounding, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_BoxRounded(" + q + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Plank: {
                // data[0..2]=halfExtents, data[3]=rounding, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_BoxRounded(" + q + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::RoundedBox: {
                // data[0..2]=halfExtents, data[3]=rounding, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_BoxRounded(" + q + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Leaf primitives — prism + cone family ---
            case SdfOpCode::RoundCone: {
                // data[0]=r1, data[1]=r2, data[2]=height, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_RoundCone(" + q + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::FakeRoundCone: {
                // data[0]=r1, data[1]=r2, data[2]=height, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_FakeRoundCone(" + q + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Segment: {
                // data[0..2]=pointA, data[3]=radius, data[4..6]=pointB — samples curPos directly (no offset)
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Segment(" + curPos + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), vec3("
                    + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::TriangularPrism: {
                // data[0]=h.x, data[1]=h.y, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_TriangularPrism(" + q + ", vec2("
                    + f(in.data[0]) + ", " + f(in.data[1]) + "));\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Pyramid: {
                // data[0]=height, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_Pyramid(" + q + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::HexPrism: {
                // data[0]=h.x (hex radius), data[1]=h.y (half-height), data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - vec3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_HexPrism(" + q + ", vec2("
                    + f(in.data[0]) + ", " + f(in.data[1]) + "));\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MirrorX: {
                std::string pN = "pp" + std::to_string(n++);
                body += "  vec3 " + pN + " = SdfCore_MirrorX(" + curPos + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::MirrorY: {
                std::string pN = "pp" + std::to_string(n++);
                body += "  vec3 " + pN + " = SdfCore_MirrorY(" + curPos + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::MirrorZ: {
                std::string pN = "pp" + std::to_string(n++);
                body += "  vec3 " + pN + " = SdfCore_MirrorZ(" + curPos + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::Elongate: {
                // data[0..2] = elongation h
                std::string pN = "pp" + std::to_string(n++);
                body += "  vec3 " + pN + " = SdfCore_Elongate(" + curPos + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "));\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::Revolution: {
                // data[0]=offset, data[4..6]=center
                std::string pN = "pp" + std::to_string(n++);
                body += "  vec3 " + pN + " = SdfCore_Revolution(" + curPos + ", vec3("
                    + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + "), "
                    + f(in.data[0]) + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::Transform: {
                // data[0..2]=translation, data[4..7]=invRot xyzw, data[8..10]=invScale, data[11]=distScale
                std::string pN = "pp" + std::to_string(n++);
                body += "  vec3 " + pN + " = SdfCore_Transform(" + curPos + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), vec4("
                    + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ", " + f(in.data[7]) + "), vec3("
                    + f(in.data[8]) + ", " + f(in.data[9]) + ", " + f(in.data[10]) + "));\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(in.data[11]); curPos = pN;
                break;
            }
            case SdfOpCode::Twist: {
                // data[0]=k
                std::string pN = "pp" + std::to_string(n++);
                body += "  vec3 " + pN + " = SdfCore_Twist(" + curPos + ", " + f(in.data[0]) + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::Bend: {
                // data[0]=k
                std::string pN = "pp" + std::to_string(n++);
                body += "  vec3 " + pN + " = SdfCore_Bend(" + curPos + ", " + f(in.data[0]) + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::RepeatInfinite: {
                // data[0..2]=spacing xyz
                std::string pN = "pp" + std::to_string(n++);
                body += "  vec3 " + pN + " = SdfCore_RepeatInfinite(" + curPos + ", vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "));\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::RepeatLimited: {
                // data[0]=spacing scalar, data[1..3]=limit xyz
                std::string pN = "pp" + std::to_string(n++);
                body += "  vec3 " + pN + " = SdfCore_RepeatLimited(" + curPos + ", "
                    + f(in.data[0]) + ", vec3("
                    + f(in.data[1]) + ", " + f(in.data[2]) + ", " + f(in.data[3]) + "));\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::RestorePos: {
                assert(!posSaveStk.empty() && "RestorePos: emit-time position stack underflow");
                assert(!distScaleSaveStk.empty() && "RestorePos: emit-time distScale stack underflow");
                float scale = distScaleSaveStk.back(); distScaleSaveStk.pop_back();
                if (std::abs(scale - 1.0f) > 1e-4f) {
                    // M4b: Transform pushes a non-unit scale; emit the multiply into the TOS distance.
                    assert(!stk.empty() && "RestorePos: value stack empty when applying distScale");
                    std::string sN = "t" + std::to_string(n++);
                    body += "  float " + sN + " = " + stk.back() + " * " + f(scale) + ";\n";
                    stk.back() = sN;
                }
                curPos = posSaveStk.back(); posSaveStk.pop_back();
                break;
            }
            // ── value-math lane (emit mirrors eval) ─────────────────────────
            // Unary (pop-and-replace TOS)
            case SdfOpCode::MathSin: {
                assert(!stk.empty() && "MathSin: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSin(" + a + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathCos: {
                assert(!stk.empty() && "MathCos: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathCos(" + a + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathSmoothstep: {
                assert(!stk.empty() && "MathSmoothstep: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSmoothstep(" + a + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathRemap: {
                assert(!stk.empty() && "MathRemap: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathRemap(" + a + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ", " + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathClamp: {
                assert(!stk.empty() && "MathClamp: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathClamp(" + a + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathAbs: {
                assert(!stk.empty() && "MathAbs: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathAbs(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathFrac: {
                assert(!stk.empty() && "MathFrac: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathFrac(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathPow: {
                assert(!stk.empty() && "MathPow: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathPow(" + a + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathSqrt: {
                assert(!stk.empty() && "MathSqrt: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSqrt(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathNegate: {
                assert(!stk.empty() && "MathNegate: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathNegate(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathStep: {
                assert(!stk.empty() && "MathStep: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathStep(" + a + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathSign: {
                assert(!stk.empty() && "MathSign: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSign(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathSaturate: {
                assert(!stk.empty() && "MathSaturate: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSaturate(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathExp: {
                assert(!stk.empty() && "MathExp: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathExp(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathLog: {
                assert(!stk.empty() && "MathLog: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathLog(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathLog2: {
                assert(!stk.empty() && "MathLog2: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathLog2(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            // Binary (pop b=top, a=new-top, replace with result)
            case SdfOpCode::MathAdd: {
                assert(stk.size() >= 2 && "MathAdd: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathAdd(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathSub: {            // non-commutative: a - b
                assert(stk.size() >= 2 && "MathSub: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSub(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathMul: {
                assert(stk.size() >= 2 && "MathMul: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathMul(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathDiv: {
                assert(stk.size() >= 2 && "MathDiv: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathDiv(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathMin: {
                assert(stk.size() >= 2 && "MathMin: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathMin(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathMax: {
                assert(stk.size() >= 2 && "MathMax: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathMax(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            // Ternary MathLerp: t_val=top, b=middle, a=bottom -> lerp(a,b,t_val)
            case SdfOpCode::MathLerp: {
                assert(stk.size() >= 3 && "MathLerp: emit-time value stack underflow");
                std::string t_val = stk.back(); stk.pop_back();
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathLerp(" + a + ", " + b + ", " + t_val + ");\n";
                stk.push_back(t);
                break;
            }
            // Ternary Select: b=top, a=middle, cond=bottom -> cond>threshold?a:b
            case SdfOpCode::Select: {
                assert(stk.size() >= 3 && "Select: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string cond = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Select(" + cond + ", " + a + ", " + b + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            // Leaf: push position-derived value
            case SdfOpCode::PositionChannel: {
                // channel baked in data[0]: 0=x,1=y,2=z,3=length(xz)
                std::string t = "t" + std::to_string(n++);
                int ch = (int)in.data[0];
                std::string expr;
                switch (ch) {
                    case 0: expr = curPos + ".x"; break;
                    case 1: expr = curPos + ".y"; break;
                    case 2: expr = curPos + ".z"; break;
                    case 3: expr = "length(vec2(" + curPos + ".x, " + curPos + ".z))"; break;
                    default: expr = curPos + ".y"; break;
                }
                body += "  float " + t + " = " + expr + ";\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Displacement: {       // pop disp; push sdf + disp * scale
                assert(stk.size() >= 2 && "Displacement: emit-time value stack underflow");
                std::string disp = stk.back(); stk.pop_back();
                std::string sdf = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Displacement(" + sdf + ", " + disp + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::DistanceTo: {          // push length(curPos - center)
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = length(" + curPos + " - vec3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "));\n";
                stk.push_back(t);
                break;
            }
            // VM-control ops (hand-dispatched — no generated kernel equivalent)
            case SdfOpCode::Output: {              // passthrough; marks recipe end
                // no-op for emit (top of stack is the output value)
                break;
            }
            case SdfOpCode::PushParam: {           // push baked parameter value
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = " + f(in.data[0]) + ";\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::ReadParam: {           // push params[data[0]] (runtime-indexed read)
                // Deliberate exception to the float-literal guard above: only the INDEX
                // (in.data[0], which slot to read) is a compile-time-known literal here —
                // the VALUE at that slot is runtime-dynamic, read from the params[] argument
                // at shader execution time. Baking the value itself as a GLSL literal would
                // defeat the entire point of Recipe-Parameterization P4 (params must vary
                // without a shader recompile) — see Recipe-Parameterization-Plan-2026-07.md
                // M2 Task 5.
                std::string t = "t" + std::to_string(n++);
                int idx = static_cast<int>(in.data[0]);
                body += "  float " + t + " = params[" + std::to_string(idx) + "];\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::ReadParamFloat3: {     // push params[idx*3 .. idx*3+2] as vec3
                // Same deliberate float-literal-guard exception as ReadParam above — the
                // three read indices are compile-time literals, the values are not.
                std::string t = "t" + std::to_string(n++);
                int idx = static_cast<int>(in.data[0]);
                int base = idx * 3;
                body += "  vec3 " + t + " = vec3(params[" + std::to_string(base) + "], params["
                    + std::to_string(base + 1) + "], params[" + std::to_string(base + 2) + "]);\n";
                stk.push_back(t + ".x");
                stk.push_back(t + ".y");
                stk.push_back(t + ".z");
                break;
            }
            case SdfOpCode::DeclarePosition: {     // meta segment: pop float3, assign out-param, translate curPos
                // Inc6 M1 prototype (VIXEN-only opcode). Pops the 3 emit-time value-stack
                // entries (deepest=x .. top=z, mirroring ReadParamFloat3's push order), emits
                // an inline assignment to the `declaredPos` out-param the MOMENT this
                // instruction is reached (the direction doc's own inline-assignment argument:
                // GLSL locals/out-params stay in scope for the rest of the emitted function
                // body, so no early-exit machinery is needed to keep walking into the resolve
                // segment below), then rewrites curPos to sample in the translated frame — the
                // same translation evalRecipe's DeclarePosition case applies at eval time, so
                // the resolve segment's shape renders at the declared position on both paths.
                assert(stk.size() >= 3 && "DeclarePosition: emit-time value stack underflow");
                std::string dz = stk.back(); stk.pop_back();
                std::string dy = stk.back(); stk.pop_back();
                std::string dx = stk.back(); stk.pop_back();
                assert(emitDeclaredPositionOutParam &&
                       "DeclarePosition used but emitDeclaredPositionOutParam=false — the "
                       "emitted function has no out-param to assign it to");
                body += "  declaredPos = vec3(" + dx + ", " + dy + ", " + dz + ");\n";
                std::string pN = "pp" + std::to_string(n++);
                body += "  vec3 " + pN + " = " + curPos + " - declaredPos;\n";
                curPos = pN;
                break;
            }
            case SdfOpCode::PushFloat3: {          // push data[0..2] as x,y,z scalars
                std::string t = "t" + std::to_string(n++);
                body += "  vec3 " + t + " = vec3(" + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t + ".x");
                stk.push_back(t + ".y");
                stk.push_back(t + ".z");
                break;
            }
            case SdfOpCode::ComposeFloat3: {       // no-op: 3 scalars on stack are already vec3
                break;
            }
            case SdfOpCode::Passthrough: {         // pop 1, push 1 unchanged
                break;                             // top of stack is already the right value
            }
            case SdfOpCode::DecomposeFloat3: {     // pop float3 (vz,vy,vx), push one component
                assert(stk.size() >= 3 && "DecomposeFloat3: emit-time value stack underflow");
                std::string vz = stk.back(); stk.pop_back();
                std::string vy = stk.back(); stk.pop_back();
                std::string vx = stk.back(); stk.pop_back();
                int ch = (int)in.data[0]; // 0=x, 1=y, 2=z
                stk.push_back(ch == 0 ? vx : ch == 1 ? vy : vz);
                break;
            }
            // Float3 arithmetic — float3 is 3 consecutive scalars on string stack; x=deepest, z=top
            // Binary: pop b(bz,by,bx) then a(az,ay,ax), recompose vec3, call kernel, push result.xyz
            case SdfOpCode::Float3Add: {
                assert(stk.size() >= 6 && "Float3Add: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  vec3 " + t + " = SdfCore_Float3Add(vec3(" + ax + ", " + ay + ", " + az + "), vec3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            case SdfOpCode::Float3Sub: {           // non-commutative: a - b
                assert(stk.size() >= 6 && "Float3Sub: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  vec3 " + t + " = SdfCore_Float3Sub(vec3(" + ax + ", " + ay + ", " + az + "), vec3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            case SdfOpCode::Float3MulComponentWise: {
                assert(stk.size() >= 6 && "Float3MulComponentWise: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  vec3 " + t + " = SdfCore_Float3MulComponentWise(vec3(" + ax + ", " + ay + ", " + az + "), vec3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            case SdfOpCode::Float3Min: {
                assert(stk.size() >= 6 && "Float3Min: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  vec3 " + t + " = SdfCore_Float3Min(vec3(" + ax + ", " + ay + ", " + az + "), vec3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            case SdfOpCode::Float3Max: {
                assert(stk.size() >= 6 && "Float3Max: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  vec3 " + t + " = SdfCore_Float3Max(vec3(" + ax + ", " + ay + ", " + az + "), vec3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            // Float3ScalarMul: scalar=top, then vz,vy,vx -> push result
            case SdfOpCode::Float3ScalarMul: {
                assert(stk.size() >= 4 && "Float3ScalarMul: emit-time value stack underflow");
                std::string s  = stk.back(); stk.pop_back();
                std::string vz = stk.back(); stk.pop_back();
                std::string vy = stk.back(); stk.pop_back();
                std::string vx = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  vec3 " + t + " = SdfCore_Float3ScalarMul(vec3(" + vx + ", " + vy + ", " + vz + "), " + s + ");\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            // Float3Dot: pop b then a -> push scalar
            case SdfOpCode::Float3Dot: {
                assert(stk.size() >= 6 && "Float3Dot: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Float3Dot(vec3(" + ax + ", " + ay + ", " + az + "), vec3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t);
                break;
            }
            // Float3Normalize: pop vz,vy,vx -> push normalized vec3
            case SdfOpCode::Float3Normalize: {
                assert(stk.size() >= 3 && "Float3Normalize: emit-time value stack underflow");
                std::string vz = stk.back(); stk.pop_back();
                std::string vy = stk.back(); stk.pop_back();
                std::string vx = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  vec3 " + t + " = SdfCore_Float3Normalize(vec3(" + vx + ", " + vy + ", " + vz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            default:
                break;
        }
    }

    assert(!stk.empty() && "EmitProceduralFieldFunctionGlsl: empty value stack at return");
    std::string signature = emitDeclaredPositionOutParam
        ? "float sdfRecipe_" + std::to_string(recipeId) + "(vec3 p, float params[6], out vec3 declaredPos) {\n"
        : "float sdfRecipe_" + std::to_string(recipeId) + "(vec3 p, float params[6]) {\n";
    return signature
        + body
        + "  return " + stk.back() + ";\n"
        "}\n";
}

} // namespace Vixen::SVO::Recipe
