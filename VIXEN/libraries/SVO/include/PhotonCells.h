#pragma once

// PhotonCells.h — photon/irradiance world-cell cache, C0/C1.
//
// This is the CPU mirror of shaders/PhotonCellsCommon.glsl.  The cache is a
// sibling of HitAccumulation, but deliberately stores only exitant diffuse
// flux in this lane.  The six SH words are layout reservation and stay zero.
// The world key is absolute in v1: level 0 uses 0.5 world units and the
// origin is the only anchor.  The key's anchorId bits are reserved for the
// future multi-anchor seam.

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Vixen::SVO::PhotonCells {

inline constexpr float kCellSize0 = 0.5f;
inline constexpr uint32_t kCapacity = 131072u;
inline constexpr uint32_t kEntryWords = 16u;
inline constexpr uint32_t kEntryBytes = kEntryWords * sizeof(uint32_t);
inline constexpr uint32_t kMaxLevel = 15u;
inline constexpr uint32_t kAnchorId = 0u;
inline constexpr uint32_t kGenerationMask = (1u << 20u) - 1u;
inline constexpr uint32_t kTransientGeneration = kGenerationMask;
inline constexpr uint32_t kMaxAge = 1024u;
inline constexpr uint32_t kHalfGenerationRange = 1u << 19u;
inline constexpr float kFluxScale = 1024.0f;
inline constexpr float kRadianceClamp = 256.0f;
inline constexpr float kTemporalAlpha = 0.25f;
inline constexpr float kHistoryWeightCap = 4.0f;
inline constexpr uint32_t kKeyTagBit = 1u << 31u;
inline constexpr int32_t kKeyDeltaMax = 511;
inline constexpr uint32_t kKeyLevelMax = 15u;
inline constexpr uint32_t kProbeLimit = 32u;

struct CellKey {
    glm::ivec3 cell{0};
    uint32_t level = 0;

    bool operator==(const CellKey& other) const {
        return cell == other.cell && level == other.level;
    }
};

// Must remain 64 bytes / 16 words.  The six SH words are reserved headroom,
// not active coefficients in C0/C1, and are zeroed by construction/reclaim.
struct PhotonCellEntry {
    uint32_t keyLo = 0;
    uint32_t keyHi = 0;
    uint32_t count = 0;
    int32_t sumFlux[3] = {0, 0, 0};
    float history[3] = {0.0f, 0.0f, 0.0f};
    float historyW = 0.0f;
    uint32_t shReserved[6] = {0, 0, 0, 0, 0, 0};
};
static_assert(sizeof(PhotonCellEntry) == kEntryBytes, "PhotonCellEntry must be 64 bytes");

// Host-written params ring record.  It intentionally has no camera or scene
// state: cell addressing is world anchored and the deposit reads HitRecord plus
// the existing LightingConfig/ShadowConfig buffers.
struct PhotonCellParams {
    uint32_t generation = 1;
    float primaryCoef = 0.0f;
    float primaryBias = 0.0f;
    float cellSize0 = kCellSize0;
    std::array<float, 4> misc0{kTemporalAlpha, kRadianceClamp,
                                static_cast<float>(kMaxAge), 0.0f};
    std::array<float, 4> misc1{0.0f, 0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(PhotonCellParams) == 48, "PhotonCellParams must be 48 bytes");

inline uint32_t NextGeneration(uint32_t current) {
    uint32_t next = (current + 1u) & kGenerationMask;
    if (next == 0u || next == kTransientGeneration) next = 1u;
    return next;
}

inline uint32_t CurrentGeneration(uint32_t generation) {
    generation &= kGenerationMask;
    return (generation == 0u || generation == kTransientGeneration) ? 1u : generation;
}

inline uint32_t SelectLevel(float footprint, float cellSize0 = kCellSize0) {
    if (cellSize0 <= 0.0f || !(footprint > cellSize0)) return 0u;
    const float level = std::ceil(std::log2(footprint / cellSize0));
    return std::min(kMaxLevel, level <= 0.0f ? 0u : static_cast<uint32_t>(level));
}

inline float CellSize(uint32_t level, float cellSize0 = kCellSize0) {
    return std::ldexp(cellSize0, static_cast<int>(std::min(level, kMaxLevel)));
}

inline CellKey MakeCellKey(const glm::vec3& worldPos, float footprint,
                           float cellSize0 = kCellSize0) {
    CellKey key;
    key.level = SelectLevel(footprint, cellSize0);
    key.cell = glm::ivec3(glm::floor(worldPos / CellSize(key.level, cellSize0)));
    return key;
}

// v1 has one fixed world anchor at cell (0,0,0).  Keeping the argument makes
// the reservation explicit and keeps this function line-for-line with GLSL.
inline uint32_t PackCellKeyLo(const glm::ivec3& cell,
                              const glm::ivec3& anchorCell = glm::ivec3(0)) {
    const glm::ivec3 delta = cell - anchorCell;
    for (int axis = 0; axis < 3; ++axis) {
        if (delta[axis] < -kKeyDeltaMax || delta[axis] > kKeyDeltaMax) return 0u;
    }
    const uint32_t dx = static_cast<uint32_t>(delta.x + kKeyDeltaMax);
    const uint32_t dy = static_cast<uint32_t>(delta.y + kKeyDeltaMax);
    const uint32_t dz = static_cast<uint32_t>(delta.z + kKeyDeltaMax);
    return kKeyTagBit | (dx << 21u) | (dy << 11u) | (dz << 1u);
}

inline uint32_t PackCellKeyHiBase(uint32_t anchorId, uint32_t level) {
    if (anchorId > 0xffu || level > kKeyLevelMax) return 0u;
    return (anchorId << 24u) | (level << 20u);
}

inline uint32_t PackCellKeyHi(uint32_t anchorId, uint32_t level, uint32_t generation) {
    const uint32_t base = PackCellKeyHiBase(anchorId, level);
    const uint32_t gen = generation & kGenerationMask;
    if (base == 0u && (anchorId != 0u || level != 0u)) return 0u;
    if (gen == 0u || gen == kTransientGeneration) return 0u;
    return base | gen;
}

inline bool UnpackCellKey(uint32_t keyLo, uint32_t keyHi,
                          CellKey& out,
                          const glm::ivec3& anchorCell = glm::ivec3(0)) {
    if ((keyLo & kKeyTagBit) == 0u) return false;
    const uint32_t anchorId = (keyHi >> 24u) & 0xffu;
    const uint32_t level = (keyHi >> 20u) & 0xfu;
    if (anchorId != kAnchorId) return false;
    const int32_t dx = static_cast<int32_t>((keyLo >> 21u) & 0x3ffu) - kKeyDeltaMax;
    const int32_t dy = static_cast<int32_t>((keyLo >> 11u) & 0x3ffu) - kKeyDeltaMax;
    const int32_t dz = static_cast<int32_t>((keyLo >> 1u) & 0x3ffu) - kKeyDeltaMax;
    out.level = level;
    out.cell = anchorCell + glm::ivec3(dx, dy, dz);
    return true;
}

// Arithmetic parent operation for the v2 pyramid seam.  Explicit floor
// division keeps negative coordinates equivalent to GLSL's signed >> 1.
inline int32_t ParentCoordinate(int32_t coordinate) {
    return coordinate >= 0 ? coordinate / 2 : -(((-coordinate) + 1) / 2);
}

inline glm::ivec3 ParentCell(const glm::ivec3& cell) {
    return {ParentCoordinate(cell.x), ParentCoordinate(cell.y),
            ParentCoordinate(cell.z)};
}

inline uint32_t HashSlot(uint32_t keyLo, uint32_t keyHiBase) {
    return ((keyLo ^ (keyHiBase * 40503u)) * 2654435761u) & (kCapacity - 1u);
}

inline uint32_t ProbeStride(uint32_t keyLo) {
    return (keyLo >> 16u) | 1u;
}

inline uint32_t GenerationAge(uint32_t current, uint32_t stored) {
    return (current - stored) & kGenerationMask;
}

inline bool IsStale(uint32_t current, uint32_t stored,
                    uint32_t maxAge = kMaxAge) {
    if (stored == 0u || stored == kTransientGeneration) return true;
    const uint32_t age = GenerationAge(current, stored);
    return age > maxAge || age > kHalfGenerationRange;
}

inline int32_t QuantizeFlux(float flux, float clamp = kRadianceClamp) {
    const float bounded = std::clamp(flux, 0.0f, clamp);
    const double scaled = static_cast<double>(bounded) * static_cast<double>(kFluxScale);
    const double rounded = std::floor(scaled + 0.5);
    const double maxValue = static_cast<double>(std::numeric_limits<int32_t>::max());
    return static_cast<int32_t>(std::clamp(rounded, 0.0, maxValue));
}

inline void ClearEntry(PhotonCellEntry& entry) {
    entry = PhotonCellEntry{};
}

inline void DepositQuantized(PhotonCellEntry& entry, const glm::vec3& flux,
                             float clamp = kRadianceClamp) {
    for (int channel = 0; channel < 3; ++channel) {
        entry.sumFlux[channel] += QuantizeFlux(flux[channel], clamp);
    }
    ++entry.count;
}

inline void FoldEntry(PhotonCellEntry& entry, float alpha = kTemporalAlpha) {
    if (entry.count == 0u) return;
    const float invCount = 1.0f / static_cast<float>(entry.count);
    for (int channel = 0; channel < 3; ++channel) {
        const float mean = (static_cast<float>(entry.sumFlux[channel]) * invCount) / kFluxScale;
        if (entry.historyW == 0.0f) entry.history[channel] = mean;
        else entry.history[channel] += (mean - entry.history[channel]) * alpha;
    }
    entry.historyW = std::min(entry.historyW + 1.0f, kHistoryWeightCap);
    entry.count = 0u;
    entry.sumFlux[0] = entry.sumFlux[1] = entry.sumFlux[2] = 0;
}

} // namespace Vixen::SVO::PhotonCells
