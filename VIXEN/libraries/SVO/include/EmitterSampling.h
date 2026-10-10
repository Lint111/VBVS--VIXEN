#pragma once
// EmitterSampling.h - CPU mirror of shaders/EmitterSampling.glsl (R502 / R502.2, S0).
//
// SYNC CONTRACT: line-by-line port of the GLSL (draw order, float ops, key layout). The
// scene object supplies the same hooks the including shader would; EmitterSceneCpu below
// backs them with an EmitterTable + a ConcatenatedOctrees. test_emitter_sampling.cpp is the
// red-green gate (seeded replay + closed-form identities + sampled-density checks).

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "EmitterPower.h"
#include "RecipeEmitterTable.h"
#include "ShellOctreeGpu.h"

namespace Vixen::SVO {

struct EmitterSampleCpu {
    glm::vec3 position{0.0f};
    uint32_t key = 0;
    glm::vec3 radiance{0.0f};
    float pdf = 0.0f;
};
static_assert(sizeof(EmitterSampleCpu) == 32, "EmitterSample std430 size");

inline constexpr uint32_t kEmitterHeaderWords = kEmitterPowerHeaderWords;
inline constexpr uint32_t kEmitterMaxDepth = 24u;

// Mirrors ReservoirCombine.glsl pcgHash / rngNextFloat.
inline uint32_t EmitterPcgHash(uint32_t v) {
    const uint32_t state = v * 747796405u + 2891336453u;
    const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// rngNextFloat can round up to exactly 1.0; the GLSL caps it (emitterNext).
inline float EmitterNext(uint32_t& rng) {
    rng = EmitterPcgHash(rng);
    const float u = static_cast<float>(rng) * (1.0f / 4294967296.0f);
    return std::min(u, 0.99999994f);
}

inline glm::vec3 EmitterOctantOffset(uint32_t octant) {
    return glm::vec3(static_cast<float>(octant & 1u), static_cast<float>((octant >> 1u) & 1u),
                     static_cast<float>((octant >> 2u) & 1u));
}

// Scene concept (all hooks const):
//   EmitterTotals Totals(); RecipeEmitterEntry Recipe(i); uint32_t SurfaceCell(i);
//   SparseEmitterEntry Sparse(i); uint32_t LaneWord(oct, w); glm::uvec2 Node(oct, node);
//   float RecipeSd(inst, pLocal); glm::vec3 RecipeRadiance(inst);
//   glm::vec3 ToWorldPoint(inst, pLocal); glm::vec3 RecipeNormal(inst, worldPos);
namespace emitter_detail {

template <class S>
float LaneFloat(const S& scene, uint32_t oct, uint32_t w) {
    return std::bit_cast<float>(scene.LaneWord(oct, w));
}

template <class S>
uint32_t PickRecipe(const S& scene, float target, uint32_t count) {
    uint32_t lo = 0u, hi = count;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) >> 1u;
        if (scene.Recipe(mid).cdf <= target) lo = mid + 1u; else hi = mid;
    }
    return std::min(lo, count - 1u);
}

template <class S>
uint32_t PickSparse(const S& scene, float target, uint32_t count) {
    uint32_t lo = 0u, hi = count;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) >> 1u;
        if (scene.Sparse(mid).cdf <= target) lo = mid + 1u; else hi = mid;
    }
    return std::min(lo, count - 1u);
}

template <class S>
glm::vec3 RecipeGradient(const S& scene, uint32_t inst, const glm::vec3& p, float h) {
    const float dx = scene.RecipeSd(inst, p + glm::vec3(h, 0, 0)) - scene.RecipeSd(inst, p - glm::vec3(h, 0, 0));
    const float dy = scene.RecipeSd(inst, p + glm::vec3(0, h, 0)) - scene.RecipeSd(inst, p - glm::vec3(0, h, 0));
    const float dz = scene.RecipeSd(inst, p + glm::vec3(0, 0, h)) - scene.RecipeSd(inst, p - glm::vec3(0, 0, h));
    const glm::vec3 g(dx, dy, dz);
    const float len = glm::length(g);
    return len > 1e-12f ? g / len : glm::vec3(0.0f, 1.0f, 0.0f);
}

