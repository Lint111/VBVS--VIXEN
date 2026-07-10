#pragma once
// ResidencyDefault.h — Lazy-Procedural-Delta-Baseline Inc0, M2 Task 4.
//
// Capability-derived boot residency default: lazy (false) iff EVERY octree in a
// ConcatenatedOctrees pool is mip-capable, eager (true) otherwise (mixed or all-binary
// pools keep today's eager behavior exactly — a mip-less lazy leaf renders INVISIBLE,
// not grey, so laziness is only safe when every tree has a real mip fallback to shade
// from). Factored out as a pure, dependency-free function (no node/GPU/graph types),
// the same way ResidencyTrigger.h and ResolvableLevel.h are each independently
// unit-testable — BodyOctreeSceneNode::DeriveResidencyDefaultIfUnset (its one production
// caller) just calls this and stashes the result.
//
// Scope note: this governs ONLY the boot-time population of the binary
// concatenated_.bricks blob (CreateOctreeBuffers/UploadBrickPool). The channelPool,
// nodes, mips, lookup tables, and shell-cache slots still upload whole at Compile —
// their laziness is a future increment's paged pool, not this one.

#include "ShellOctreeGpu.h"  // ConcatenatedOctrees, OctreeConfig, mipPoolBaseOf
#include "MipSample.h"       // sizeof(MipSample) — mip pool byte<->sample-count conversion
#include "ShellDerive.h"     // ShellPool (Task 4b's active-config-view selection)

#include <cstdint>
#include <vector>

namespace Vixen::SVO {

// True if octree `i` in `pool` is mip-capable: channelCount > 0 AND its own slice of
// pool.mipPool (offset mipPoolBaseOf(config), length channelCount*nodeCount MipSamples —
// ConcatenateSdfWithMips's own bookkeeping, MipBake.h:373) is non-empty and in-bounds.
// Checked directly against the mip pool's actual byte range (not just channelCount>0)
// so a hand-built pool with live channels but no baked mips still reports NOT
// mip-capable, rather than the caller silently booting lazy with nothing to shade from.
inline bool IsOctreeMipCapable(const ConcatenatedOctrees& pool, uint32_t i) {
    if (i >= pool.count || i >= pool.configs.size() || i >= pool.nodeCounts.size()) {
        return false;
    }
    const OctreeConfig& cfg = pool.configs[i];
    if (cfg.channelCount == 0u) {
        return false;
    }
    const uint32_t base     = mipPoolBaseOf(cfg);
    const uint32_t nodeCount = pool.nodeCounts[i];
    const uint32_t sliceLen  = cfg.channelCount * nodeCount;  // MipSample units
    const uint32_t poolTotal =
        static_cast<uint32_t>(pool.mipPool.size() / sizeof(MipSample));
    return sliceLen > 0u && base < poolTotal && (base + sliceLen) <= poolTotal;
}

// The boot residency default for the whole pool: false (lazy) iff every octree is
// mip-capable per IsOctreeMipCapable; true (eager) otherwise, including the empty-pool
// case (count==0 keeps today's eager default — nothing to derive laziness FROM).
inline bool DeriveResidencyDefault(const ConcatenatedOctrees& pool) {
    if (pool.count == 0u) {
        return true;  // eager — matches the pre-M2 default for an empty/not-yet-built pool
    }
    for (uint32_t i = 0; i < pool.count; ++i) {
        if (!IsOctreeMipCapable(pool, i)) {
            return true;  // eager — at least one tree has no mip fallback to boot lazy from
        }
    }
    return false;  // lazy — every tree is mip-capable
}

// ---------------------------------------------------------------------------
// M2 Task 4b — shell-cache config reconciliation on residency grant (design §8.7).
// ---------------------------------------------------------------------------
// At Compile, CreateShellBuffers rewrites binding-5 (OCTREE_CONFIG_BUFFER) to the
// shell-COMPACT configs (re-packed per-octree poolBrickBase) whenever a shell cache
// was derived, because the live render samples the compact shell pool, not the
// source pool. A residency-grant re-upload that blindly writes the SOURCE configs
// (concatenated_.configs) would clobber that rewrite at exactly the mip->brick
// transition, corrupting SDF addressing for octree index >=1 in any multi-octree
// pool. StampAndSelectActiveConfigs mutates brickResident=1 into every config in
// the SAME view CreateShellBuffers last wrote (both CPU double-buffer shell slots
// if a shell cache exists; the source configs otherwise) and returns which vector
// a caller should actually re-upload to binding-5.
//
// Pure w.r.t. GPU state (mutates only the passed CPU vectors) so the selection
// logic is unit-testable without a device — BodyOctreeSceneNode::
// PollBrickUploadCompletion is the one production caller (it owns the actual
// device->Upload() call this function deliberately does not make).
inline std::vector<OctreeConfig>* StampAndSelectActiveConfigs(
    ConcatenatedOctrees& source,
    ShellPool shellCache[2]) {
    for (auto& cfg : source.configs) {
        setBrickResident(cfg, true);
    }
    const bool haveShellCache = !shellCache[0].compact.configs.empty();
    if (!haveShellCache) {
        return &source.configs;
    }
    for (auto& cfg : shellCache[0].compact.configs) {
        setBrickResident(cfg, true);
    }
    for (auto& cfg : shellCache[1].compact.configs) {
        setBrickResident(cfg, true);
    }
    return &shellCache[0].compact.configs;
}

}  // namespace Vixen::SVO
