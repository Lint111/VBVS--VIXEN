#pragma once
#include "Recipe/SdfInstruction.h"

// The generated kernel header below uses bare glm::min/glm::max/glm::abs heavily. When this
// header is reached after <windows.h> (pulled in transitively on the Windows build via
// Vulkan/GTest), the `min`/`max`/`abs` function-like macros mangle those calls into a syntax
// error (KI-017/KI-020 #2). NOMINMAX only helps before windows.h is seen, which a header cannot
// guarantee, so drop the macros outright right before the generated include — no C++ code wants
// them. Same convention as GpuTraversalMirror.h. Must precede the include below, not just
// follow this header's own includes, since the generated header is pulled in immediately here.
#undef min
#undef max
#undef abs

#include "Recipe/generated/SdfCoreKernels.g.hpp"   // Yeroket::Sdf::Generated::SdfCore_*
#include "Recipe/RecipeRegistry.h"  // Recipe-Nested-Invocation M1: InvokeRecipe's callee lookup
#include <glm/glm.hpp>
#include <cassert>
#include <cstdint>
#include <span>

namespace Vixen::SVO::Recipe {

// params: per-instance dynamic-parameter array backing ReadParam/ReadParamFloat3 (Recipe-
// Parameterization-Plan-2026-07 M1 Task 3). Defaults to an empty span so every pre-P4 call site
// compiles unchanged. Out-of-range reads fail safe to 0.0f (mirrors the Yeroket Burst reference's
// `baseIdx < ctx.Parameters.Length ? ... : 0f` pattern) rather than asserting/crashing, since
// content authoring will get an index wrong sometimes.
//
// outDeclaredPos (Recipe-Diversity-Stress-Scene-Inc6 M1 — spatial-contract meta/resolve
// prototype): when non-null, receives the world position captured by a DeclarePosition
// instruction encountered during the walk (mirroring the GLSL emitter's `out vec3` convention
// from Recipe-Spatial-Contract-Two-Pass-Culling-Direction-2026-07.md). Left untouched (caller's
// original value) if the program contains no DeclarePosition instruction — same "opt-in, not
// universal" contract the direction doc describes. Defaults to nullptr so every pre-Inc6 call
// site compiles unchanged.
//
// registry (Recipe-Nested-Invocation M1): backs InvokeRecipe's callee lookup — required
// (asserted) only if `prog` actually contains an InvokeRecipe instruction; every pre-M1 call
// site (no InvokeRecipe in its program) compiles and runs unchanged with the nullptr default.
inline float evalRecipe(const SdfInstruction* prog, uint32_t count, glm::vec3 p,
                         std::span<const float> params = {}, glm::vec3* outDeclaredPos = nullptr,
                         const RecipeRegistry* registry = nullptr) {
    float stack[64]; int sp = 0;
    glm::vec3 pos = p;                   // current sample point (mirrors C# VM ctx.Pos)
    glm::vec3 posStack[64]; int psp = 0; // domain-transform save stack (C# VM ctx.PosStack)
    float distScaleStack[64];            // distance-scale per saved frame (pushed 1.0f for non-scaling transforms; M4b Transform uses data[11])
    using namespace Yeroket::Sdf::Generated;
    for (uint32_t i = 0; i < count; ++i) {
        const SdfInstruction& in = prog[i];
        switch (static_cast<SdfOpCode>(in.opCode)) {
#include "Recipe/generated/SdfRecipeEvalDispatch.g.inc"
        }
    }
    assert(sp == 1 && "evalRecipe: expected exactly one value on stack at return");
    return stack[sp - 1];
}

} // namespace Vixen::SVO::Recipe