template <class S>
EmitterSampleCpu SampleRecipe(const S& scene, uint32_t& rng, float pPipeline) {
    const EmitterTotals t = scene.Totals();
    const float u1 = EmitterNext(rng);
    const float u2 = EmitterNext(rng);
    const float u3 = EmitterNext(rng);
    const float u4 = EmitterNext(rng);
    const float u5 = EmitterNext(rng);

    const uint32_t k = PickRecipe(scene, u1 * t.phiRecipe, t.recipeEntryCount);
    const RecipeEmitterEntry e = scene.Recipe(k);

    glm::vec3 pLocal;
    uint32_t origin = 0u;
    const glm::vec3 aabbMin(e.gridAabbMin[0], e.gridAabbMin[1], e.gridAabbMin[2]);
    if (e.cellCount > 0u) {
        const uint32_t pick = std::min(static_cast<uint32_t>(u2 * static_cast<float>(e.cellCount)), e.cellCount - 1u);
        const uint32_t ci = scene.SurfaceCell(e.cellOffset + pick);
        const uint32_t dim = e.gridDim;
        const uint32_t cx = ci % dim, cy = (ci / dim) % dim, cz = ci / (dim * dim);
        pLocal = aabbMin + (glm::vec3(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(cz)) +
                            glm::vec3(u3, u4, u5)) * e.cellSize;
        const float h = 0.25f * e.cellSize;
        for (uint32_t i = 0; i < 3u; ++i) {
            const float sd = scene.RecipeSd(e.instIdx, pLocal);
            pLocal -= sd * RecipeGradient(scene, e.instIdx, pLocal, h);
        }
        origin = ci;
    } else {
        const float z = 1.0f - 2.0f * u2;
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float ph = 2.0f * kEmitterPi * u3;
        pLocal = aabbMin + e.cellSize * glm::vec3(r * std::cos(ph), r * std::sin(ph), z);
    }

    EmitterSampleCpu s;
    s.position = scene.ToWorldPoint(e.instIdx, pLocal);
    s.radiance = scene.RecipeRadiance(e.instIdx);
    s.pdf = pPipeline * (e.phi / t.phiRecipe) / e.areaInst;
    s.key = 1u | ((e.instIdx & 0xFFu) << 2u) | ((origin & 0x3FFFFFu) << 10u);
    return s;
}

template <class S>
EmitterSampleCpu SampleSparse(const S& scene, uint32_t& rng, float pPipeline) {
    const EmitterTotals t = scene.Totals();
    const float u1 = EmitterNext(rng);
    const uint32_t k = PickSparse(scene, u1 * t.phiSparse, t.sparseEntryCount);
    const SparseEmitterEntry e = scene.Sparse(k);
    const uint32_t oi = e.octreeIdx;

    const float rootPhi = LaneFloat(scene, oi, 1u);
    const uint32_t leafCount = scene.LaneWord(oi, 0u);
    const uint32_t nodeCount = scene.LaneWord(oi, 2u);
    if (!(rootPhi > 0.0f) || leafCount == 0u) return {};

    uint32_t node = 0u;
    glm::vec3 cellMin(0.0f);
    float size = 1.0f;
    bool atLeaf = false;
    for (uint32_t depth = 0u; depth < kEmitterMaxDepth && !atLeaf; ++depth) {
        const glm::uvec2 d = scene.Node(oi, node);
        const uint32_t valid = (d.x >> 16) & 0xFFu;
        const uint32_t leafMask = (d.x >> 24) & 0xFFu;
        const uint32_t childPointer = d.x & 0x7FFFu;
        const uint32_t nonLeaf = valid & ~leafMask;
        const uint32_t totalNonLeaf = static_cast<uint32_t>(std::popcount(nonLeaf));
        const float target = EmitterNext(rng) * LaneFloat(scene, oi, kEmitterHeaderWords + node);

        float acc = 0.0f;
        bool have = false;
        uint32_t chosenOct = 0u, chosenIdx = 0u;
        bool chosenLeaf = false;
        for (uint32_t oct = 0u; oct < 8u; ++oct) {
            if (((valid >> oct) & 1u) == 0u) continue;
            const uint32_t below = (1u << oct) - 1u;
            const bool isLeaf = ((leafMask >> oct) & 1u) != 0u;
            const uint32_t cidx = isLeaf
                ? childPointer + totalNonLeaf + static_cast<uint32_t>(std::popcount(valid & leafMask & below))
                : childPointer + static_cast<uint32_t>(std::popcount(nonLeaf & below));
            const float phi = LaneFloat(scene, oi, kEmitterHeaderWords + cidx);
            if (!(phi > 0.0f)) continue;
            acc += phi;
            chosenOct = oct;
            chosenIdx = cidx;
            chosenLeaf = isLeaf;
            have = true;
            if (target < acc) break;
        }
        if (!have) return {};
        cellMin += EmitterOctantOffset(chosenOct) * (size * 0.5f);
        size *= 0.5f;
        node = chosenIdx;
        atLeaf = chosenLeaf;
    }
    if (!atLeaf) return {};

    uint32_t lo = 0u, hi = leafCount;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) >> 1u;
        if (scene.LaneWord(oi, kEmitterHeaderWords + nodeCount + mid) < node) lo = mid + 1u; else hi = mid;
    }
    if (lo >= leafCount || scene.LaneWord(oi, kEmitterHeaderWords + nodeCount + lo) != node) return {};
    const uint32_t octBase = kEmitterHeaderWords + nodeCount + leafCount + lo * 8u;

    float octTotal = 0.0f;
    for (uint32_t o = 0u; o < 8u; ++o) octTotal += LaneFloat(scene, oi, octBase + o);
    const float octTarget = EmitterNext(rng) * octTotal;
    float acc2 = 0.0f;
    bool haveOct = false;
    uint32_t octant = 0u;
    float octPhi = 0.0f;
    for (uint32_t o = 0u; o < 8u; ++o) {
        const float phi = LaneFloat(scene, oi, octBase + o);
        if (!(phi > 0.0f)) continue;
        acc2 += phi;
        octant = o;
        octPhi = phi;
        haveOct = true;
        if (octTarget < acc2) break;
    }
    if (!haveOct) return {};

    const float u3 = EmitterNext(rng);
    const float u4 = EmitterNext(rng);
    const float u5 = EmitterNext(rng);
    const float side = size * 0.5f;
    const glm::vec3 pNorm = cellMin + EmitterOctantOffset(octant) * side + glm::vec3(u3, u4, u5) * side;

    const float areaNorm = 3.0f * side * side;
    const float areaWorld = areaNorm * e.areaScale;

    EmitterSampleCpu s;
    s.position = scene.ToWorldPoint(e.instIdx, pNorm * kEmitterWorldGridSize);
    s.radiance = glm::vec3(octPhi / areaNorm);
    s.pdf = pPipeline * (e.phi / t.phiSparse) * (octPhi / rootPhi) / areaWorld;
    s.key = 2u | ((e.instIdx & 0xFFu) << 2u) | ((((node & 0x7FFFFu) << 3u) | octant) << 10u);
    return s;
}

}  // namespace emitter_detail

