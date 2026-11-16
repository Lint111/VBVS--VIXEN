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

#include "Recipe/generated/GlyphOutlineInstance.g.hpp"
#include "Recipe/generated/PathInstance.g.hpp"
#include "Recipe/generated/SdfCoreKernels.g.hpp"   // Yeroket::Sdf::Generated::SdfCore_*
#include "Recipe/RecipeRegistry.h"  // Recipe-Nested-Invocation M1: InvokeRecipe's callee lookup
#include <glm/glm.hpp>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <stdexcept>

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
                         const RecipeRegistry* registry = nullptr,
                         std::span<const Yeroket::Sdf::Kernels::GlyphOutlineInstance> glyphInstances = {},
                         std::span<const Yeroket::Sdf::Kernels::PathInstance> pathInstances = {},
                         const void* elementRow = nullptr, uint32_t elementStride = 0u,
                         const uint32_t* planSlots = nullptr, uint32_t planSlotCount = 0u) {
    float stack[64]; int sp = 0;
    // The generated kernel dispatch carries MaterialId as a categorical sidecar even though this
    // legacy scalar API returns distance only. Keep the sidecar state local so generated cases
    // can preserve it through combines without changing this evaluator's public ABI.
    float materialStack[64]{};
    bool materialPresent[64]{};
    glm::vec3 pos = p;                   // current sample point (mirrors C# VM ctx.Pos)
    glm::vec3 posStack[64]; int psp = 0; // domain-transform save stack (C# VM ctx.PosStack)
    float distScaleStack[64];            // distance-scale per saved frame (pushed 1.0f for non-scaling transforms; M4b Transform uses data[11])
    const auto* elementRowBytes = static_cast<const unsigned char*>(elementRow);
    (void)elementStride;
    using namespace Yeroket::Sdf::Kernels;
    for (uint32_t i = 0; i < count; ++i) {
        const SdfInstruction& in = prog[i];
        switch (static_cast<SdfOpCode>(in.opCode)) {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4101)
#endif
#include "Recipe/generated/SdfRecipeEvalDispatch.g.inc"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
        default:
            // v5 document-only overlay instructions are expanded by the generated document
            // flattener before CPU evaluation. Keep direct evaluator calls fail-loud if one
            // reaches this legacy runtime without that lowering step.
            assert(false && "unsupported layer-port or set opcode in direct recipe evaluation");
            break;
        }
    }
    assert(sp == 1 && "evalRecipe: expected exactly one value on stack at return");
    return stack[sp - 1];
}

} // namespace Vixen::SVO::Recipe
