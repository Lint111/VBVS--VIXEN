#pragma once
// ============================================================================
// SdfBake.h — Bake an SdfRecipes recipe into a GaiaVoxelWorld of narrow-band
// SDF voxels (Density = signed distance), then build a LaineKarrasOctree over
// those voxels. Inc2 M1 CPU path.
//
// OWNERSHIP: same pattern as ShellOctree.h — the bundle owns world + registry;
// octree is declared LAST so it destructs FIRST (before the pointers it holds).
// ============================================================================
#include "SdfRecipes.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include "AttributeRegistry.h"
#include "LaineKarrasOctree.h"

#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <cmath>

namespace Vixen::SVO {

// ---------------------------------------------------------------------------
// SdfBakeResult — world + registry of narrow-band SDF voxels (no octree yet).
// sampleStored queries the world at an integer grid position.
// ---------------------------------------------------------------------------
struct SdfBakeResult {
    std::unique_ptr<Vixen::GaiaVoxel::GaiaVoxelWorld> world;
    std::unique_ptr<AttributeRegistry> registry;
    int n = 0;        // Grid side length (cells in [0,n))
    glm::vec3 center{0.0f};

    // Returns the baked Density (signed distance) stored at the nearest integer
    // grid cell, or nullopt if that cell is outside the narrow band (unallocated).
    std::optional<float> sampleStored(const glm::vec3& gridPos) const;
};

// ---------------------------------------------------------------------------
// BakeRecipeToSdfWorld
//
// Evaluates the recipe over the integer grid [0,n)^3.  For each cell whose
// |evalSdf| <= bandVoxels a voxel entity is created carrying:
//   Density{signedDistance}  (the baked SDF value — NOT a constant 1.0)
//   Color{1,1,1}
//   Material{1}
//
// The AttributeRegistry keys mirror ShellOctree.h ("density" Float key + "color"
// Vec3 attribute), so the same SVORebuild / ShellOctreeGpu pipeline can consume
// the result unchanged.
// ---------------------------------------------------------------------------
inline SdfBakeResult BakeRecipeToSdfWorld(uint32_t recipeId, const glm::vec3& center,
                                          const RecipeParams& rp, int n, float bandVoxels) {
    SdfBakeResult r;
    r.n      = n;
    r.center = center;

    // Registry (mirrors ShellOctree.h: key="density" Float + "color" Vec3 attr)
    r.registry = std::make_unique<AttributeRegistry>();
    r.registry->registerKey("density", Vixen::VoxelData::AttributeType::Float, 0.0f);
    r.registry->addAttribute("color", Vixen::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));

    // World
    r.world = std::make_unique<Vixen::GaiaVoxel::GaiaVoxelWorld>();

    // Evaluate recipe over [0,n)^3 — emit only narrow-band cells
    for (int z = 0; z < n; ++z)
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const glm::vec3 p(static_cast<float>(x),
                              static_cast<float>(y),
                              static_cast<float>(z));
            const float sd = evalSdf(recipeId, p, center, rp);
            if (std::abs(sd) <= bandVoxels) {
                const Vixen::GaiaVoxel::ComponentQueryRequest comps[] = {
                    Vixen::GaiaVoxel::Density{sd},
                    Vixen::GaiaVoxel::Color{glm::vec3(1.0f)},
                    Vixen::GaiaVoxel::Material{1u},
                };
                r.world->createVoxel(
                    Vixen::GaiaVoxel::VoxelCreationRequest{p, comps});
            }
        }

    return r;
}

// sampleStored implementation (out-of-line in the header — inline body)
inline std::optional<float> SdfBakeResult::sampleStored(const glm::vec3& gridPos) const {
    // getEntityByWorldSpace returns a bare EntityID (gaia::ecs::Entity); check
    // existence with world->exists() before querying components.
    const Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID e =
        world->getEntityByWorldSpace(gridPos);
    if (!world->exists(e)) return std::nullopt;
    auto d = world->getComponentValue<Vixen::GaiaVoxel::Density>(e);
    return d.has_value() ? std::optional<float>(d.value()) : std::nullopt;
}

// ---------------------------------------------------------------------------
// SdfBodyOctree — owning bundle returned by BuildSdfBodyOctree.
// Declaration order matters: octree last => destructs first.
// ---------------------------------------------------------------------------
struct SdfBodyOctree {
    std::unique_ptr<Vixen::GaiaVoxel::GaiaVoxelWorld> world;
    std::unique_ptr<AttributeRegistry> registry;
    std::unique_ptr<LaineKarrasOctree> octree;
};

// ---------------------------------------------------------------------------
// BuildSdfBodyOctree
//
// Builds a LaineKarrasOctree over the baked SDF world, using the same
// parameter choices as ShellOctree.h::BuildShellOctree:
//   - worldMin = (0,0,0), worldMax = (n,n,n) where n = baked.n
//   - maxLevels = log2(n) + brickDepth  (exact, as derived in ShellOctree.h)
//   - setBodyOctree(true) so non-LOD castRay uses the GPU-parity traversal
//
// The baked world and registry are MOVED into the returned bundle (the
// SdfBakeResult is left with null pointers after the call).
// ---------------------------------------------------------------------------
inline SdfBodyOctree BuildSdfBodyOctree(SdfBakeResult& baked, int brickDepth /*= 3*/) {
    SdfBodyOctree result;

    // Transfer ownership from the bake result
    result.world    = std::move(baked.world);
    result.registry = std::move(baked.registry);

    const int n          = baked.n;
    const glm::vec3 worldMin(0.0f);
    const glm::vec3 worldMax(static_cast<float>(n));

    // maxLevels: 2^(maxLevels - brickDepth) must == n  =>  maxLevels = log2(n) + brickDepth
    // n is set by the caller as a power of two (baking uses [0,n)^3 grid).
    int log2n = 0;
    {
        int tmp = n;
        while (tmp > 1) { tmp >>= 1; ++log2n; }
    }
    const int maxLevels = log2n + brickDepth;

    result.octree = std::make_unique<LaineKarrasOctree>(
        *result.world, result.registry.get(), maxLevels, brickDepth);
    result.octree->rebuild(*result.world, worldMin, worldMax);
    result.octree->setBodyOctree(true);

    return result;
}

}  // namespace Vixen::SVO
