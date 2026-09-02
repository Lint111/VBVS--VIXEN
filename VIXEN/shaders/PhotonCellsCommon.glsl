// PhotonCellsCommon.glsl — C0/C1 photon world-cell cache.
// CPU twin: libraries/SVO/include/PhotonCells.h.
//
// The table is a HitAccum-style open-addressed cache.  World cells are
// absolute in v1, with a fixed origin anchor and a 20-bit cache generation.
// keyHi's transient generation keeps readers from observing a half-cleared
// slot.  The six SH words are reservation only and remain zero.

#ifndef VIXEN_PHOTON_CELLS_COMMON_GLSL
#define VIXEN_PHOTON_CELLS_COMMON_GLSL

const uint kPhotonCellCapacity = 131072u;
const uint kPhotonCellEntryWords = 16u;
const uint kPhotonCellMaxLevel = 15u;
const uint kPhotonCellGenerationMask = 0xFFFFFu;
const uint kPhotonCellTransientGeneration = 0xFFFFFu;
const uint kPhotonCellMaxAge = 1024u;
const uint kPhotonCellHalfGenerationRange = 0x80000u;
const uint kPhotonCellKeyTagBit = 0x80000000u;
const int kPhotonCellKeyDeltaMax = 511;
const uint kPhotonCellProbeLimit = 32u;
const float kPhotonCellFluxScale = 1024.0;
const float kPhotonCellDefaultSize0 = 0.5;
const float kPhotonCellDefaultAlpha = 0.25;
const float kPhotonCellDefaultClamp = 256.0;
const float kPhotonCellHistoryWeightCap = 4.0;
const float kPhotonCellPi = 3.14159265358979323846;

struct PhotonCellEntry {
    uint keyLo;
    uint keyHi;
    uint count;
    int sumFluxR;
    int sumFluxG;
    int sumFluxB;
    float historyR;
    float historyG;
    float historyB;
    float historyW;
    uint shReserved[6];
};

struct PhotonCellParams {
    uint generation;
    float primaryCoef;
    float primaryBias;
    float cellSize0;
    vec4 misc0; // alpha, radiance clamp, max age, spare
    vec4 misc1; // reserved
};

// Keep the standalone shaders on one stable descriptor contract.  Unused
// interface blocks are dead-stripped by SPIR-V reflection, so clear/fold only
// expose the table/params bindings while deposit exposes the complete set.
#include "Generated/LightingConfig.glsl"
#include "HitRecord.glsl"
#include "Generated/ShadowConfig.glsl"
layout(std430, binding = 0) buffer PhotonCellTable {
    PhotonCellEntry photonCells[];
};
layout(std430, binding = 1) readonly buffer PhotonCellParamsBuffer {
    PhotonCellParams photonCellParams;
};
layout(std430, binding = 2) readonly buffer HitRecordBuffer {
    HitRecord hitRecords[];
};
layout(std430, binding = 3) readonly buffer LightingConfigSSBO {
    LightingConfig lightingConfig;
};
layout(std430, binding = 4) readonly buffer ShadowConfigSSBO {
    ShadowConfig shadowConfig;
};

uint PhotonCellGeneration() {
    uint generation = photonCellParams.generation & kPhotonCellGenerationMask;
    return (generation == 0u || generation == kPhotonCellTransientGeneration) ? 1u : generation;
}

uint PhotonCellSelectLevel(float footprint, float cellSize0) {
    if (cellSize0 <= 0.0 || !(footprint > cellSize0)) return 0u;
    float level = ceil(log2(footprint / cellSize0));
    return min(kPhotonCellMaxLevel, level <= 0.0 ? 0u : uint(level));
}

float PhotonCellSize(uint level, float cellSize0) {
    return ldexp(cellSize0, int(min(level, kPhotonCellMaxLevel)));
}

ivec3 PhotonCellCoord(vec3 worldPos, float cellSize) {
    return ivec3(floor(worldPos / cellSize));
}

uint PhotonCellPackKeyLo(ivec3 cell) {
    ivec3 delta = cell; // v1 fixed origin anchor; anchorId remains reserved in keyHi.
    if (any(lessThan(delta, ivec3(-kPhotonCellKeyDeltaMax))) ||
        any(greaterThan(delta, ivec3(kPhotonCellKeyDeltaMax)))) return 0u;
    uvec3 biased = uvec3(delta + ivec3(kPhotonCellKeyDeltaMax));
    return kPhotonCellKeyTagBit | (biased.x << 21u) |
           (biased.y << 11u) | (biased.z << 1u);
}

uint PhotonCellPackKeyHiBase(uint level) {
    return min(level, kPhotonCellMaxLevel) << 20u;
}

uint PhotonCellHashSlot(uint keyLo, uint keyHiBase) {
    return ((keyLo ^ (keyHiBase * 40503u)) * 2654435761u) & (kPhotonCellCapacity - 1u);
}

uint PhotonCellProbeStride(uint keyLo) {
    return (keyLo >> 16u) | 1u;
}

uint PhotonCellGenerationAge(uint current, uint stored) {
    return (current - stored) & kPhotonCellGenerationMask;
}

