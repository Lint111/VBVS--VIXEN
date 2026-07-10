#pragma once
/**
 * @file RecipeBaker.h
 * @brief I3.3 — BakeRegistryToPool: bake every registered recipe into a
 *        memory-budgeted, count-unbounded ConcatenatedOctrees pool.
 *
 * Ownership: SdfBodyOctree holds unique_ptrs; keep them alive in
 * RecipeBakeResult::owned.  The pool's pointer vector is built from owned.
 */
#include "Recipe/RecipeRegistry.h"
#include "SdfBake.h"         // BakeRecipeInstructionsToSdfWorld, BuildSdfBodyOctree
#include "ShellOctreeGpu.h"  // ConcatenatedOctrees
#include "MipBake.h"         // ConcatenateSdfWithMips

#include <cassert>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Vixen::SVO {

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
struct RecipeBakeConfig {
    glm::vec3 center           = { 32.f, 32.f, 32.f };
    uint32_t  defaultResolution = 64;
    float     defaultBand       = 2.5f;
    uint32_t  defaultBrickDepth = 3;
    // 0 = unbounded.  >0 = hard budget in bytes:
    //   sum of (nodes + bricks + channelPool) bytes across all baked octrees.
    uint64_t  byteBudget        = 0;
};

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------
struct RecipeBakeResult {
    ConcatenatedOctrees          pool;   // concatenated pool (refs into owned)
    std::vector<SdfBodyOctree>   owned;  // keeps unique_ptrs alive
    bool        ok  = true;
    std::string err;
};

// ---------------------------------------------------------------------------
// BakeRegistryToPool
//
// For each registered id (ascending order):
//   1. Bake using the entry's params (fall back to cfg defaults when 0).
//   2. Stamp entry.octreeSlot = k (k = 0-based insertion order).
//   3. After concatenation: if byteBudget>0 and the pool exceeds it,
//      set ok=false with a descriptive err.
// ---------------------------------------------------------------------------
inline RecipeBakeResult BakeRegistryToPool(RecipeRegistry& reg,
                                           const RecipeBakeConfig& cfg) {
    RecipeBakeResult res;
    const auto ids = reg.Ids();  // ascending order guaranteed by RecipeRegistry

    res.owned.reserve(ids.size());

    for (uint32_t k = 0; k < static_cast<uint32_t>(ids.size()); ++k) {
        const uint32_t id    = ids[k];
        RecipeRegistry::RecipeEntry* entry = reg.GetMutable(id);

        const int      n     = (entry->bakeResolution != 0)
                               ? static_cast<int>(entry->bakeResolution)
                               : static_cast<int>(cfg.defaultResolution);
        const float    band  = (entry->bandVoxels != 0.f)
                               ? entry->bandVoxels
                               : cfg.defaultBand;
        const int      depth = (entry->brickDepth != 0)
                               ? static_cast<int>(entry->brickDepth)
                               : static_cast<int>(cfg.defaultBrickDepth);

        auto baked = BakeRecipeInstructionsToSdfWorld(
            entry->bytecode.data(),
            static_cast<uint32_t>(entry->bytecode.size()),
            cfg.center, n, band, depth);

        res.owned.push_back(BuildSdfBodyOctree(baked, depth));

        // ConcatenateSdfWithMips (below) attaches a mip pool per-octree via
        // SdfBodyOctree::octree->getOctree(); BuildSdfBodyOctree always builds
        // this member (see SdfBake.h), so a null here means something upstream
        // silently produced a mip-less octree. Assert rather than silently ship
        // a mip-less pool that M2's lazy residency would then boot invisible.
        assert(res.owned.back().octree->getOctree() != nullptr &&
               "BakeRegistryToPool: baked octree has no LaineKarrasOctree — mip bake would be skipped");

        entry->octreeSlot = k;
    }

    // Build pointer vector from owned (after reserving so no realloc occurs).
    std::vector<const SdfBodyOctree*> ptrs;
    ptrs.reserve(res.owned.size());
    for (const auto& o : res.owned) ptrs.push_back(&o);

    if (!ptrs.empty()) {
        res.pool = ConcatenateSdfWithMips(ptrs);
    }

    // Budget check (0 = unbounded).
    if (cfg.byteBudget > 0) {
        const uint64_t poolBytes =
            static_cast<uint64_t>(res.pool.nodes.size()) +
            static_cast<uint64_t>(res.pool.bricks.size()) +
            static_cast<uint64_t>(res.pool.channelPool.size()) +
            static_cast<uint64_t>(res.pool.mipPool.size());
        if (poolBytes > cfg.byteBudget) {
            res.ok  = false;
            res.err = "recipe pool over budget by " +
                      std::to_string(poolBytes - cfg.byteBudget) + " bytes (" +
                      std::to_string(poolBytes) + " / " +
                      std::to_string(cfg.byteBudget) + " budget, incl. mipPool)";
        }
    }

    return res;
}

}  // namespace Vixen::SVO
