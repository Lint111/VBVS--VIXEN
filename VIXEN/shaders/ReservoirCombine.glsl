// ============================================================================
// ReservoirCombine.glsl - shared RIS/WRS/MIS reservoir primitives (Sampled
// Lighting Inc3 M4/M5) — GPU twin of libraries/SVO/include/Reservoir.h
// ============================================================================
// SYNC CONTRACT: reservoirUpdate/reservoirUnbiasedWeight/reservoirCombine below are
// a line-by-line port of Reservoir.h's Reservoir::Update / Reservoir::UnbiasedWeight
// / Reservoir::Combine — see that file's header for the full WRS/MIS derivation
// (unbiasedness identity, temporal-combine-as-resumed-WRS-stream). Any change here
// must be mirrored there (test_reservoir_mirror.cpp's CPU-mirror tests are the
// red-green gate for this math, run BEFORE trusting this GPU path).
//
// Factored out at M5 so DirectLighting.comp's TEMPORAL combine and
// SpatialReuseShade.comp's SPATIAL combine call the SAME primitive (Reservoir::Combine
// is generic over "another reservoir stream to fold in" — it has no notion of
// temporal vs spatial, only "prev" vs "current" — see Reservoir.h's own Combine doc
// comment), per the plan's "reuse the same MIS balance-heuristic combine primitive"
// mandate: spatial reuse must not reimplement this math a second time.
//
// Requires Generated/ReservoirRecord.glsl to already be #included (ReservoirRecord
// struct) before this file.

// Small self-contained PCG-style hash RNG (no existing GLSL PRNG utility in this
// codebase) — seeded per-pixel-per-frame so RIS/spatial draws differ both across
// pixels and across frames.
uint pcgHash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Returns a fresh uniform float in [0,1) each call, advancing `state` (caller-owned).
float rngNextFloat(inout uint state) {
    state = pcgHash(state);
    return float(state) * (1.0 / 4294967296.0);  // 2^32
}

// WRS update (Reservoir::Update mirror): streams ONE candidate into the reservoir.
void reservoirUpdate(inout ReservoirRecord r, uint candidateIndex, float pHat, float sourcePdf, float u) {
    if (sourcePdf <= 0.0) return;

    float w = pHat / sourcePdf;
    r.weightSum += w;
    r.sampleCount += 1u;

    if (r.weightSum > 0.0 && u < (w / r.weightSum)) {
        r.y = candidateIndex;
        r.targetPdf = pHat;
    }
}

// W = (1/targetPdf) * (weightSum/sampleCount) (Reservoir::UnbiasedWeight mirror).
float reservoirUnbiasedWeight(ReservoirRecord r) {
    if (r.y == 0xFFFFFFFFu || r.sampleCount == 0u || r.targetPdf <= 0.0) return 0.0;
    return (1.0 / r.targetPdf) * (r.weightSum / float(r.sampleCount));
}

// Generic combine (Reservoir::Combine mirror): folds a validated OTHER reservoir into
// `current` via the "resume the same WRS stream" identity — used for BOTH temporal
// reuse (DirectLighting.comp: `other` = reprojected previous-frame reservoir) and
// spatial reuse (SpatialReuseShade.comp: `other` = a neighbor pixel's post-temporal
// reservoir). `pHatAtOtherY` is p_hat re-evaluated at THIS shading point/frame for
// `other.y` (the re-evaluation that keeps the combine unbiased under a moving target
// function — Reservoir.h's header).
void reservoirCombine(inout ReservoirRecord current, ReservoirRecord other, float pHatAtOtherY, float u) {
    if (other.y == 0xFFFFFFFFu || other.sampleCount == 0u || other.weightSum <= 0.0) return;

    current.weightSum += other.weightSum;
    current.sampleCount += other.sampleCount;

    if (current.weightSum > 0.0 && u < (other.weightSum / current.weightSum)) {
        current.y = other.y;
        current.targetPdf = pHatAtOtherY;
    }
}
