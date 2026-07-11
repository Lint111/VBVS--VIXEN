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
#include "Recipe/SdfRecipeEval.h"

#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <cmath>
#include <vector>
#include <functional>

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
// Occupancy-based SDF, sparse at the BRICK level (occupancy fix — supersedes the
// old Inc2 M6 thin-band criterion):
//   1. A brick (brickDepth → 8^3 cells) is OCCUPIED if any of its cells is inside
//      the solid OR within the surface band (evalSdf <= bandVoxels) — i.e. it holds
//      interior AND surface-shell bricks, not just the shell.
//   2. Every cell of an ACTIVE (occupied + 1-brick dilation) brick gets a voxel
//      entity carrying the TRUE signed distance (Density), Color, Roughness, Material.
//   3. Non-occupied exterior bricks (beyond the band) get no voxels → the octree
//      skips them. The old |evalSdf| <= band predicate dropped a body's deep-interior
//      bricks (SDF interior holes); the occupancy predicate fills the whole solid.
//
// Why fully populate active bricks (not just band cells): the GPU trilinear march
// (StoredSdf.glsl) samples the SDF across a whole leaf brick. If non-band cells in
// an allocated brick were left empty, ShellOctreeGpu::SerializeSdf packs them as
// 0.0 — a FALSE iso-surface (sd==0) that the per-brick march hits as brick-shaped
// facets. Storing the real SDF over the entire active brick makes the field a
// correct, hole-free signed distance inside every allocated brick. Buffer sizes are
// unchanged: SerializeSdf always reserves 512 floats per brick; this just fills the
// zeros with real distances.
//
// The AttributeRegistry keys mirror ShellOctree.h ("density" Float key + "color"
// Vec3 attribute), so the same SVORebuild / ShellOctreeGpu pipeline consumes the
// result unchanged.
// ---------------------------------------------------------------------------
// Default emission-eval: every voxel is non-emissive (0.0). Passed as
// BakeSdfWorld's default EmitFn so every existing call site (BakeRecipeToSdfWorld,
// BakeRecipeInstructionsToSdfWorld) stays byte-identical — the M3 gate scene is
// the only caller that supplies a real emission function.
inline float NoEmission(const glm::vec3&) { return 0.0f; }

