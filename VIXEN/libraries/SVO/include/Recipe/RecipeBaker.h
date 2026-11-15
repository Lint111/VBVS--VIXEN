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
#include "MipBake.h"         // ConcatenateSdfWithMips, ConcatenateSdfWithAniso
#include "MipAnisoPool.h"    // MipAnisoSelfCheck*

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Vixen::SVO {

namespace detail {

inline bool HasNonDistanceRecipeOutput(const RecipeRegistry::RecipeEntry& entry) {
    for (const Recipe::SdfInstruction& instruction : entry.bytecode) {
        const auto* metadata = Recipe::FindRecipeOpcodeMetadata(instruction.opCode);
        if (metadata == nullptr) continue;
        if ((std::strcmp(metadata->name, "MaterialRef") == 0 ||
             std::strcmp(metadata->name, "Attach") == 0) ||
            (std::strcmp(metadata->name, "Output") == 0 && instruction.data[0] != 0.0f))
            return true;
    }
    return false;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
struct RecipeBakeConfig {
    glm::vec3 center           = { 32.f, 32.f, 32.f };
    uint32_t  defaultResolution = 64;
    float     defaultBand       = 2.5f;
    uint32_t  defaultBrickDepth = 3;
    // Recipe-space distance represented by one bake-grid voxel. Stored distances remain in
    // grid units for the octree marcher; evaluators sample at this scale and convert distance.
    float     recipeUnitsPerVoxel = 1.0f;
    // Material sidecars use this only when a RecipeEntry does not carry its own typed default.
    uint32_t  defaultMaterialId = 0u;
    // 0 = unbounded.  >0 = hard budget in bytes:
    //   sum of (nodes + bricks + channelPool + mipPool) bytes across all baked octrees.
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
    if (!(cfg.recipeUnitsPerVoxel > 0.0f) || !std::isfinite(cfg.recipeUnitsPerVoxel)) {
        res.ok = false;
        res.err = "recipeUnitsPerVoxel must be finite and positive";
        return res;
    }
    const auto ids = reg.Ids();  // ascending order guaranteed by RecipeRegistry

    res.owned.reserve(ids.size());

    // Deep-Field Mip Policy — anisotropic coarse mips: flag-gated, additive.
    // VIXEN_MIP_ANISO_BAKE unset (default) reproduces the exact prior
    // behavior (ConcatenateSdfWithMips, no mipAnisoPool) — flag-off is
    // byte-exact. Set to opt into the ConcatenateSdfWithAniso path, which
    // also bakes+attaches mipAnisoPool and prints the boot self-check.
    const bool anisoBake = std::getenv("VIXEN_MIP_ANISO_BAKE") != nullptr;
    uint32_t anisoCoarseTotal = 0;
    uint32_t anisoThresholdLevelReported = 0;
    bool     anisoSampleRowPrinted = false;
    MipAnisoSample anisoBody0Root{};

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

        SdfBakeResult baked;
        if (detail::HasNonDistanceRecipeOutput(*entry)) {
            Recipe::LoweredRecipeProgram outputProgram;
            std::string lowerError;
            const std::span<const float> parameters(entry->parameterValues.data(),
                                                     entry->parameterValues.size());
            const uint32_t defaultMaterialId = entry->defaultMaterialId != 0u
                ? entry->defaultMaterialId : cfg.defaultMaterialId;
            if (!outputProgram.Lower(entry->bytecode.data(),
                    static_cast<std::uint32_t>(entry->bytecode.size()), parameters,
                    true, defaultMaterialId, lowerError)) {
                res.ok = false;
                res.err = "recipe output-set lowering failed for recipe " +
                          std::to_string(id) + ": " + lowerError;
                return res;
            }
            if (outputProgram.OutputCount() <= 1u) {
                res.ok = false;
                res.err = "recipe output marker did not produce an output set for recipe " +
                          std::to_string(id);
                return res;
            }
            RecipeOutputSetBakeEvaluator evaluator(outputProgram, cfg.center, n,
                                                   cfg.recipeUnitsPerVoxel);
            baked = BakeSdfWorld(evaluator, cfg.center, n, band, depth);
        } else {
            baked = BakeRecipeInstructionsToSdfWorld(
                entry->bytecode.data(),
                static_cast<uint32_t>(entry->bytecode.size()),
                cfg.center, n, band, depth,
                std::span<const float>(entry->parameterValues.data(),
                                       entry->parameterValues.size()), {}, {}, nullptr,
                cfg.recipeUnitsPerVoxel);
        }

        res.owned.push_back(BuildSdfBodyOctree(baked, depth));

        // ConcatenateSdfWithMips (below) attaches a mip pool per-octree via
        // SdfBodyOctree::octree->getOctree(); BuildSdfBodyOctree always builds
        // this member (see SdfBake.h), so a null here means something upstream
        // silently produced a mip-less octree. Assert rather than silently ship
        // a mip-less pool that M2's lazy residency would then boot invisible.
        assert(res.owned.back().octree->getOctree() != nullptr &&
               "BakeRegistryToPool: baked octree has no LaineKarrasOctree — mip bake would be skipped");

        entry->octreeSlot = k;

        if (anisoBake) {
            // One extra bake pass per body, boot-time only, purely diagnostic
            // (the real per-body bake ConcatenateSdfWithAniso does below is
            // what actually ships in res.pool — this one just feeds the boot
            // print/self-check numbers without threading state out of the
            // concat loop).
            const Octree* oct = res.owned.back().octree->getOctree();
            if (oct && oct->root && !oct->root->childDescriptors.empty()) {
                SerializedOctree s = SerializeSdfWithMips(res.owned.back());
                const uint32_t thr = detail::DefaultAnisoThresholdLevel(s.config);
                MipAnisoPool p = BakeMipAnisoPool(*oct, s, thr);
                anisoCoarseTotal += p.coarseNodeCount;
                anisoThresholdLevelReported = thr;

                if (!anisoSampleRowPrinted) {
                    anisoBody0Root = p.Get(0, 0);
                    std::printf("[MipAnisoPool] sample row: recipeId=%u node=0 channel=0 thresholdLevel=%u "
                                "cov(+X=%.3f -X=%.3f +Y=%.3f -Y=%.3f +Z=%.3f -Z=%.3f) source=BakeRegistryToPool body 0\n",
                                id, thr, anisoBody0Root.covPosX, anisoBody0Root.covNegX,
                                anisoBody0Root.covPosY, anisoBody0Root.covNegY,
                                anisoBody0Root.covPosZ, anisoBody0Root.covNegZ);
                    anisoSampleRowPrinted = true;
                }
            }
        }
    }

    // Build pointer vector from owned (after reserving so no realloc occurs).
    std::vector<const SdfBodyOctree*> ptrs;
    ptrs.reserve(res.owned.size());
    for (const auto& o : res.owned) ptrs.push_back(&o);

    if (!ptrs.empty()) {
        res.pool = anisoBake ? ConcatenateSdfWithAniso(ptrs) : ConcatenateSdfWithMips(ptrs);
    }

    if (anisoBake && !ptrs.empty()) {
        std::printf("[MipAnisoPool] bodies=%zu totalPoolBytes=%zu coarseNodesWithSample=%u thresholdLevel=%u "
                    "(default = userMaxLevels-brickDepthLevels)\n",
                    res.owned.size(), res.pool.mipAnisoPool.size(), anisoCoarseTotal, anisoThresholdLevelReported);

        // CPU-side self-check (spec item 3): axis-aligned slab -> strong
        // asymmetry, solid cube -> near-isotropic. Reuses body 0's root
        // sample already computed in the loop above (no extra bake pass).
        // A dedicated slab/cube pair is exercised directly in
        // test_mip_aniso_pool.cpp; this just reports PASS/FAIL against
        // whatever body 0 actually is, so every real boot prints numbers.
        if (anisoSampleRowPrinted) {
            const auto slabCheck = MipAnisoSelfCheckSlabAsymmetry(anisoBody0Root);
            std::printf("%s\n", slabCheck.report.c_str());
        }
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
