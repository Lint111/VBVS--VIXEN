#pragma once
// HitAccumulation.h — Wavefront W3a: CPU mirror of the (recipeId, cell@mip)
// hierarchical hit-accumulation math (undertow
// docs/plans/2026-08-04-wavefront-recipe-shading.md § "W3 design", authoritative).
//
// SYNC CONTRACT: once W3b lands the GPU accumulate pass, every function below is
// a line-by-line twin of the GLSL (same discipline as Reservoir.h /
// ReservoirCombine.glsl — see that pair's header comments). Until then this
// header IS the specification: test_hit_accumulation_mirror.cpp red-greens the
// math BEFORE any GPU path exists (the slice plan's own TDD prescription).
//
// THE MODEL (architecture section, distilled):
// - A hit's cone FOOTPRINT (raySizeCoef·t — the same LOD function traversal
//   uses) selects a mip: footprint within detailSize0 ⇒ mip 0 (per-ray exact,
//   the fail-soft path); wider ⇒ ceil-log2 mip whose cell it aggregates into.
// - A cell entry is an AVERAGE SUM {count, Σposition, Σdirection, Σdistance,
//   Σthroughput}. recipeId is CATEGORICAL and lives in the KEY — a cell whose
//   footprint spans recipes splits by construction; nothing categorical ever
//   averages.
// - Sums are FIXED-POINT INTEGERS: position cell-LOCAL (bounded by cellSize ⇒
//   exact int arithmetic, no float-atomic device dependency on the GPU),
//   direction snorm, throughput unsigned-normalized-ish scale. Integer sums
//   make Merge exactly associative/commutative — wave order CANNOT change the
//   result, which is the whole point (GPU accumulation order is nondeterministic).
// - Resolve divides by count; the averaged direction SHORTENS when the cell's
//   hits disagree — |Σdir|/count is the Toksvig factor, fed to the shade as a
//   roughness widen (the artifact becomes the filter).

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>

