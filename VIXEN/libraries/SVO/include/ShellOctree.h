#pragma once

// ShellOctree — build a sparse SURFACE-SHELL ESVO for a body via the entity/voxel
// path (no dense grid). SP1 Task 2.
//
// BuildShellOctree(depth, materialId):
//   1. take the hollow unit-sphere surface cells from ShellVoxels(depth),
//   2. mint one voxel entity per cell (carrying the Material component),
//   3. build + rebuild a LaineKarrasOctree over those entities.
//
// OWNERSHIP / LIFETIME (verified against the headers):
//   LaineKarrasOctree stores a NON-OWNING `GaiaVoxelWorld* m_voxelWorld` and
//   `AttributeRegistry* m_registry` (LaineKarrasOctree.h, lines 296 & 303), and
//   EntityBrickView queries the world LAZILY at traversal time
//   (EntityBrickView.h — WorldSpace/IntegerGrid/LocalGrid modes look entities up
//   on demand via Morton against `m_world`). So castRay() reads the world AFTER
//   rebuild(). A bare octree over a function-local world would therefore dangle.
//   => the returned ShellOctree OWNS the world and the registry; the octree is
//      declared LAST so it destructs FIRST, before the state it points into.

#include "ShellVoxelizer.h"
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"   // Material, Density, Color components
#include "AttributeRegistry.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Vixen::SVO {

/**
 * Owning, movable bundle: a surface-shell ESVO plus the world + registry it
 * references. Move-only (unique_ptr members). SP2 caches one of these per kind.
 *
 * Declaration order matters for destruction: `octree` last => destroyed first,
 * before the `world`/`registry` its non-owning pointers reference.
 */
struct ShellOctree {
    std::unique_ptr<Vixen::GaiaVoxel::GaiaVoxelWorld> world;
    std::unique_ptr<AttributeRegistry> registry;
    std::unique_ptr<LaineKarrasOctree> octree;
};

/**
 * Build a hollow surface-shell ESVO for a unit-sphere body.
 *
 * @param depth      Shell lattice depth: n = 2^depth cells/axis (cells in [0,n)).
 * @param materialId Material component value attached to every shell voxel.
 * @return Owning ShellOctree (world + registry + octree). castRay via
 *         `result.octree`.
 *
 * Coordinate convention: shell cells are placed at INTEGER grid positions
 * glm::vec3(cell) — Morton encoding floors onto the integer grid, so cast hit
 * points read back in the same integer world coords (matches the ray-casting
 * test suite). Octree bounds are exactly [0, n]³ (see below).
 */
inline ShellOctree BuildShellOctree(int depth, uint32_t materialId) {
    ShellOctree result;

    // --- Registry (mirrors the canonical test fixture: density key + color attr).
    // Must exist before createVoxel/rebuild.
    result.registry = std::make_unique<AttributeRegistry>();
    result.registry->registerKey("density", Vixen::VoxelData::AttributeType::Float, 1.0f);
    result.registry->addAttribute("color", Vixen::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));

    // --- World.
    result.world = std::make_unique<Vixen::GaiaVoxel::GaiaVoxelWorld>();

    // --- Mint one voxel entity per surface-shell cell, carrying the material.
    // Cells are in the integer lattice [0, n); place each voxel at its integer
    // grid position (Morton encoding floors onto the integer grid, so cast hit
    // points read back in these same coords).
    const std::vector<glm::ivec3> cells = ShellVoxels(depth);
    for (const glm::ivec3& cell : cells) {
        const glm::vec3 pos(cell);
        const Vixen::GaiaVoxel::ComponentQueryRequest components[] = {
            Vixen::GaiaVoxel::Density{1.0f},
            Vixen::GaiaVoxel::Color{glm::vec3(1.0f, 1.0f, 1.0f)},
            Vixen::GaiaVoxel::Material{materialId},
        };
        result.world->createVoxel(Vixen::GaiaVoxel::VoxelCreationRequest{pos, components});
    }

    // --- Octree bounds [0, n]³. rebuild() assumes the voxel grid spans
    // [0, worldSize] (SVORebuild.cpp: voxelsPerAxis = worldSize.x; bricks binned
    // by raw position). A power-of-2 n keeps brick alignment exact (bricksPerAxis
    // = n / 2^brickDepth) — an asymmetric/padded min shears the binning and drops
    // the far-boundary shell. Verified: clean [0,n] bounds make both shells hit.
    const int n = 1 << depth;
    const glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    const glm::vec3 worldMax(static_cast<float>(n), static_cast<float>(n), static_cast<float>(n));

    // brickDepthLevels=3 (8³ bricks, as in the examples). The octree must resolve
    // an n=2^depth lattice: 2^(maxLevels - brickDepthLevels) == n  =>
    // maxLevels = depth + brickDepthLevels (exact; verified to traverse cleanly).
    constexpr int kBrickDepthLevels = 3;
    const int maxLevels = depth + kBrickDepthLevels;

    result.octree = std::make_unique<LaineKarrasOctree>(
        *result.world, result.registry.get(), maxLevels, kBrickDepthLevels);
    result.octree->rebuild(*result.world, worldMin, worldMax);

    // A shell IS a body octree (integer-grid voxels at worldMin=0, GPU-serializable brick
    // layout): route its non-LOD castRay through the GPU-parity traversal so body
    // collision/queries match the renderer exactly (LaineKarrasOctree::setBodyOctree).
    result.octree->setBodyOctree(true);

    return result;
}

}  // namespace Vixen::SVO
