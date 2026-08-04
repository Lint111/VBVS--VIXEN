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
    glm::vec3 direction{0.0f};   // unit hit direction
    float     distance = 0.0f;   // path distance (the record's own LOD key)
    glm::vec3 throughput{0.0f};
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

// --- Packed key (W3b's single-CAS GPU slot claim) ----------------------------
// The GPU hash table claims a slot with ONE 32-bit atomicCompSwap — portable
// (no 64-bit-atomic extension) and EXACT (no fingerprint mis-merge) because the
// FULL key fits one word: recipeId(8) | mip(4) | camera-anchored 6-bit signed
// cell deltas ×3 | a nonzero tag bit (0 stays the empty-slot sentinel). The
// 6-bit range is sufficient BY CONSTRUCTION: a mip only engages when
// footprint = dist·raySizeCoef exceeds detailSize0, so cellSize ≥ dist·coef and
// the frustum spans at most ~1/coef (~20 at the default 0.05) cells of that
// mip around the camera's own cell (the per-mip anchor). Anything outside the
// range packs to 0 — the fail-soft per-ray path, NEVER a wrapped/aliased key.

inline constexpr uint32_t kKeyTagBit    = 1u << 31;
inline constexpr int32_t  kKeyDeltaMax  = 31;   // 6-bit signed: [-31, 31] (−32 unused, keeps symmetry)
inline constexpr uint32_t kKeyRecipeMax = 255;  // 8 bits
inline constexpr uint32_t kKeyMipMax    = 15;   // 4 bits

// 0 = out-of-range (fail-soft). anchor = the camera's own cell at key.mip.
// Layout: [31]=tag, [30:23]=recipeId, [22:19]=mip, [18:13]/[12:7]/[6:1]=biased
// deltas (bias +kKeyDeltaMax maps [-31,31] into [0,62]), [0]=spare 0.
inline uint32_t PackCellKey(const CellKey& key, const glm::ivec3& anchorCell) {
    if (key.recipeId > kKeyRecipeMax || key.mip > kKeyMipMax) return 0u;
    const glm::ivec3 delta = key.cell - anchorCell;
    for (int i = 0; i < 3; ++i) {
        if (delta[i] < -kKeyDeltaMax || delta[i] > kKeyDeltaMax) return 0u;
    }
    const uint32_t dx = static_cast<uint32_t>(delta.x + kKeyDeltaMax);
    const uint32_t dy = static_cast<uint32_t>(delta.y + kKeyDeltaMax);
    const uint32_t dz = static_cast<uint32_t>(delta.z + kKeyDeltaMax);
    return kKeyTagBit |
           (key.recipeId << 23) |
           (key.mip << 19) |
           (dx << 13) | (dy << 7) | (dz << 1);
}

// Returns false on a 0/undecodable word. mip is supplied by the caller (the GPU
// reader iterates per-mip anchors; mip is also IN the packed word — both must
// agree or this returns false).
inline bool UnpackCellKey(uint32_t packed, const glm::ivec3& anchorCell,
                          uint32_t mip, CellKey& out) {
    if ((packed & kKeyTagBit) == 0u) return false;
    const uint32_t packedMip = (packed >> 19) & 0xFu;
    if (packedMip != mip) return false;
    out.recipeId = (packed >> 23) & 0xFFu;
    out.mip = packedMip;
    const int32_t dx = static_cast<int32_t>((packed >> 13) & 0x3Fu) - kKeyDeltaMax;
    const int32_t dy = static_cast<int32_t>((packed >> 7) & 0x3Fu) - kKeyDeltaMax;
    const int32_t dz = static_cast<int32_t>((packed >> 1) & 0x3Fu) - kKeyDeltaMax;
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