// ---------------------------------------------------------------------------
// BakeSdfWorld — eval-callable core (P2.1 M1).
//
// Two-pass narrow-band bake. EvalFn: float(glm::vec3 gridPos).
// Color/roughness synthesis is unchanged from the original analytic path.
// EmitFn: float(glm::vec3 gridPos) -> emissive intensity (Sampled Lighting
// Inc3 M3); defaults to NoEmission so pre-M3 callers are unaffected.
// ---------------------------------------------------------------------------
template<class EvalFn, class EmitFn = float(*)(const glm::vec3&)>
inline SdfBakeResult BakeSdfWorld(EvalFn&& eval, const glm::vec3& center,
                                  int n, float bandVoxels, int brickDepth = 3,
                                  EmitFn&& emit = NoEmission) {
    SdfBakeResult r;
    r.n      = n;
    r.center = center;

    // Registry (density Float key + color Vec3 attr + roughness Float attr)
    r.registry = std::make_unique<AttributeRegistry>();
    r.registry->registerKey("density", Vixen::VoxelData::AttributeType::Float, 0.0f);
    r.registry->addAttribute("color", Vixen::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));
    r.registry->addAttribute("roughness", Vixen::VoxelData::AttributeType::Float, 0.5f);
    // Sampled Lighting Inc3 M3: scalar emissive-intensity channel. Default 0.0
    // (non-emissive) — a bake that never sets EmissionIntensity produces an
    // all-zero emissive channel, which is the M3 byte-identity escape hatch
    // (an emissive-absent scene must reproduce the pre-emissive M1/M2 output).
    r.registry->addAttribute("emissionIntensity", Vixen::VoxelData::AttributeType::Float, 0.0f);

    // World
    r.world = std::make_unique<Vixen::GaiaVoxel::GaiaVoxelWorld>();

    const int brickSide     = 1 << brickDepth;                       // 8
    const int bricksPerAxis = (n + brickSide - 1) / brickSide;
    auto brickIndex = [&](int bx, int by, int bz) {
        return (bz * bricksPerAxis + by) * bricksPerAxis + bx;
    };

    // Pass 1 — mark OCCUPIED bricks: a brick is occupied if any of its cells is
    // INSIDE the solid OR within the surface band, i.e. sd <= bandVoxels (negative =
    // inside, confirmed SdfRecipes.h:46). This view-independent occupancy predicate is
    // true for all deep-interior cells (sd very negative) AND the outward band up to
    // +bandVoxels; it is false only for exterior cells beyond the band. Dropping the old
    // std::abs() is the fix: the previous |sd| <= band predicate only marked bricks the
    // thin surface shell passed through, leaving a body's deep-interior bricks (sd far
    // below -band, never touching the shell) UNALLOCATED — the Stored-SDF interior-hole
    // bug. Now a body's full solid interior gets real SDF data.
    const size_t numBricks = static_cast<size_t>(bricksPerAxis) * bricksPerAxis * bricksPerAxis;
    std::vector<uint8_t> occupiedBrick(numBricks, 0u);
    for (int z = 0; z < n; ++z)
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float sd = eval(
                glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
            if (sd <= bandVoxels)
                occupiedBrick[brickIndex(x / brickSide, y / brickSide, z / brickSide)] = 1u;
        }

    // Dilate the occupied set by ONE brick. The GPU trilinear stencil + gradient at an
    // outer-boundary brick's faces reach one voxel into the neighbouring brick; if that
    // neighbour were unallocated, _sampleSdfVoxel returns its 1e9 sentinel and corrupts the
    // sample/normal at the face. Populating a 1-brick margin guarantees every stencil around a
    // real surface brick reads honest data, so the march sphere-traces cleanly with consistent
    // normals (no brick-face shading seams). Under the occupancy predicate the interior is
    // already fully populated, so this dilation now only adds the one-brick OUTWARD skirt beyond
    // the surface — exactly the exterior trilinear-stencil margin it was meant for. Sparsity is
    // preserved (solid body + 1 exterior margin, not the whole grid).
    std::vector<uint8_t> activeBrick(numBricks, 0u);
    // NOTE: do NOT name a variable `near`/`far` — MSVC reserves them (legacy segment
    // qualifiers via windows.h), which GCC does not, so it silently breaks the MSVC build.
    for (int bz = 0; bz < bricksPerAxis; ++bz)
      for (int by = 0; by < bricksPerAxis; ++by)
        for (int bx = 0; bx < bricksPerAxis; ++bx) {
            bool touchesOccupied = false;
            for (int dz = -1; dz <= 1 && !touchesOccupied; ++dz)
              for (int dy = -1; dy <= 1 && !touchesOccupied; ++dy)
                for (int dx = -1; dx <= 1 && !touchesOccupied; ++dx) {
                    const int nx = bx + dx, ny = by + dy, nz = bz + dz;
                    if (nx < 0 || ny < 0 || nz < 0 ||
                        nx >= bricksPerAxis || ny >= bricksPerAxis || nz >= bricksPerAxis) continue;
                    if (occupiedBrick[brickIndex(nx, ny, nz)]) touchesOccupied = true;
                }
            if (touchesOccupied) activeBrick[brickIndex(bx, by, bz)] = 1u;
        }

    // Pass 2 — fully populate every active brick with the TRUE signed distance
    // plus spatially-varying color and roughness (Inc3 M2 multi-channel bake).
    for (int z = 0; z < n; ++z)
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            if (!activeBrick[brickIndex(x / brickSide, y / brickSide, z / brickSide)])
                continue;
            const glm::vec3 p(static_cast<float>(x),
                              static_cast<float>(y),
                              static_cast<float>(z));
            const float sd = eval(p);
            // Smooth RGB bands varying across the grid (visible per-voxel variation)
            const glm::vec3 col = 0.5f + 0.5f * glm::cos(
                glm::vec3(p.x, p.y, p.z) * 0.12f
                + glm::vec3(0.0f, 2.094f, 4.188f));
            // Roughness: striped along Y, clamped to [0,1]
            const float rough = glm::clamp(
                0.2f + 0.6f * glm::fract(p.y * 0.0625f), 0.0f, 1.0f);
            const float emission = emit(p);
            const Vixen::GaiaVoxel::ComponentQueryRequest comps[] = {
                Vixen::GaiaVoxel::Density{sd},
                Vixen::GaiaVoxel::Color{col},
                Vixen::GaiaVoxel::Roughness{rough},
                Vixen::GaiaVoxel::Material{1u},
                Vixen::GaiaVoxel::EmissionIntensity{emission},
            };
            r.world->createVoxel(
                Vixen::GaiaVoxel::VoxelCreationRequest{p, comps});
        }

    return r;
}

