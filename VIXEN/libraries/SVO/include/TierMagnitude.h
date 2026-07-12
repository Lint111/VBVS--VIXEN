#pragma once
// TierMagnitude.h — Tiered ESVO Observer Addressing, Inc1 M2 (Task 4).
//
// Apparent-brightness falloff as a function of the composed distance Task 3
// (TierDirection.h's ComposeLocalDirection) produces, plus an optional
// light-delay/staleness term (design doc §7 step 1: "brightness falloff +
// optional light-delay term for the burn's staleness -- a physically-
// motivated detection floor, not required for v1").
//
// Formula choice: a standard inverse-square-style falloff,
//     apparentMagnitude(distanceCm, intrinsicBrightness) =
//         intrinsicBrightness / (1 + (distanceCm / referenceDistanceCm)^2)
// This is deliberately NOT real astrophysical magnitude (no log-scale
// Pogson-ratio units, no absolute-magnitude/luminosity-distance physics).
// The design doc explicitly does not mandate precise astrophysical units --
// just "direction + apparent magnitude" (plan Task 4) -- so the bar here is
// simplicity and monotonic plausibility (closer == brighter, falls off
// smoothly, never negative, well-defined at distance == 0), not radiometric
// accuracy. The `+1` in the denominator avoids a divide-by-zero/singularity
// at distanceCm == 0 (an object exactly at the observer's own local origin
// still returns a finite, maximal brightness == intrinsicBrightness, rather
// than +inf) while still behaving like a clean inverse-square law once
// distanceCm is a few multiples of referenceDistanceCm. A caller that later
// wants real astrophysical magnitudes should treat this as the "brightness
// falloff" building block, not replace it in place -- swapping in a
// logarithmic Pogson scale is a separate, deliberate future decision, not
// implied by anything this file does.
//
// Light-delay/staleness term: VIXEN has NO existing time-simulation or
// light-speed/propagation-delay plumbing anywhere in the codebase (searched
// via codegraph: EngineTime/Timer/GPUTimestampQuery/DeviceBudgetManager's
// frame timers are the full extent of "time" in this engine -- all
// frame-cadence/profiling clocks, none of them a simulation-time or
// light-speed model). Per the plan's own explicit warning ("confirm at
// implementation time whether such a time model exists at all, and if not,
// stub this term as a documented no-op rather than inventing a new
// time-simulation system this increment"), the staleness term below is a
// documented no-op: it is a real, always-present parameter (never removed
// from the signature, so a future increment doesn't need an ABI/call-site
// change to wire it up for real), defaults to 0 (no delay, i.e. today's
// only meaningful value), and its only effect when nonzero is a
// DOCUMENTED-INERT bookkeeping pass-through (see ApparentMagnitude below) --
// it does NOT compute an actual light-travel time from distance/speed-of-
// light, because VIXEN has no simulation-time axis to attach that delay to
// yet (there is no "current sim time" to subtract a computed delay from).
//
// Scope note (Tiered-ESVO-Inc1-Plan-2026-07.md §0): pure CPU math, no
// GPU/render/time-simulation-system dependency.

#include <algorithm>
#include <cstddef>

// This header uses std::max. When included after <windows.h> (pulled in transitively on the
// Windows build via Vulkan/GTest), the `min`/`max` function-like macros mangle unqualified
// calls into a syntax error. NOMINMAX only helps before windows.h is seen, which a header
// cannot guarantee, so drop the macros outright — no C++ code wants them. Same convention as
// GpuTraversalMirror.h.
#undef min
#undef max

namespace Vixen::SVO {

// Tunable reference distance (centimeters) at which apparent brightness has
// fallen to exactly half of intrinsicBrightness. Chosen at T0-planet-span
// order of magnitude (~10,000 km, see TierMath.h's re-derived T0 span) as a
// plausible "this is roughly where a bright fleet burn starts looking like
// a dim point of light" reference for the driving use case (§7 step 1) --
// not a physically-derived constant, just a single tunable knob so the
// falloff formula has exactly one free parameter, kept as a named constant
// (not hand-inlined) so a later increment can retune it without touching
// the formula itself.
inline constexpr double kDefaultReferenceDistanceCm = 1.0e9;  // 10,000 km

// Optional staleness parameter for ApparentMagnitude. `delaySeconds`
// defaults to 0.0 (disabled/inert -- see file header). Kept as its own
// struct (rather than a bare double) so the "is this actually enabled"
// intent is explicit at call sites and self-documenting in test code,
// rather than a magic 0.0 vs. non-zero convention buried in a raw
// parameter.
struct LightDelayStaleness {
    // Seconds of light-travel delay to apply. 0.0 == disabled: the term is
    // a true no-op (see ApparentMagnitude's doc comment for exactly what
    // "no-op" means here, since VIXEN has no time-simulation system yet to
    // wire a real delay into).
    double delaySeconds = 0.0;

    [[nodiscard]] bool IsEnabled() const { return delaySeconds > 0.0; }
};

// Brightness/magnitude falloff as a function of composed distance
// (TierDirection.h's ComposedDirection::distanceCm). `intrinsicBrightness`
// is the object's brightness at distance 0 (arbitrary units -- this is not
// calibrated to any real photometric scale, see file header). Monotonically
// non-increasing as distanceCm grows, for any non-negative intrinsicBrightness.
inline double ApparentMagnitude(double distanceCm, double intrinsicBrightness,
                                 double referenceDistanceCm = kDefaultReferenceDistanceCm) {
    const double d = std::max(distanceCm, 0.0);
    const double ref = std::max(referenceDistanceCm, 1e-9);  // guard div-by-zero on a degenerate reference
    const double ratio = d / ref;
    return intrinsicBrightness / (1.0 + ratio * ratio);
}

// ApparentMagnitude with an optional light-delay/staleness parameter.
//
// IMPORTANT — what "optional" means here: `staleness` is accepted and its
// `IsEnabled()`/`delaySeconds` are exposed on the return value so a caller
// (or a future increment) can observe what staleness WOULD have been
// requested, but this function does not alter the returned brightness
// value based on it in any way -- there is no simulation-time system in
// VIXEN today to compute "what the object's brightness/position was
// `delaySeconds` in the past," so doing anything else here would be
// inventing a time-simulation system inside a magnitude-falloff function,
// exactly what the plan warns against. This is the "documented no-op"
// choice: a real parameter (not deleted from the signature, so wiring a
// real delay model in later is a body-only change), inert today by
// construction, not by an accidental oversight.
struct StaleApparentMagnitude {
    double magnitude = 0.0;               // identical to ApparentMagnitude(...)'s result
    double appliedDelaySeconds = 0.0;      // echoes staleness.delaySeconds verbatim (no-op passthrough)
};

inline StaleApparentMagnitude ApparentMagnitudeWithStaleness(
    double distanceCm, double intrinsicBrightness, const LightDelayStaleness& staleness,
    double referenceDistanceCm = kDefaultReferenceDistanceCm) {
    StaleApparentMagnitude result;
    result.magnitude = ApparentMagnitude(distanceCm, intrinsicBrightness, referenceDistanceCm);
    // No-op passthrough: recorded, never fed back into `magnitude` above.
    result.appliedDelaySeconds = staleness.delaySeconds;
    return result;
}

}  // namespace Vixen::SVO
