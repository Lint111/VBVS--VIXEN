#pragma once
#include "Recipe/SdfInstruction.h"
#include "Recipe/RecipeRegistry.h"  // IsValidSdfOpCode — the exact opcode set this emitter must cover
#include "Recipe/generated/GlyphOutlineInstance.g.hpp"
#include <cassert>
#include <algorithm>
#include "Recipe/generated/RecipeGlslLiterals.g.h"
#include <bit>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Vixen::SVO::Recipe {

// GLSL sibling of EmitProceduralComputeShader (SdfRecipeCodegen.h) — Lazy-Procedural-Delta-
// Baseline Inc1 M4 Task 8. Same emit-time simulation (value stack, position stack, DistScale
// stack with the |scale-1|>1e-4 multiply on RestorePos) but emits GLSL and returns ONLY a
// composable `float sdfRecipe_<id>(vec3 p, float params[6]) { ... }` function — no trace main
// (that's the caller's job: composing this + SdfCoreKernels.g.glsl + a wrapper compute shader,
// per Task 9's numerical-parity harness). Covers exactly the opcode set
// RecipeRegistry::IsValidSdfOpCode accepts (asserted at the call site by that same predicate,
// mirroring the HLSL emitter's `assert(paramMask == 0)`). `params` (Recipe-Parameterization
// M2 Task 5) is the per-instance dynamic-parameter array backing ReadParam/ReadParamFloat3 —
// every emitted function takes it uniformly (even one that never uses it), since
// evalRecipeField's switch dispatches to whichever sdfRecipe_<id> by recipeId and needs one
// call shape.
//
// Numeric operands use the generated binary32 formatter. uintBitsToFloat has float type,
// including for integer-valued operands, signed zero and subnormal words.
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
// Recipe-Nested-Invocation M1: `registry` backs InvokeRecipe's callee lookup at emit time —
// required (asserted) only if `prog` (or, recursively, any callee it invokes) actually
// contains an InvokeRecipe instruction; every pre-M1 call site (no InvokeRecipe anywhere in
// its program) compiles and emits unchanged with the nullptr default.
//
// Unroll-strategy choice (M1, per the direction doc's §1.3 — recursive-inlining vs
// call-to-already-unrolled-function): RECURSIVE INLINING, confirmed against this emitter's
// actual structure rather than assumed from the doc's non-binding recommendation. This
// emitter does emit-time symbolic execution over STRING expressions (`stk`/`curPos` are
// std::string, not real GLSL locals it has to worry about scoping) — walking a callee's
// bytecode is EXACTLY the same shape as walking the caller's own bytecode: append more `t<n>`/
// `pp<n>` lines to the SAME `body`, push the callee's final string expression onto the SAME
// `stk`. This makes recursive inlining not just "the more conservative first cut" but the
// STRUCTURALLY NATURAL fit — no new codegen concept (cross-function calls, callee parameter
// passing, a second function signature) is needed at all; the callee's walk is simply MORE
// instructions in the caller's single self-contained sdfRecipe_<id> function, preserving
// today's "one totally self-contained function per top-level recipe" shape exactly.
inline std::string EmitProceduralFieldFunctionGlsl(
    const SdfInstruction* prog,
    uint32_t count,
    uint32_t recipeId,
    bool emitDeclaredPositionOutParam = false,
    const Vixen::SVO::RecipeRegistry* registry = nullptr,
    std::span<const Yeroket::Sdf::Generated::GlyphOutlineInstance> glyphInstances = {})
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

    // Operands obey the binary32 transport contract (integer-word literals).
    const auto f = RecipeGlslFloatLiteral;

    // A GlyphOutline instruction indexes this fixed local array. Each row is the complete
    // typed GPU struct, including its own fixed-width [GpuArray] contour bucket.
    if (!glyphInstances.empty()) {
        body += "  GlyphOutlineInstance glyphOutlineInstances["
            + std::to_string(glyphInstances.size()) + "];\n";
        for (std::size_t glyphIndex = 0; glyphIndex < glyphInstances.size(); ++glyphIndex) {
            const auto& glyph = glyphInstances[glyphIndex];
            const std::string row = "glyphOutlineInstances[" + std::to_string(glyphIndex) + "]";
            const std::uint32_t segmentCount = std::min<std::uint32_t>(
                glyph.SegmentCount, static_cast<std::uint32_t>(Yeroket::Sdf::Generated::SegmentCapacity));
            body += "  " + row + ".SegmentCount = " + std::to_string(segmentCount) + "u;\n";
            body += "  " + row + ".BoundsMin.X = " + f(glyph.BoundsMin.X) + ";\n";
            body += "  " + row + ".BoundsMin.Y = " + f(glyph.BoundsMin.Y) + ";\n";
            body += "  " + row + ".BoundsMax.X = " + f(glyph.BoundsMax.X) + ";\n";
            body += "  " + row + ".BoundsMax.Y = " + f(glyph.BoundsMax.Y) + ";\n";
            body += "  " + row + ".FillRule = " + std::to_string(glyph.FillRule) + "u;\n";
            for (std::uint32_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
                const auto& segment = glyph.Segments[segmentIndex];
                const std::string prefix = row + ".Segments[" + std::to_string(segmentIndex) + "]";
                const auto emitPoint = [&](const char* name, const auto& point) {
                    body += "  " + prefix + "." + name + ".X = " + f(point.X) + ";\n";
                    body += "  " + prefix + "." + name + ".Y = " + f(point.Y) + ";\n";
                };
                emitPoint("P0", segment.P0);
                emitPoint("P1", segment.P1);
                emitPoint("P2", segment.P2);
                emitPoint("P3", segment.P3);
                body += "  " + prefix + ".Kind = " + std::to_string(segment.Kind) + "u;\n";
                body += "  " + prefix + ".ContourStart = " + std::to_string(segment.ContourStart) + "u;\n";
            }
        }
    }

    // Recursive-inlining walk (Recipe-Nested-Invocation M1): a self-recursive lambda so
    // InvokeRecipe can walk a callee's bytecode into the SAME body/stk/n/curPos state as the
    // outer walk, without duplicating this whole function. Captures everything by reference;
    // `self` is how a C++ lambda recurses into itself.
    auto walk = [&](auto&& self, const SdfInstruction* wprog, uint32_t wcount) -> void {
    for (uint32_t i = 0; i < wcount; ++i) {
        const SdfInstruction& in = wprog[i];
        assert(IsValidSdfOpCode(in.opCode) && "EmitProceduralFieldFunctionGlsl: unknown opcode");
        // paramMask!=0 is now legal exactly for ReadParam/ReadParamFloat3 (Recipe-
        // Parameterization M1 Task 2's registry allow-list) — every other opcode still
        // requires paramMask==0, mirroring RecipeRegistry::Register's own narrowed check.
        assert((in.paramMask == 0 ||
                static_cast<SdfOpCode>(in.opCode) == SdfOpCode::ReadParam ||
                static_cast<SdfOpCode>(in.opCode) == SdfOpCode::ReadParamFloat3) &&
               "ParamMask!=0 only valid on ReadParam/ReadParamFloat3");
        switch (static_cast<SdfOpCode>(in.opCode)) {
#include "Recipe/generated/SdfRecipeCodegenGlslDispatch.g.inc"
            default:
                break;
        }
    }
    };  // end walk lambda

    walk(walk, prog, count);

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
