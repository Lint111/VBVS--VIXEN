#pragma once
// FootprintRegime.h — E6-T1: shared CPU/GPU footprint classifier (stencil doc's
// prerequisite slice; residency-unification design's step 2).
//
// This is the CPU twin of shaders/SceneBindings.glsl's classifyFootprintRegime (defined
// just above that file's #include "TraceWorld.glsl", ~line 494), which is itself the
// exact three-comparison arithmetic that used to be duplicated inline at that file's
// entry dispatch and TraceWorld.glsl's two composition-probe call sites. Both sides
// implement the SAME formula from the residency design's proposal
// (Vixen-Docs/Deep-Field-Residency-Unification-2026-08.md:176-194):
//
//   footprint = worldDist * raySizeCoef + raySizeBias
//   if raySizeCoef <= 0 || footprint < cellWorldSize/8:  Surface  (regime 1)
//   if footprint < cosmicK * cellWorldSize:               MipHit   (regime 2)
//   else:                                                 Cosmic   (regime 3)
//
// Factored out as a pure, dependency-free function (no node/GPU/graph types) — the same
// pattern ResidencyTrigger.h/ResolvableLevel.h/ResidencyDefault.h already use — so it is
// independently unit-testable and so a future residency-decision consumer (step 4, NOT
// this slice) can call it CPU-side with the same push-constant values the shader already
// receives per frame.
//
// GLSL is the source formula; there is no C++->GLSL transpiler for this function class,
// so the two sides are hand-synced (same manual parity discipline this epoch already
// practices elsewhere, per Deep-Field-Mip-Accessor-Policy-2026-08.md:200). This slice
// does NOT wire this CPU twin into any live residency decision — that is step 4, future
// scope. It also does not add stencil storage.
//
// The "/8.0" brick divisor and cosmicK are named constants here (kBrickDivisor /
// caller-supplied cosmicK) rather than re-derived — the constants-auditable step 3 of the
// slice plan. GLSL's literal `8.0` and this header's kBrickDivisor must be kept equal by
// eye (both are dimensionless divisors, not push-constant-sourced, so no runtime parity
// probe is possible without one of them becoming a shared constant emitted into both
// languages — out of scope for this extraction).  E6-T2 consumes this twin from
// the live residency trigger.

namespace Vixen::SVO {

enum class FootprintRegime : unsigned int {
    Surface = 1,
    MipHit  = 2,
    Cosmic  = 3,
};

// Matches shaders/SceneBindings.glsl's classifyFootprintRegime brick-cell subdivision
// literal (the entry-dispatch gate's "/8.0"). Named here as the auditable half of the
// C++<->GLSL constant contract (slice plan step 3); the GLSL side keeps the bare literal
// (shader constant folding, no include for a single scalar).
inline constexpr float kBrickDivisor = 8.0f;

// Pure classifier — identical arithmetic to the GLSL twin, evaluated in double order and
// operator set (no change of precision/associativity from the shader's float ops).
inline FootprintRegime ClassifyFootprintRegime(float worldDist, float cellWorldSize,
                                                float raySizeCoef, float raySizeBias,
                                                float cosmicK) {
    float footprint = worldDist * raySizeCoef + raySizeBias;
    if (raySizeCoef <= 0.0f || footprint < cellWorldSize / kBrickDivisor) {
        return FootprintRegime::Surface;
    }
    if (footprint < cosmicK * cellWorldSize) {
        return FootprintRegime::MipHit;
    }
    return FootprintRegime::Cosmic;
}

}  // namespace Vixen::SVO