template <class S>
float EmitterTotalPower(const S& scene) {
    const EmitterTotals t = scene.Totals();
    return t.phiRecipe + t.phiSparse;
}

template <class S>
EmitterSampleCpu SampleEmitter(const S& scene, uint32_t& rng) {
    const EmitterTotals t = scene.Totals();
    const float total = t.phiRecipe + t.phiSparse;
    if (!(total > 0.0f)) return {};
    const float u0 = EmitterNext(rng);
    if (u0 * total < t.phiRecipe) return emitter_detail::SampleRecipe(scene, rng, t.phiRecipe / total);
    return emitter_detail::SampleSparse(scene, rng, t.phiSparse / total);
}

template <class S>
float EmitterPHat(const S& scene, const EmitterSampleCpu& s, const glm::vec3& x, const glm::vec3& n) {
    if (!(s.pdf > 0.0f)) return 0.0f;
    const glm::vec3 d = s.position - x;
    const float dist2 = std::max(glm::dot(d, d), 1e-6f);
    const glm::vec3 w = d / std::sqrt(dist2);
    const float cosR = std::max(glm::dot(n, w), 0.0f);
    float cosE = 1.0f;
    if ((s.key & 3u) == 1u) cosE = std::max(glm::dot(scene.RecipeNormal((s.key >> 2u) & 0xFFu, s.position), -w), 0.0f);
    return EmitterLuminance(s.radiance) * cosR * cosE / dist2;
}

// Scene backed by an EmitterTable + shapes + a concatenated octree stream. Recipe geometry
// comes from caller-supplied callbacks (recipeSd is recipe-local; normal is world-space).
struct EmitterSceneCpu {
    const EmitterTable* table = nullptr;
    const RecipeEmitterShapes* shapes = nullptr;
    const ConcatenatedOctrees* cat = nullptr;
    std::span<const BodyInstanceGpu> instances;
    std::function<float(uint32_t instIdx, const glm::vec3& pLocal)> recipeSd;

    EmitterTotals Totals() const { return table->totals; }
    RecipeEmitterEntry Recipe(uint32_t i) const { return table->recipe[i]; }
    uint32_t SurfaceCell(uint32_t i) const { return shapes->surfaceCells[i]; }
    SparseEmitterEntry Sparse(uint32_t i) const { return table->sparse[i]; }
    uint32_t LaneWord(uint32_t oct, uint32_t w) const {
        const EmitterPowerLaneView v = ViewEmitterPowerLaneOf(*cat, oct);
        return v.words[w];
    }
    glm::uvec2 Node(uint32_t oct, uint32_t node) const {
        const size_t index = static_cast<size_t>(cat->configs[oct].nodeArrayBase) + node;
        const uint32_t* w = reinterpret_cast<const uint32_t*>(cat->nodes.data()) + index * 2u;
        return glm::uvec2(w[0], w[1]);
    }
    float RecipeSd(uint32_t inst, const glm::vec3& p) const { return recipeSd(inst, p); }
    glm::vec3 RecipeRadiance(uint32_t inst) const {
        const BodyInstanceMaterialGpu& m = instances[inst].material;
        return glm::vec3(m.color[0], m.color[1], m.color[2]) * m.recipeParams[3];
    }
    glm::vec3 ToWorldPoint(uint32_t inst, const glm::vec3& p) const {
        return TransformPoint(instances[inst].transform.localToWorld, p);
    }
    glm::vec3 RecipeNormal(uint32_t inst, const glm::vec3& worldPos) const {
        const BodyInstanceTransformGpu& tr = instances[inst].transform;
        const glm::vec3 local = TransformPoint(tr.worldToLocal, worldPos);
        const BodyInstanceMaterialGpu& m = instances[inst].material;
        const RecipeEmitterShape* shape = shapes->Find(m.recipeId);
        const float h = shape && shape->cellSize > 0.0f ? 0.25f * shape->cellSize : 1e-3f;
        return TransformNormalToWorld(tr, emitter_detail::RecipeGradient(*this, inst, local, h));
    }
};

}  // namespace Vixen::SVO
