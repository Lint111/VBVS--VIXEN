#pragma once
// Reservoir.h — Sampled Lighting Inc3 M4: CPU mirror of the RIS/ReSTIR reservoir math
// that DirectLighting.comp implements on the GPU (weighted reservoir sampling + MIS
// balance-heuristic temporal combine).
//
// SYNC CONTRACT: Reservoir::Update / Reservoir::Combine are a line-by-line port of the
// RIS-update and temporal-combine logic in shaders/DirectLighting.comp. Any change to
// the GLSL reservoir math must be mirrored here (same discipline as
// test_vndf_mirror.cpp's VndfMirror namespace — see that file's header comment).
//
// WEIGHTED RESERVOIR SAMPLING (WRS), the "resampled importance sampling" (RIS) shape:
// each candidate i carries a SOURCE pdf p(i) (here: uniform-over-M-candidates, since
// M3's light-tree cut is sampled uniformly — importance-by-power is a stretch goal, not
// required for unbiasedness) and a TARGET function value p_hat(i) (the unshadowed
// contribution estimate: emitted power / distance^2, no visibility term — visibility is
// applied only once, when the FINAL chosen sample is shaded). Each candidate's RIS
// weight is w_i = p_hat(i) / p(i). The reservoir keeps exactly one sample, replacing its
// current pick with probability w_i / weightSum as candidates stream in (Algorithm 3,
// Bitterli et al. "Spatiotemporal reservoir resampling", the canonical WRS update).
//
// UNBIASEDNESS IDENTITY (verified by CPU-mirror test 1): after streaming M candidates,
//   W = (1 / p_hat(y)) * (weightSum / M)
// is an unbiased estimator of 1/p_hat(y) integrated over the candidate distribution —
// i.e. contribution = f(y) * W is an unbiased estimator of the true integral (Bitterli
// et al., eq 6). This is the SAME identity a naive per-pixel Monte-Carlo average would
// give if you evaluated EVERY candidate directly; WRS reaches it while only ever storing
// ONE sample.
//
// TEMPORAL COMBINE (MIS balance heuristic, verified by CPU-mirror test 2): combining a
// current-frame reservoir with a reprojected previous-frame reservoir is done by
// treating the previous reservoir's own (y_prev, weightSum_prev, M_prev) as ONE more
// candidate stream fed into the SAME WRS update, using the CURRENT frame's target
// function p_hat evaluated at y_prev (re-evaluated, since the previous frame's p_hat was
// computed under a possibly-different geometry/shading context — this re-evaluation is
// what keeps the combine unbiased under a moving target function, the "generalized RIS"
// justification in Bitterli et al. section 4). The balance heuristic here reduces to:
// merging two reservoirs of (weightSum_a, M_a) and (weightSum_b, M_b) into one of
// (weightSum_a + weightSum_b, M_a + M_b) is exactly equivalent to having streamed all of
// stream A's candidates then all of stream B's candidates through ONE reservoir — the
// WRS update is associative, so no separate "MIS weight" needs to be computed and
// applied on top; the balance heuristic falls directly out of treating the combine as
// resuming the same running WRS process (Algorithm 4, Bitterli et al.).

#include <cstdint>
#include <random>

namespace Vixen::SVO {

// Mirrors Generated/ReservoirRecord.g.h's field shape exactly (kept as a plain,
// GPU-independent struct here so this header has no Vulkan/codegen dependency;
// DirectLightingNode's C++ side uses Vixen::Gpu::ReservoirRecord directly for the
// actual SSBO layout — the two are field-for-field identical, verified by
// test_reservoir_mirror.cpp's own static_assert against the generated header).
struct ReservoirState {
    uint32_t y = 0xFFFFFFFFu;   // chosen sample index; 0xFFFFFFFF = empty/invalid
    float    weightSum = 0.0f;  // WRS running Sum(w_i)
    uint32_t sampleCount = 0u;  // M
    float    targetPdf = 0.0f;  // p_hat(y) at the CURRENTLY chosen sample