namespace Vixen::SVO::HitAccum {

// Fixed-point scales (GPU twins must match EXACTLY).
inline constexpr int64_t kPosScale        = 1 << 16;  // position quantum = cellSize / kPosScale (cell-local)
inline constexpr int64_t kDirScale        = 1 << 15;  // direction quantum = 1 / kDirScale (snorm components)
inline constexpr int64_t kThroughputScale = 1 << 15;  // throughput quantum = 1 / kThroughputScale

struct CellKey {
    uint32_t   recipeId = 0;
    uint32_t   mip = 0;
    glm::ivec3 cell{0};
    bool operator==(const CellKey& o) const {
        return recipeId == o.recipeId && mip == o.mip && cell == o.cell;
    }
    bool operator!=(const CellKey& o) const { return !(*this == o); }
};

// The average-sum entry. int64 on the CPU mirror (headroom for the associativity
// proofs); the GPU twin bounds count and uses i32 pairs — same VALUES by contract.
struct AccumEntry {
    uint32_t count = 0;
    int64_t  sumPosQ[3]        = {0, 0, 0};  // cell-local fixed point
    int64_t  sumDirQ[3]        = {0, 0, 0};
    int64_t  sumDistQ          = 0;          // same quantum as position
    int64_t  sumThroughputQ[3] = {0, 0, 0};
};

struct HitSample {
    glm::vec3 worldPos{0.0f};
    glm::vec3 direction{0.0f};   // ANY unit vector; the GPU feeds the record's surface
                                 // NORMAL (W3c-2) — |avg| is then the classic Toksvig
                                 // normal-agreement factor (W3b summed view direction,
                                 // which is near-parallel within a cell: no signal)
    float     distance = 0.0f;   // path distance (the record's own LOD key)
    glm::vec3 throughput{0.0f};  // the GPU feeds the record's albedo (W3c-2)
};

struct ResolvedCell {
    glm::vec3 avgPos{0.0f};        // world-space
    glm::vec3 avgDir{0.0f};        // UNNORMALIZED average — |avgDir| IS the Toksvig factor
    float     avgDistance = 0.0f;
    glm::vec3 avgThroughput{0.0f};
    float     toksvigFactor = 1.0f;  // |avgDir| clamped to (0, 1]
};

// footprint <= detailSize0 -> 0 (per-ray exact); else ceil(log2(footprint/detailSize0)).
inline uint32_t SelectMip(float footprint, float detailSize0) {
    if (detailSize0 <= 0.0f || !(footprint > detailSize0)) return 0u;
    const float mip = std::ceil(std::log2(footprint / detailSize0));
    return mip <= 0.0f ? 0u : static_cast<uint32_t>(mip);
}

inline float CellSize(uint32_t mip, float detailSize0) {
    return std::ldexp(detailSize0, static_cast<int>(mip));
}

inline CellKey MakeCellKey(uint32_t recipeId, const glm::vec3& worldPos,
                           float footprint, float detailSize0) {
    CellKey key;
    key.recipeId = recipeId;
    key.mip = SelectMip(footprint, detailSize0);
    const float cellSize = CellSize(key.mip, detailSize0);
    key.cell = glm::ivec3(glm::floor(worldPos / cellSize));  // floor, never trunc-toward-zero
    return key;
}

// Quantize one hit into a cell's entry (cell-local position; the caller passes
// the SAME key MakeCellKey produced — the cell origin derives from it). Each
// sample is quantized ONCE; from then on everything is exact integer math.
inline void Accumulate(AccumEntry& entry, const HitSample& hit,
                       const CellKey& key, float detailSize0) {
    const float cellSize = CellSize(key.mip, detailSize0);
    const glm::vec3 cellOrigin = glm::vec3(key.cell) * cellSize;
    const glm::vec3 local = (hit.worldPos - cellOrigin) / cellSize;  // [0,1) inside the cell
    for (int i = 0; i < 3; ++i) {
        entry.sumPosQ[i]        += static_cast<int64_t>(std::llround(static_cast<double>(local[i]) * static_cast<double>(kPosScale)));
        entry.sumDirQ[i]        += static_cast<int64_t>(std::llround(static_cast<double>(hit.direction[i]) * static_cast<double>(kDirScale)));
        entry.sumThroughputQ[i] += static_cast<int64_t>(std::llround(static_cast<double>(hit.throughput[i]) * static_cast<double>(kThroughputScale)));
    }
    // Distance shares the position quantum (cellSize / kPosScale).
    entry.sumDistQ += static_cast<int64_t>(std::llround(static_cast<double>(hit.distance) * static_cast<double>(kPosScale) / static_cast<double>(cellSize)));
    entry.count += 1u;
}

// Exact integer merge — associative and commutative bit-for-bit by construction.
inline void MergeEntries(AccumEntry& into, const AccumEntry& from) {
    into.count += from.count;
    for (int i = 0; i < 3; ++i) {
        into.sumPosQ[i]        += from.sumPosQ[i];
        into.sumDirQ[i]        += from.sumDirQ[i];
        into.sumThroughputQ[i] += from.sumThroughputQ[i];
    }
    into.sumDistQ += from.sumDistQ;
}

inline ResolvedCell Resolve(const AccumEntry& entry, const CellKey& key, float detailSize0) {
    ResolvedCell r;
    if (entry.count == 0u) return r;
    const float cellSize = CellSize(key.mip, detailSize0);
    const glm::vec3 cellOrigin = glm::vec3(key.cell) * cellSize;
    const double invCount = 1.0 / static_cast<double>(entry.count);
    for (int i = 0; i < 3; ++i) {
        r.avgPos[i]        = cellOrigin[i] + static_cast<float>(static_cast<double>(entry.sumPosQ[i]) * invCount / static_cast<double>(kPosScale)) * cellSize;
        r.avgDir[i]        = static_cast<float>(static_cast<double>(entry.sumDirQ[i]) * invCount / static_cast<double>(kDirScale));
        r.avgThroughput[i] = static_cast<float>(static_cast<double>(entry.sumThroughputQ[i]) * invCount / static_cast<double>(kThroughputScale));
    }
    r.avgDistance = static_cast<float>(static_cast<double>(entry.sumDistQ) * invCount / static_cast<double>(kPosScale)) * cellSize;
    r.toksvigFactor = glm::clamp(glm::length(r.avgDir), 1e-4f, 1.0f);
    return r;
}

// --- Two-word packed key (W3b rev 2 — the diag's range finding) --------------
// The FIRST design packed the full key into one word with 6-bit deltas — sized
// against the SECONDARY cone coef (0.05). The PRIMARY coef is tan-based
// (~0.0016 at 500 px), and a mip's cells at their engaging distance span
// 2·tan(fov/2)/coef ≈ 520 cells — INDEPENDENT of detailSize0 (cellSize ≈
// t·coef cancels t). Single-word keys cannot address that. Rev 2:
//   keyLo = [31]=tag | [30:21]=dx | [20:11]=dy | [10:1]=dz | [0]=spare
//           (10-bit biased deltas, range ±kKeyDeltaMax = ±511) — CAS-claimed.
//   keyHi = [31:24]=recipeId | [23:20]=mip | rest spare — written by the claim
//           WINNER (atomicExchange); readers compare BOTH words, and a
//           mismatch (an in-flight claim or a delta collision across
//           recipe/mip) probes the NEXT slot — producing benign DUPLICATE-key
//           entries that resolve identically. No mis-merge; the diag gate
//           compares totals/histograms exactly and occupancy as ≥.
//           W3c-3 amendment: a racer seeing OUR key's TRANSIENT marker takes a
//           BOUNDED re-read (16 spins) on that slot before probing on — the
//           original probe-on rule MINTED ~3 fresh duplicate slots per frame
//           (measured occupancy creep), and each mint could flip which
//           duplicate a reader's probe selects. Read-side counterpart: the
//           resolve count-weighted-averages ALL live duplicates of a key
//           (selection-independent) instead of taking the first match.
// Anchoring: the FRUSTUM CENTER of the mip's engagement band (camera +
// forward·0.75·detail·2^mip/coef — band [t/2, t] centered), which halves the
// span: half-width ≈ tan(fov/2)/coef ≈ 264, band depth ≈ ±159, corner ≈ 308 —
// inside ±511 with ~1.65× margin at the 500-px reference.

inline constexpr uint32_t kKeyTagBit    = 1u << 31;
inline constexpr int32_t  kKeyDeltaMax  = 511;  // 10-bit biased: [-511, 511]
inline constexpr uint32_t kKeyRecipeMax = 255;  // 8 bits (keyHi)
inline constexpr uint32_t kKeyMipMax    = 15;   // 4 bits (keyHi)

// The per-mip anchor cell: frustum-center of mip's engagement band. coef must
// be the PRIMARY cone coefficient (RaySizeCoefNode's own value).
inline glm::ivec3 AnchorCell(const glm::vec3& camPos, const glm::vec3& camForward,
                             float coef, float detailSize0, uint32_t mip) {
    const float cellSize = CellSize(mip, detailSize0);
    const float bandCenterT = coef > 0.0f ? (0.75f * cellSize / coef) : 0.0f;
    const glm::vec3 center = camPos + camForward * bandCenterT;
    return glm::ivec3(glm::floor(center / cellSize));
}

// 0 = out-of-range (fail-soft; 0 stays the empty-slot CAS sentinel).
inline uint32_t PackCellKeyLo(const glm::ivec3& cell, const glm::ivec3& anchorCell) {
    const glm::ivec3 delta = cell - anchorCell;
    for (int i = 0; i < 3; ++i) {
        if (delta[i] < -kKeyDeltaMax || delta[i] > kKeyDeltaMax) return 0u;
    }
    const uint32_t dx = static_cast<uint32_t>(delta.x + kKeyDeltaMax);
    const uint32_t dy = static_cast<uint32_t>(delta.y + kKeyDeltaMax);
    const uint32_t dz = static_cast<uint32_t>(delta.z + kKeyDeltaMax);
    return kKeyTagBit | (dx << 21) | (dy << 11) | (dz << 1);
}

inline uint32_t PackCellKeyHi(uint32_t recipeId, uint32_t mip) {
    if (recipeId > kKeyRecipeMax || mip > kKeyMipMax) return 0u;  // caller fail-softs
    return (recipeId << 24) | (mip << 20);
}

// Returns false on an untagged lo word. The anchor must be the SAME AnchorCell
// the packer used (derivable: mip is in hi).
inline bool UnpackCellKey(uint32_t keyLo, uint32_t keyHi, const glm::ivec3& anchorCell,
                          CellKey& out) {
    if ((keyLo & kKeyTagBit) == 0u) return false;
    out.recipeId = (keyHi >> 24) & 0xFFu;
    out.mip = (keyHi >> 20) & 0xFu;
    const int32_t dx = static_cast<int32_t>((keyLo >> 21) & 0x3FFu) - kKeyDeltaMax;
    const int32_t dy = static_cast<int32_t>((keyLo >> 11) & 0x3FFu) - kKeyDeltaMax;
    const int32_t dz = static_cast<int32_t>((keyLo >> 1) & 0x3FFu) - kKeyDeltaMax;
    out.cell = anchorCell + glm::ivec3(dx, dy, dz);
    return true;
}

// Toksvig roughness widen: ft = |avg dir| in (0,1]; alpha'^2 = alpha^2 + (1-ft)/ft
// (clamped to [baseRoughness, 1]). ft==1 (perfect agreement) returns
// baseRoughness EXACTLY; widening is monotone as agreement drops.
inline float ToksvigWiden(float baseRoughness, float toksvigFactor) {
    if (toksvigFactor >= 1.0f) return baseRoughness;
    const float ft = glm::clamp(toksvigFactor, 1e-4f, 1.0f);
    const float variance = (1.0f - ft) / ft;
    const float widened = std::sqrt(baseRoughness * baseRoughness + variance);
    return glm::clamp(widened, baseRoughness, 1.0f);
}

} // namespace Vixen::SVO::HitAccum
