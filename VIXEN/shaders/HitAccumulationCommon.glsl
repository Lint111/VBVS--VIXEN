// ============================================================================
// HitAccumulationCommon.glsl — Wavefront W3b: the GPU twin of
// libraries/SVO/include/HitAccumulation.h.
// ============================================================================
// SYNC CONTRACT: every function and constant below is a line-by-line twin of
// HitAccumulation.h (the CPU mirror IS the specification —
// test_hit_accumulation_mirror.cpp red-greens the math; the same discipline as
// ReservoirCombine.glsl / Reservoir.h). Any change here must be mirrored there
// FIRST and re-greened. Quantization note: the CPU spec rounds via double
// llround; this twin rounds via float round() — count-based verification is
// rounding-immune (counts don't depend on the quantum edge), sum comparisons
// tolerate ±1 quantum per sample.
//
// The table entry is ALL 32-bit words so every mutation is a plain int atomic:
// packedKey claimed by ONE atomicCompSwap (the whole key fits one word — see
// PackCellKey's layout comment in the header), sums accumulated by atomicAdd.
// i32 sum headroom: |per-sample quantum| <= 2^16 ⇒ safe to 2^15 samples/cell
// per clear — orders beyond any per-frame cell population.

#ifndef HIT_ACCUMULATION_COMMON_GLSL
#define HIT_ACCUMULATION_COMMON_GLSL

// Fixed-point scales — MUST equal HitAccumulation.h's kPosScale/kDirScale/
// kThroughputScale exactly.
const float kHitAccumPosScale        = 65536.0;  // 1 << 16
const float kHitAccumDirScale        = 32768.0;  // 1 << 15
const float kHitAccumThroughputScale = 32768.0;  // 1 << 15

// Two-word packed key (rev 2 — the diag's range finding; MUST equal the
// header's PackCellKeyLo/PackCellKeyHi/AnchorCell exactly):
//   keyLo = [31]=tag | [30:21]=dx | [20:11]=dy | [10:1]=dz | [0]=spare
//           (10-bit biased deltas ±511) — the CAS-claim word.
//   keyHi = [31:24]=recipeId | [23:20]=mip — written by the claim WINNER;
//           readers compare BOTH words, mismatch probes on (benign duplicate
//           entries that resolve identically).
const uint kHitAccumKeyTagBit   = 0x80000000u;
const int  kHitAccumKeyDeltaMax = 511;
const uint kHitAccumKeyRecipeMax = 255u;
const uint kHitAccumKeyMipMax    = 15u;

// Table capacity — the C++ side sizes the buffer from the SAME number
// (VulkanGraphApplication.h kHitAccumTableCapacity; a size-mismatch is caught
// by the diag readback's occupancy scan, not silently).
const uint kHitAccumTableCapacity = 65536u;
const uint kHitAccumProbeLimit    = 32u;   // double-hashed probes before fail-soft drop

struct HitAccumEntryGpu {
    uint keyLo;       // 0 = empty (the CAS sentinel)
    uint keyHi;       // winner-written recipe|mip
    uint count;
    int  sumPosQX; int sumPosQY; int sumPosQZ;
    int  sumDirQX; int sumDirQY; int sumDirQZ;
    int  sumDistQ;
    int  sumTpQX; int sumTpQY; int sumTpQZ;
};  // 13 words = 52 B

// --- Twins of HitAccumulation.h -------------------------------------------

// footprint <= detailSize0 -> 0 (per-ray exact); else ceil(log2(ratio)).
uint HitAccumSelectMip(float footprint, float detailSize0) {
    if (detailSize0 <= 0.0 || !(footprint > detailSize0)) return 0u;
    float mip = ceil(log2(footprint / detailSize0));
    return mip <= 0.0 ? 0u : uint(mip);
}

float HitAccumCellSize(uint mip, float detailSize0) {
    return detailSize0 * float(1u << mip);   // ldexp twin (mip <= 15 by key range)
}

ivec3 HitAccumCellCoord(vec3 worldPos, float cellSize) {
    return ivec3(floor(worldPos / cellSize)); // floor, never trunc-toward-zero
}

// The per-mip anchor cell: frustum-center of the mip's engagement band —
// AnchorCell's twin (coef = the PRIMARY cone coefficient).
ivec3 HitAccumAnchorCell(vec3 camPos, vec3 camForward, float coef, float detailSize0, uint mip) {
    float cellSize = HitAccumCellSize(mip, detailSize0);
    float bandCenterT = coef > 0.0 ? (0.75 * cellSize / coef) : 0.0;
    vec3 center = camPos + camForward * bandCenterT;
    return ivec3(floor(center / cellSize));
}

// 0 = out-of-range (fail-soft; 0 stays the empty-slot CAS sentinel).
uint HitAccumPackCellKeyLo(ivec3 cell, ivec3 anchorCell) {
    ivec3 delta = cell - anchorCell;
    if (any(lessThan(delta, ivec3(-kHitAccumKeyDeltaMax))) ||
        any(greaterThan(delta, ivec3(kHitAccumKeyDeltaMax)))) return 0u;
    uvec3 d = uvec3(delta + ivec3(kHitAccumKeyDeltaMax));
    return kHitAccumKeyTagBit | (d.x << 21) | (d.y << 11) | (d.z << 1);
}

uint HitAccumPackCellKeyHi(uint recipeId, uint mip) {
    if (recipeId > kHitAccumKeyRecipeMax || mip > kHitAccumKeyMipMax) return 0u;
    return (recipeId << 24) | (mip << 20);
}

// Knuth multiplicative hash into the table (both words folded in).
uint HitAccumSlotOf(uint keyLo, uint keyHi) {
    return ((keyLo ^ (keyHi * 40503u)) * 2654435761u) % kHitAccumTableCapacity;
}

// Double-hash probe stride: ODD (capacity is a power of two, so any odd stride
// cycles the whole table) — kills the linear-probe clustering the diag measured
// (~9K/frame probe-race losses on structured sequential cell keys).
uint HitAccumProbeStride(uint keyLo) {
    return ((keyLo >> 16) | 1u);
}

#endif // HIT_ACCUMULATION_COMMON_GLSL