// Thin wrapper — analytic recipe path (behaviour-identical to the original).
inline SdfBakeResult BakeRecipeToSdfWorld(uint32_t recipeId, const glm::vec3& center,
                                          const RecipeParams& rp, int n, float bandVoxels,
                                          int brickDepth = 3) {
    return BakeSdfWorld(
        [&](const glm::vec3& p) { return evalSdf(recipeId, p, center, rp); },
        center, n, bandVoxels, brickDepth);
}

// Sampled Lighting Inc3 M3: analytic recipe path + an explicit emissive-intensity
// eval function, for authoring the >=10^3-emissive-voxel gate scene. EmitFn is
// evaluated in the SAME grid space as `eval` (world-space grid coords, not
// object-centered) — callers wanting object-centered emission should subtract
// `center` themselves, mirroring evalSdf's own convention.
template<class EmitFn>
inline SdfBakeResult BakeRecipeToSdfWorldWithEmission(uint32_t recipeId, const glm::vec3& center,
                                                      const RecipeParams& rp, int n, float bandVoxels,
                                                      EmitFn&& emit, int brickDepth = 3) {
    return BakeSdfWorld(
        [&](const glm::vec3& p) { return evalSdf(recipeId, p, center, rp); },
        center, n, bandVoxels, brickDepth,
        std::forward<EmitFn>(emit));
}

// Recipe-instruction path — evaluates a P0 SdfInstruction[] program via the CPU stack-VM.
// The program is authored in OBJECT-CENTERED space (matching evalSdf's
// convention on the analytic path): `center` is subtracted from the grid
// sample point before evaluation, so a primitive authored at local (0,0,0)
// lands at `center` in the grid. This mirrors evalSdf's `p - center` exactly
// (SdfBake.h ~line 45) — the two bake paths are now consistent.
inline SdfBakeResult BakeRecipeInstructionsToSdfWorld(const Recipe::SdfInstruction* prog,
                                                      uint32_t count,
                                                      const glm::vec3& center,
                                                      int n, float bandVoxels,
                                                      int brickDepth = 3) {
    return BakeSdfWorld(
        [&](const glm::vec3& p) { return Recipe::evalRecipe(prog, count, p - center); },
        center, n, bandVoxels, brickDepth);
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
    // Density carries the SIGNED distance (negative inside), so rebuild must keep
    // all-interior bricks: select voxels by occupancy, not density>0. MUST precede
    // rebuild() — it decides which voxels become bricks (fixes the dropped-interior-
    // brick sentinel contamination, the Stored-SDF brick-fleck root cause).
    result.octree->setSignedDistanceField(true);
    result.octree->rebuild(*result.world, worldMin, worldMax);
    result.octree->setBodyOctree(true);

    return result;
}

}  // namespace Vixen::SVO
