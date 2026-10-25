#pragma once
#include "Recipe/SdfInstruction.h"
#include "Recipe/RecipeRegistry.h"  // IsValidSdfOpCode — the exact opcode set this emitter must cover
#include "Recipe/generated/GlyphOutlineInstance.g.hpp"
#include "Recipe/generated/PathInstance.g.hpp"
#include <cassert>
#include <algorithm>
#include "Recipe/generated/RecipeGlslLiterals.g.h"
#include <bit>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
    std::span<const Yeroket::Sdf::Kernels::GlyphOutlineInstance> glyphInstances = {},
    uint32_t glyphInstanceBase = 0,
    std::span<const Yeroket::Sdf::Kernels::PathInstance> pathInstances = {},
    bool readInstructionOperands = false)
{
    std::vector<std::string> stk;
    std::string body;
    int n = 0;

    // emit-time position stack: mirrors pos/posStack in evalRecipe (C# VM ctx.Pos/PosStack)
    std::string curPos = "p";
    std::vector<std::string> posSaveStk;
    // emit-time distScale stack: 1.0f for non-scaling transforms; M4b Transform pushes data[11]
    // At RestorePos: if |scale-1|>1e-4 emit a multiply to scale the TOS distance before popping.
    std::vector<std::pair<float, std::string>> distScaleSaveStk;

    // Operands obey the binary32 transport contract (integer-word literals).
    const auto f = RecipeGlslFloatLiteral;

    // GlyphOutline data lives in the shared runtime SSBO declared by the shader source. Keep
    // the span in the API for callers that already provide the entry payload, but never emit
    // its rows as GLSL literals: those literals are what makes the driver unroll the SDF and
    // grow its compiler cache. The generated opcode dispatch still emits the indexed access;
    // the walk below rebases that local index into the flattened SSBO.
    (void)glyphInstances;

    const auto glyphBaseForProgram = [&](const SdfInstruction* wprog) {
        // RecipeRegistry rewrites every GlyphOutline operand to the one shared blob's
        // global index at registration. Nested recipe walks therefore must not apply the
        // old per-entry prefix sum. Keep the legacy base parameter for standalone emitters
        // and tests that intentionally pass local rows without a registry.
        if (registry) return 0U;
        (void)wprog;
        return glyphInstanceBase;
    };

    // A PathDistance instruction indexes this fixed local array, laid out like the glyph rows.
    if (!pathInstances.empty()) {
        body += "  PathInstance pathInstances[" + std::to_string(pathInstances.size()) + "];\n";
        for (std::size_t pathIndex = 0; pathIndex < pathInstances.size(); ++pathIndex) {
            const auto& path = pathInstances[pathIndex];
            const std::string row = "pathInstances[" + std::to_string(pathIndex) + "]";
            const std::uint32_t segmentCount = std::min<std::uint32_t>(
                path.SegmentCount, static_cast<std::uint32_t>(Yeroket::Sdf::Kernels::PathSegmentCapacity));
            body += "  " + row + ".SegmentCount = " + std::to_string(segmentCount) + "u;\n";
            body += "  " + row + ".Dimension = " + std::to_string(path.Dimension) + "u;\n";
            body += "  " + row + ".Closed = " + std::to_string(path.Closed) + "u;\n";
            body += "  " + row + ".FillRule = " + std::to_string(path.FillRule) + "u;\n";
            for (std::uint32_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
                const auto& segment = path.Segments[segmentIndex];
                const std::string prefix = row + ".Segments[" + std::to_string(segmentIndex) + "]";
                const auto emitPoint = [&](const char* name, const auto& point) {
                    body += "  " + prefix + "." + name + ".X = " + f(point.X) + ";\n";
                    body += "  " + prefix + "." + name + ".Y = " + f(point.Y) + ";\n";
                    body += "  " + prefix + "." + name + ".Z = " + f(point.Z) + ";\n";
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
    std::vector<SdfInstruction> adjustedProgram(wprog, wprog + wcount);
    const uint32_t base = glyphBaseForProgram(wprog);
    for (std::uint32_t instructionIndex = 0; instructionIndex < adjustedProgram.size(); ++instructionIndex) {
        auto& in = adjustedProgram[instructionIndex];
        if (static_cast<SdfOpCode>(in.opCode) == SdfOpCode::GlyphOutline) {
            const uint32_t localIndex = std::bit_cast<uint32_t>(in.data[0]);
            in.data[0] = std::bit_cast<float>(localIndex + base);
        }
        // R475.3: a closed unroll can retain its uploaded tape as the operand source.
        // InvokeRecipe must be closed before binding, exactly as for EvalRecipeTape.
        assert((!readInstructionOperands || in.opCode != static_cast<uint8_t>(SdfOpCode::InvokeRecipe)) &&
            "buffer operands require UnrollRecipeInstructions first");
        const auto d = [&](std::uint32_t slot) {
            return readInstructionOperands
                ? "recipeTape[recipeOperandBase + " + std::to_string(instructionIndex)
                    + "u].data[" + std::to_string(slot) + "]"
                : f(in.data[slot]);
        };
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
    if (readInstructionOperands) {
        const auto insertion = signature.find(", out vec3");
        signature.insert(insertion == std::string::npos ? signature.find(") {") : insertion,
            ", uint recipeOperandBase");
    }
    return signature
        + body
        + "  return " + stk.back() + ";\n"
        "}\n";
}

// SdfCoreKernels.g.glsl reads the shared glyph outline blob through `glyphOutlineInstances`, so a
// shader that inlines the kernels must declare that buffer where the kernels can see it: after the
// GlyphOutlineInstance struct they define and before their first function. Returns the kernel text
// with the declaration inserted at that point, or the text unchanged when it has no such struct.
inline std::string WithGlyphOutlineBuffer(const std::string& sdfCoreKernelsGlsl, uint32_t binding,
                                          uint32_t set = 0) {
    constexpr std::string_view marker = "#endif // GLYPHOUTLINEINSTANCE_GLSL\n";
    const auto at = sdfCoreKernelsGlsl.find(marker);
    if (at == std::string::npos) return sdfCoreKernelsGlsl;
    std::string out = sdfCoreKernelsGlsl;
    out.insert(at + marker.size(),
        "layout(set = " + std::to_string(set) + ", std430, binding = " + std::to_string(binding) +
        ") readonly buffer GlyphOutlineBuffer {\n"
        "    GlyphOutlineInstance glyphOutlineInstances[];\n"
        "};\n");
    return out;
}

} // namespace Vixen::SVO::Recipe
