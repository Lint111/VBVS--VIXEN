#pragma once
// RecipeEmitterTable.h - R502.2 emitter data (count-agnostic lighting, S0).
//
// CPU builder for the two instance-level emitter CDFs the sampler reads
// (lightdesign §2.2 recipe emitters, §2.3 step 1 sparse-octree selection) and the
// EmitterTotals aggregate (§2.4). Nothing renders from it yet.
//
// Record layouts mirror the generated Vixen::Gpu records (codegen/config-schemas/
// RecipeEmitterEntry.cs, SparseEmitterEntry.cs, EmitterTotals.cs); SVO does not depend on
// RenderGraph, so the parity static_asserts live in BodyOctreeSceneNode.cpp.
//
// Power model: Phi = pi * Y(radiance) * A  (diffuse emitter), Y = Rec.709 luminance.
//   recipe instance: radiance = color * recipeParams[3] (R502.2: Q1 option a);
//                    A_inst = A_recipe * s^2, A_recipe = surfaceCells * cellSize^2 (or the
//                    bound-sphere 4*pi*r^2 fallback when the recipe has no occupancy grid).
//   stored instance: Phi_inst = pi * Phi_root(octree lane) * (kWorldGridSize * s)^2,
//                    Phi_root already integrates emission * area in octree-normalized units.
// s is the instance's uniform scale, cbrt(|det| of the 3x3 of localToWorld).

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "Recipe/RecipeRegistry.h"
#include "ShellOctreeGpu.h"

namespace Vixen::SVO {

inline constexpr float kEmitterPi = 3.14159265358979323846f;
inline constexpr float kEmitterWorldGridSize = 10.0f;  // octree [0,1]^3 -> instance-local span
inline constexpr uint32_t kRecipeEmitterFlagBoundSphereArea = 1u;

// One scalar for "how much light": Rec.709 luminance. Shared by the table, the sampler and
// the GLSL twin (emitterLuminance) so the choice is a one-line change.
inline float EmitterLuminance(const glm::vec3& rgb) {
    return 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z;
}

struct RecipeEmitterEntry {
    uint32_t instIdx = 0;
    float phi = 0.0f;
    float cdf = 0.0f;
    uint32_t flags = 0;
    uint32_t cellOffset = 0;
    uint32_t cellCount = 0;
    uint32_t gridDim = 0;
    float cellSize = 0.0f;
    float gridAabbMin[3] = {0.0f, 0.0f, 0.0f};
    float areaInst = 0.0f;
};
static_assert(sizeof(RecipeEmitterEntry) == 48, "RecipeEmitterEntry std430 size");

struct SparseEmitterEntry {
    uint32_t instIdx = 0;
    uint32_t octreeIdx = 0;
    float phi = 0.0f;
    float cdf = 0.0f;
    float areaScale = 0.0f;
    uint32_t _pad0 = 0, _pad1 = 0, _pad2 = 0;
};
static_assert(sizeof(SparseEmitterEntry) == 32, "SparseEmitterEntry std430 size");

struct EmitterTotals {
    float phiRecipe = 0.0f;
    float phiSparse = 0.0f;
    uint32_t recipeEntryCount = 0;
    uint32_t sparseEntryCount = 0;
};
static_assert(sizeof(EmitterTotals) == 16, "EmitterTotals std430 size");

// Per-recipe geometry the table needs, built once per recipe registration.
struct RecipeEmitterShape {
    uint32_t recipeId = 0;
    uint32_t cellOffset = 0;  // into RecipeEmitterShapes::surfaceCells
    uint32_t cellCount = 0;   // 0 => bound-sphere fallback
    uint32_t gridDim = 0;
    float cellSize = 0.0f;
    glm::vec3 aabbMin{0.0f};
    float areaRecipe = 0.0f;  // recipe-local area
    bool boundSphereFallback = false;
};

struct RecipeEmitterShapes {
    std::vector<RecipeEmitterShape> shapes;      // ascending recipeId
    std::vector<uint32_t> surfaceCells;          // x + dim * (y + dim * z), per recipe slice

    const RecipeEmitterShape* Find(uint32_t recipeId) const {
        size_t lo = 0, hi = shapes.size();
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (shapes[mid].recipeId < recipeId) lo = mid + 1; else hi = mid;
        }
        return (lo < shapes.size() && shapes[lo].recipeId == recipeId) ? &shapes[lo] : nullptr;
    }
};