bool PhotonCellIsStale(uint current, uint stored) {
    if (stored == 0u || stored == kPhotonCellTransientGeneration) return true;
    uint age = PhotonCellGenerationAge(current, stored);
    uint maxAge = uint(max(photonCellParams.misc0.z, 0.0));
    return age > maxAge || age > kPhotonCellHalfGenerationRange;
}

void PhotonCellZeroPayload(uint slot) {
    photonCells[slot].count = 0u;
    photonCells[slot].sumFluxR = 0;
    photonCells[slot].sumFluxG = 0;
    photonCells[slot].sumFluxB = 0;
    photonCells[slot].historyR = 0.0;
    photonCells[slot].historyG = 0.0;
    photonCells[slot].historyB = 0.0;
    photonCells[slot].historyW = 0.0;
    for (uint i = 0u; i < 6u; ++i) photonCells[slot].shReserved[i] = 0u;
}

uint PhotonCellClaim(ivec3 cell, uint level, uint currentGeneration) {
    uint keyLo = PhotonCellPackKeyLo(cell);
    if (keyLo == 0u) return ~0u;
    uint keyHiBase = PhotonCellPackKeyHiBase(level);
    uint slot = PhotonCellHashSlot(keyLo, keyHiBase);
    uint stride = PhotonCellProbeStride(keyLo);

    for (uint probe = 0u; probe < kPhotonCellProbeLimit; ++probe) {
        uint oldLo = atomicCompSwap(photonCells[slot].keyLo, 0u, keyLo);
        if (oldLo == 0u) {
            // Publish transient before clearing/publishing the payload.  A
            // level-0 keyHiBase of zero must not be mistaken for an init race.
            atomicExchange(photonCells[slot].keyHi,
                           keyHiBase | kPhotonCellTransientGeneration);
            PhotonCellZeroPayload(slot);
            atomicExchange(photonCells[slot].keyHi,
                           keyHiBase | currentGeneration);
            return slot;
        }

        uint storedHi = atomicAdd(photonCells[slot].keyHi, 0u);
        if (storedHi == 0u) {
            // Another lane won keyLo but has not published its transient marker.
            continue;
        }
        uint storedBase = storedHi & ~kPhotonCellGenerationMask;
        uint storedGeneration = storedHi & kPhotonCellGenerationMask;
        if (storedBase == keyHiBase && oldLo == keyLo &&
            storedGeneration != kPhotonCellTransientGeneration) {
            // A matching key is a cache hit even when its generation is old:
            // the widened age policy applies only when a different key needs
            // to reclaim this slot, preserving the EWMA history on a hit.
            atomicMax(photonCells[slot].keyHi, keyHiBase | currentGeneration);
            return slot;
        }

        // Different keys, including a same-coordinate/different-level alias,
        // may reclaim only a stale slot.  This is the D4 lazy-reclaim rule that
        // prevents the previous salvage path from probing forever behind old
        // generations.
        if (storedGeneration == kPhotonCellTransientGeneration) {
            for (uint spin = 0u; spin < 16u; ++spin) {
                storedHi = atomicAdd(photonCells[slot].keyHi, 0u);
                if ((storedHi & kPhotonCellGenerationMask) != kPhotonCellTransientGeneration) break;
            }
            storedGeneration = storedHi & kPhotonCellGenerationMask;
        }
        if (storedGeneration != kPhotonCellTransientGeneration &&
            PhotonCellIsStale(currentGeneration, storedGeneration)) {
            uint marker = keyHiBase | kPhotonCellTransientGeneration;
            if (atomicCompSwap(photonCells[slot].keyHi, storedHi, marker) == storedHi) {
                PhotonCellZeroPayload(slot);
                atomicExchange(photonCells[slot].keyLo, keyLo);
                atomicExchange(photonCells[slot].keyHi,
                               keyHiBase | currentGeneration);
                return slot;
            }
        }
        slot = (slot + stride) & (kPhotonCellCapacity - 1u);
    }
    return ~0u; // fail-soft on probe exhaustion
}

int PhotonCellQuantize(float flux) {
    float bounded = clamp(flux, 0.0, photonCellParams.misc0.y);
    return int(round(bounded * kPhotonCellFluxScale));
}

vec3 PhotonCellClampedDiffuseFlux(HitRecord rec) {
    vec3 flux = rec.albedo * uintBitsToFloat(rec._pad0[1]);
    uint lightCount = min(lightingConfig.lightCount, 4u);
    for (uint i = 0u; i < lightCount; ++i) {
        Light light = lightingConfig.lights[i];
        vec3 lightDir = normalize(light.direction_or_position);
        float ndotl = max(dot(rec.worldNormal, lightDir), 0.0);
        float visibility = 1.0;
        if (shadowConfig.enabled != 0u) {
            visibility = ((rec._pad0[2] >> i) & 1u) != 0u ? 1.0 : 0.0;
        }
        flux += visibility * ndotl * (rec.albedo / kPhotonCellPi) * light.radiance;
    }
    return clamp(flux, vec3(0.0), vec3(photonCellParams.misc0.y));
}

#endif