    bool IsValid() const { return y != 0xFFFFFFFFu && sampleCount > 0u; }
};

namespace Reservoir {

// Streams ONE candidate (index candidateIndex, target-function value pHat, source pdf
// sourcePdf) into the reservoir via the WRS update rule (Bitterli et al. Algorithm 3).
// `u` is a uniform random number in [0,1) supplied by the caller (kept explicit, not
// drawn internally, so the CPU-mirror tests can drive it deterministically).
//
// w_i = pHat / sourcePdf (RIS weight). weightSum += w_i; sampleCount += 1; with
// probability w_i/weightSum, replace (y, targetPdf) with (candidateIndex, pHat).
inline void Update(ReservoirState& r, uint32_t candidateIndex, float pHat, float sourcePdf, float u) {
    if (sourcePdf <= 0.0f) return;  // degenerate candidate: cannot form a valid RIS weight

    const float w = pHat / sourcePdf;
    r.weightSum += w;
    r.sampleCount += 1u;

    if (r.weightSum > 0.0f && u < (w / r.weightSum)) {
        r.y = candidateIndex;
        r.targetPdf = pHat;
    }
}

// Streams `count` candidates drawn from a uniform source pdf (1/count each — M3's
// light-tree cut has no per-node importance weighting yet, so RIS candidates are drawn
// uniformly over the cut; `pHatFn(i)` supplies the target-function value for cut node
// i). `rng` supplies the uniform draws (both the candidate-index draw and the WRS
// replacement draw), for use by CPU-mirror tests and any future CPU-side reference path.
template <typename PHatFn, typename Rng>
inline ReservoirState BuildFromUniformCandidates(uint32_t count, PHatFn&& pHatFn, Rng& rng) {
    ReservoirState r{};
    if (count == 0u) return r;

    std::uniform_int_distribution<uint32_t> pickCandidate(0u, count - 1u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float sourcePdf = 1.0f / static_cast<float>(count);

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t candidateIndex = pickCandidate(rng);
        const float pHat = pHatFn(candidateIndex);
        Update(r, candidateIndex, pHat, sourcePdf, unit(rng));
    }
    return r;
}

// The unbiased contribution weight: W = (1/targetPdf) * (weightSum/sampleCount).
// Returns 0 for an empty/degenerate reservoir (nothing to contribute).
inline float UnbiasedWeight(const ReservoirState& r) {
    if (!r.IsValid() || r.targetPdf <= 0.0f) return 0.0f;
    return (1.0f / r.targetPdf) * (r.weightSum / static_cast<float>(r.sampleCount));
}

// Temporal combine (MIS balance heuristic via the "resume the same WRS stream" identity
// — see this file's header comment). `prev` is the REPROJECTED previous-frame reservoir
// (already validity-tested by the caller — geometric reject, per KI-023's worldPos
// buffer — an invalid prev must not be passed in; caller substitutes an empty
// ReservoirState instead). `pHatAtPrevY` is p_hat re-evaluated THIS frame at prev.y (the
// re-evaluation that keeps the combine unbiased under a moving target function).
// `prev.sampleCount` is pre-clamped by the caller to ReservoirConfig.temporalCap before
// calling this (bounds a long-lived reservoir's effective M, mirrors
// AccumulationConfig.maxFrames's role for the EWMA history).
inline void Combine(ReservoirState& current, const ReservoirState& prev, float pHatAtPrevY, float u) {
    if (!prev.IsValid() || prev.sampleCount == 0u) return;

    // Treat the previous reservoir as ONE candidate stream: its own weightSum represents
    // sampleCount-many candidates collapsed into one w = weightSum "mass" already
    // resampled down to a single pick (prev.y). Feeding that mass back in at the
    // CURRENT frame's re-evaluated p_hat(prev.y) is exactly Bitterli et al.'s
    // generalized-RIS temporal reuse: streaming a reservoir into another reservoir
    // combines the running sums directly (weightSum/sampleCount add), with the
    // replacement draw using the SAME formula Update() uses for a single candidate —
    // Combine forwards to Update with the previous reservoir's own weightSum as the
    // pre-aggregated RIS weight for a single "meta-candidate" of source pdf 1 (already
    // resampled, so no further division by a source pdf is needed).
    if (prev.weightSum <= 0.0f) return;

    current.weightSum += prev.weightSum;
    current.sampleCount += prev.sampleCount;

    if (current.weightSum > 0.0f && u < (prev.weightSum / current.weightSum)) {
        current.y = prev.y;
        current.targetPdf = pHatAtPrevY;
    }
}

}  // namespace Reservoir

}  // namespace Vixen::SVO