// Surface cell: the grid's conservative min|sd| (already margined) is within half the cell
// diagonal of zero. No occupancy grid -> the bound sphere's 4*pi*r^2, flagged.
inline RecipeEmitterShapes BuildRecipeEmitterShapes(const RecipeRegistry& registry) {
    RecipeEmitterShapes out;
    for (const uint32_t id : registry.Ids()) {
        const RecipeRegistry::RecipeEntry* entry = registry.Get(id);
        if (!entry) continue;
        RecipeEmitterShape shape;
        shape.recipeId = id;
        const size_t dim = entry->occupancyGridDim;
        if (dim > 0 && entry->occupancyGridValues.size() == dim * dim * dim &&
            entry->occupancyGridCellSize > 0.0f) {
            shape.gridDim = static_cast<uint32_t>(dim);
            shape.cellSize = entry->occupancyGridCellSize;
            shape.aabbMin = entry->occupancyGridAabbMin;
            shape.cellOffset = static_cast<uint32_t>(out.surfaceCells.size());
            const float threshold = 0.5f * 1.7320508f * entry->occupancyGridCellSize;
            for (size_t i = 0; i < entry->occupancyGridValues.size(); ++i) {
                if (entry->occupancyGridValues[i] <= threshold)
                    out.surfaceCells.push_back(static_cast<uint32_t>(i));
            }
            shape.cellCount = static_cast<uint32_t>(out.surfaceCells.size()) - shape.cellOffset;
            shape.areaRecipe = static_cast<float>(shape.cellCount) * shape.cellSize * shape.cellSize;
        }
        if (shape.cellCount == 0u) {
            shape.boundSphereFallback = true;
            shape.areaRecipe = 4.0f * kEmitterPi * entry->boundRadius * entry->boundRadius;
            shape.aabbMin = entry->boundCenter;   // fallback reuses the grid fields:
            shape.cellSize = entry->boundRadius;  // centre / radius of the bound sphere
        }
        out.shapes.push_back(shape);
    }
    return out;
}

// Uniform scale of an instance transform: cbrt(|det3|).
inline float EmitterInstanceScale(const Affine3x4Gpu& m) {
    const double det =
        static_cast<double>(m.rows[0][0]) * (static_cast<double>(m.rows[1][1]) * m.rows[2][2] - static_cast<double>(m.rows[1][2]) * m.rows[2][1]) -
        static_cast<double>(m.rows[0][1]) * (static_cast<double>(m.rows[1][0]) * m.rows[2][2] - static_cast<double>(m.rows[1][2]) * m.rows[2][0]) +
        static_cast<double>(m.rows[0][2]) * (static_cast<double>(m.rows[1][0]) * m.rows[2][1] - static_cast<double>(m.rows[1][1]) * m.rows[2][0]);
    return static_cast<float>(std::cbrt(std::fabs(det)));
}

struct EmitterTable {
    std::vector<RecipeEmitterEntry> recipe;
    std::vector<SparseEmitterEntry> sparse;
    EmitterTotals totals;
};

// octreeRootPower[i] is the sparse lane's root power (octree-normalized units) of octree i,
// 0 for octrees without a lane. Entries are in instance order; cdf is inclusive.
inline EmitterTable BuildEmitterTable(std::span<const BodyInstanceGpu> instances,
                                      const RecipeEmitterShapes& shapes,
                                      std::span<const float> octreeRootPower) {
    EmitterTable table;
    double recipeSum = 0.0, sparseSum = 0.0;
    for (uint32_t i = 0; i < instances.size(); ++i) {
        const BodyInstanceMaterialGpu& m = instances[i].material;
        const float s = EmitterInstanceScale(instances[i].transform.localToWorld);
        if (!(s > 0.0f)) continue;
        if (m.providerKind == 1u) {
            const float emission = m.recipeParams[3];
            const float lum = EmitterLuminance(glm::vec3(m.color[0], m.color[1], m.color[2])) * emission;
            if (!(emission > 0.0f) || !(lum > 0.0f)) continue;
            const RecipeEmitterShape* shape = shapes.Find(m.recipeId);
            if (!shape || !(shape->areaRecipe > 0.0f)) continue;
            RecipeEmitterEntry e;
            e.instIdx = i;
            e.areaInst = shape->areaRecipe * s * s;
            e.phi = kEmitterPi * lum * e.areaInst;
            recipeSum += e.phi;
            e.cdf = static_cast<float>(recipeSum);
            e.flags = shape->boundSphereFallback ? kRecipeEmitterFlagBoundSphereArea : 0u;
            e.cellOffset = shape->cellOffset;
            e.cellCount = shape->cellCount;
            e.gridDim = shape->gridDim;
            e.cellSize = shape->cellSize;
            e.gridAabbMin[0] = shape->aabbMin.x;
            e.gridAabbMin[1] = shape->aabbMin.y;
            e.gridAabbMin[2] = shape->aabbMin.z;
            table.recipe.push_back(e);
        } else if (m.providerKind == 0u && m.octreeIndex < octreeRootPower.size()) {
            const float root = octreeRootPower[m.octreeIndex];
            if (!(root > 0.0f)) continue;
            SparseEmitterEntry e;
            e.instIdx = i;
            e.octreeIdx = m.octreeIndex;
            const float worldScale = kEmitterWorldGridSize * s;
            e.areaScale = worldScale * worldScale;
            e.phi = kEmitterPi * root * e.areaScale;
            sparseSum += e.phi;
            e.cdf = static_cast<float>(sparseSum);
            table.sparse.push_back(e);
        }
    }
    table.totals.phiRecipe = static_cast<float>(recipeSum);
    table.totals.phiSparse = static_cast<float>(sparseSum);
    table.totals.recipeEntryCount = static_cast<uint32_t>(table.recipe.size());
    table.totals.sparseEntryCount = static_cast<uint32_t>(table.sparse.size());
    return table;
}

}  // namespace Vixen::SVO
