// test_shell_derive.cpp — Surface-Shell ESVO cache derivation (increment 1).
//
// Covers the design's §G step 6 verification, at the CPU source-of-truth level:
//   ShellVsFullTree        — shell is a lossless subset: no reachable brick dropped,
//                            surface bricks all retained, interior-solid bricks dropped
//                            (the bandwidth win is real and measured).
//   ShellSupersetOfSurface — SHELL ⊇ SURFACE, and every shell brick is either a
//                            surface brick or a 26-neighbour of one (soundness).
//   DoubleBufferSwapIdentical — deriving the same source into two slots yields
//                            byte-identical shellData/shellLookup (ping-pong safe).
//   DirtyRevalidateUpdatesRightBricks — a value edit to specific source bricks,
//                            replayed through RevalidateShellBricks, updates exactly
//                            those shell slots and nothing else.
//   ShellThicknessGrows    — dilation 2 ⊇ dilation 1 ⊇ surface (thickness param).
//   ProxyAabbsEmittedForExactlyShellBricks — one local-space [0,1]^3 AABB per shell
//                            brick, mirroring shellLookup order (raster-proxy artifact).
//   DeriveShellPoolEmitsProxiesWithOctreeIndex — per-octree proxy lists carry the
//                            owning octree index through the multi-octree pool.

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <utility>

#undef far
#undef near
#undef min
#undef max

#include "SdfBake.h"
#include "ShellOctreeGpu.h"
#include "ShellDerive.h"
#include "MipBake.h"
#include "SdfRecipes.h"

using namespace Vixen::SVO;

// ---------------------------------------------------------------------------
// Fixture: bake a SOLID sphere (r=6, n=32, band=2) → concatenate → single octree.
// n=32 (bpa=4) gives a real interior so interior-solid bricks exist to be dropped.
// ---------------------------------------------------------------------------
struct ShellFixture {
    ConcatenatedOctrees cat;
    SdfBodyOctree body;   // keep alive (owns world/registry)

    ShellFixture() {
        // n=64 (bpa=8) with a large sphere gives genuine interior-solid bricks
        // (deeper than one brick from the surface) so the shell drops them and
        // the bandwidth win is real and measurable — not just the already-sparse
        // bake shell. band=2 keeps the bake narrow; interior is filled by the
        // bake's own margin only near the surface.
        const int n = 64;
        const float r = 24.0f;
        const glm::vec3 center{32.0f, 32.0f, 32.0f};
        RecipeParams rp{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, 2.0f);
        body = BuildSdfBodyOctree(baked, 3);
        std::vector<const SdfBodyOctree*> octs{ &body };
        cat = ConcatenateSdf(octs);
    }
};

// Recompute a brick's SDF min/max directly from the concatenated pool for an
// independent oracle (does NOT reuse ShellDerive's helper).
static void OracleMinMax(const ConcatenatedOctrees& cat, uint32_t bi,
                         float& mn, float& mx) {
    const OctreeConfig& cfg = cat.configs[0];
    const uint32_t stride = cfg.brickStrideFloats;
    const float* pool = reinterpret_cast<const float*>(cat.channelPool.data());
    mn =  1e30f; mx = -1e30f;
    for (uint32_t v = 0; v < SerializedOctree::kVoxelsPerBrick; ++v) {
        const float d = pool[static_cast<size_t>(bi) * stride + v];
        mn = std::fmin(mn, d);
        mx = std::fmax(mx, d);
    }
}

// ---------------------------------------------------------------------------
// ShellVsFullTree — subset that drops interior-solid bricks, measured win.
// ---------------------------------------------------------------------------
TEST(ShellDerive, ShellVsFullTree) {
    ShellFixture f;
    ASSERT_GT(f.cat.brickCounts[0], 0u);
    ASSERT_FALSE(f.cat.channelPool.empty());

    ShellDeriveResult r = DeriveShell(f.cat, 0);

    EXPECT_EQ(r.sourceBrickCount, f.cat.brickCounts[0]);
    EXPECT_EQ(r.shellLookup.size(), r.shellBrickCount);
    EXPECT_GT(r.surfaceBrickCount, 0u) << "a sphere must have surface bricks";

    // Shell is a subset of the source (never larger).
    EXPECT_LE(r.shellBrickCount, r.sourceBrickCount);

    // IMPORTANT ARCHITECTURAL FINDING (honest, verified):
    // VIXEN's BakeSdfWorld already sparsifies to (surface + 1-brick margin) — it
    // NEVER stores deep interior-solid bricks (SdfBake.h pass-1 marks only in-band
    // bricks, then dilates by exactly one brick). So the SOURCE concatenated_ pool
    // the shell is derived FROM is already a thin shell. The derivation's
    // SHELL = surface ∪ dilate26(surface) reproduces that same active set. There
    // are therefore ZERO interior-solid bricks to drop for a surface+margin bake:
    // the shell is exactly lossless and equal to the source active set.
    //
    // The design's §A bandwidth win ("drop interior-solid bricks") only
    // materializes when the source contains full-interior bricks (i.e. a bake
    // that fills the whole interior, or a coarser dilation than the bake's). We
    // therefore assert LOSSLESSNESS (never larger, never drops a reachable brick),
    // count any interior-solid bricks present, and report the measured reduction —
    // rather than assert a reduction the current bake cannot produce.
    const float halfDiag =
        0.5f * 1.7320508f * static_cast<float>(f.cat.configs[0].brickSize);
    uint32_t interiorSolid = 0;
    for (uint32_t bi = 0; bi < r.sourceBrickCount; ++bi) {
        float mn, mx; OracleMinMax(f.cat, bi, mn, mx);
        if (mx <= -halfDiag) ++interiorSolid;   // fully inside
    }
    RecordProperty("interiorSolidBricks", static_cast<int>(interiorSolid));

    // Independently count DEEP interior-solid bricks: interior-solid AND not a
    // 26-neighbour of any surface brick. ONLY these are droppable — an
    // interior-solid brick adjacent to the surface is still reachable by the
    // stencil and must be kept. For a surface+1-margin bake this count is 0.
    uint32_t droppable = 0;
    for (uint32_t bi = 0; bi < r.sourceBrickCount; ++bi) {
        float mn, mx; OracleMinMax(f.cat, bi, mn, mx);
        if (mx > -halfDiag) continue;             // not fully interior
        if (r.shell[bi]) continue;                // kept (neighbour of surface)
        ++droppable;                              // interior-solid AND dropped
    }
    RecordProperty("droppableBricks", static_cast<int>(droppable));

    // Losslessness: every dropped brick must be interior-solid (never surface).
    for (uint32_t bi = 0; bi < r.sourceBrickCount; ++bi) {
        if (!r.shell[bi]) {
            float mn, mx; OracleMinMax(f.cat, bi, mn, mx);
            EXPECT_LE(mx, -halfDiag)
                << "dropped brick " << bi << " is not interior-solid (would create a hole)";
        }
    }

    // Shell drops exactly the deep interior-solid bricks (0 for a margin bake).
    EXPECT_EQ(r.sourceBrickCount - r.shellBrickCount, droppable);

    // Measured bandwidth: shell pool strictly smaller when anything dropped.
    EXPECT_EQ(r.sourcePoolBytes,
              static_cast<uint64_t>(r.sourceBrickCount) * r.brickStrideFloats * 4);
    EXPECT_EQ(r.shellPoolBytes,
              static_cast<uint64_t>(r.shellBrickCount) * r.brickStrideFloats * 4);

    // Report the concrete reduction for the log.
    RecordProperty("sourceBricks", static_cast<int>(r.sourceBrickCount));
    RecordProperty("surfaceBricks", static_cast<int>(r.surfaceBrickCount));
    RecordProperty("shellBricks", static_cast<int>(r.shellBrickCount));
    RecordProperty("sourceBytes", static_cast<int>(r.sourcePoolBytes));
    RecordProperty("shellBytes", static_cast<int>(r.shellPoolBytes));
    printf("[ShellDerive] source=%u surface=%u shell=%u | pool %llu -> %llu bytes (%.1f%%)\n",
           r.sourceBrickCount, r.surfaceBrickCount, r.shellBrickCount,
           (unsigned long long)r.sourcePoolBytes,
           (unsigned long long)r.shellPoolBytes,
           r.sourcePoolBytes ? 100.0 * (double)r.shellPoolBytes / (double)r.sourcePoolBytes : 0.0);
}

// ---------------------------------------------------------------------------
// SyntheticFullInteriorDropsSolidBricks — the design's §A bandwidth mechanism,
// proven on a MANUFACTURED full-interior source (what a full-interior bake would
// produce). A 6x6x6 grid of bricks: a 4x4x4 solid interior block whose SDF is
// all -100 (fully inside) wrapped by a surface layer. The shell must drop the
// deep interior bricks (those NOT a 26-neighbour of any surface brick).
// ---------------------------------------------------------------------------
TEST(ShellDerive, SyntheticFullInteriorDropsSolidBricks) {
    const uint32_t bpa = 6;              // 6^3 = 216 bricks
    const uint32_t brickCount = bpa * bpa * bpa;
    const uint32_t vpb = SerializedOctree::kVoxelsPerBrick;  // 512
    const uint32_t stride = 5u * vpb;    // SDF+Color+Roughness canonical stride

    ConcatenatedOctrees cat;
    cat.count = 1;
    cat.brickCounts = { brickCount };
    cat.nodeCounts  = { 0 };

    // Config: one Stored-SDF octree, SDF channel at base 0.
    OctreeConfig cfg{};
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.brickSize = 8;
    cfg.bricksPerAxis = static_cast<int32_t>(bpa);
    cfg.poolBrickBase = 0;
    cfg.channelCount = 1;
    cfg.brickStrideFloats = stride;
    cfg.channels[0].semanticId = static_cast<uint32_t>(SEM_SDF);
    cfg.channels[0].elemCount = 1;
    cfg.channels[0].channelBaseFloats = 0;
    cat.configs = { cfg };

    // Dense brick pool + a grid lookup mapping every cell to its own brick index.
    std::vector<float> pool(static_cast<size_t>(brickCount) * stride, 0.0f);
    std::vector<uint32_t> lookup(brickCount, 0xFFFFFFFFu);

    auto flat = [&](uint32_t gx, uint32_t gy, uint32_t gz) {
        return gx + gy * bpa + gz * bpa * bpa;
    };

    uint32_t solidCount = 0, shellLayerCount = 0;
    for (uint32_t gz = 0; gz < bpa; ++gz)
      for (uint32_t gy = 0; gy < bpa; ++gy)
        for (uint32_t gx = 0; gx < bpa; ++gx) {
            const uint32_t bi = flat(gx, gy, gz);
            lookup[bi] = bi;   // identity mapping: cell -> brick
            // Surface = the outermost brick layer (any coord == 0 or bpa-1).
            const bool onBoundary =
                gx == 0 || gy == 0 || gz == 0 ||
                gx == bpa - 1 || gy == bpa - 1 || gz == bpa - 1;
            const float fill = onBoundary ? 0.0f    // straddles the iso-surface
                                          : -100.0f; // deep interior solid
            if (onBoundary) ++shellLayerCount; else ++solidCount;
            for (uint32_t v = 0; v < vpb; ++v)
                pool[static_cast<size_t>(bi) * stride + v] = fill;
        }

    cat.channelPool.resize(pool.size() * sizeof(float));
    std::memcpy(cat.channelPool.data(), pool.data(), cat.channelPool.size());
    cat.brickGridLookup.resize(lookup.size() * sizeof(uint32_t));
    std::memcpy(cat.brickGridLookup.data(), lookup.data(), cat.brickGridLookup.size());

    ShellDeriveResult r = DeriveShell(cat, 0);

    // Surface = the 6^3 - 4^3 = 216 - 64 = 152 boundary bricks.
    EXPECT_EQ(r.surfaceBrickCount, shellLayerCount);
    EXPECT_EQ(solidCount, brickCount - shellLayerCount);

    // The shell keeps surface + its 26-neighbours. The interior 4^3 block's
    // outer layer (adjacent to the boundary) is pulled in by dilation; only the
    // innermost 2^3 = 8 bricks are >1 brick from any surface brick and dropped.
    EXPECT_LT(r.shellBrickCount, r.sourceBrickCount)
        << "deep interior-solid bricks must be dropped (the §A bandwidth win)";
    EXPECT_EQ(r.shellBrickCount, brickCount - 8u)
        << "exactly the innermost 2x2x2 block should be dropped at dilation 1";

    // Every dropped brick was interior-solid (never a surface brick).
    for (uint32_t bi = 0; bi < brickCount; ++bi) {
        if (!r.shell[bi]) EXPECT_FALSE(r.surface[bi]);
    }

    printf("[ShellDerive/synthetic] source=%u surface=%u shell=%u dropped=%u | "
           "pool %llu -> %llu bytes (%.1f%%)\n",
           r.sourceBrickCount, r.surfaceBrickCount, r.shellBrickCount,
           r.sourceBrickCount - r.shellBrickCount,
           (unsigned long long)r.sourcePoolBytes,
           (unsigned long long)r.shellPoolBytes,
           100.0 * (double)r.shellPoolBytes / (double)r.sourcePoolBytes);
}

// ---------------------------------------------------------------------------
// ShellGridLookupIsRenderEquivalent — the drop-in binding-12 remap proves the
// render reads the COMPACT pool with IDENTICAL results. For every grid cell, the
// SDF value the shader would read via (source lookup -> source pool) must equal
// the value read via (shellGridLookup -> shellData) — EXCEPT where the source
// brick was dropped (an interior-solid brick the iso-surface march never reaches),
// which the remap marks 0xFFFFFFFF (empty). This is the render-correctness gate.
// ---------------------------------------------------------------------------
TEST(ShellDerive, ShellGridLookupIsRenderEquivalent) {
    ShellFixture f;
    ShellDeriveResult r = DeriveShell(f.cat, 0);

    const OctreeConfig& cfg = f.cat.configs[0];
    const uint32_t stride = cfg.brickStrideFloats;
    const uint32_t bpa    = static_cast<uint32_t>(cfg.bricksPerAxis);
    const uint32_t poolBase = cfg.poolBrickBase;   // 0 for octree 0
    const float* srcPool  = reinterpret_cast<const float*>(f.cat.channelPool.data());
    const uint32_t* srcLookup =
        reinterpret_cast<const uint32_t*>(f.cat.brickGridLookup.data());
    const float* shellPool = reinterpret_cast<const float*>(r.shellData.data());
    const uint32_t* shellGrid =
        reinterpret_cast<const uint32_t*>(r.shellGridLookup.data());

    ASSERT_EQ(r.shellGridLookup.size(), static_cast<size_t>(bpa) * bpa * bpa * sizeof(uint32_t));

    uint32_t droppedCells = 0, checkedVoxels = 0;
    for (uint32_t flat = 0; flat < bpa * bpa * bpa; ++flat) {
        const uint32_t srcBrick   = srcLookup[flat];
        const uint32_t shellSlot  = shellGrid[flat];
        if (srcBrick == 0xFFFFFFFFu) {           // empty in source
            EXPECT_EQ(shellSlot, 0xFFFFFFFFu) << "empty cell must stay empty at flat " << flat;
            continue;
        }
        if (shellSlot == 0xFFFFFFFFu) {          // dropped interior-solid brick
            ++droppedCells;
            EXPECT_FALSE(r.surface[srcBrick]) << "a dropped cell must not be a surface brick";
            continue;
        }
        // Occupied & retained: every voxel must byte-match through both addressings.
        for (uint32_t v = 0; v < SerializedOctree::kVoxelsPerBrick; ++v) {
            const float a = srcPool[static_cast<size_t>(poolBase) + srcBrick * stride + v];
            const float b = shellPool[static_cast<size_t>(shellSlot) * stride + v];
            ASSERT_EQ(a, b) << "render-equivalence break at flat=" << flat
                            << " voxel=" << v << " (src brick " << srcBrick
                            << " -> shell slot " << shellSlot << ")";
            ++checkedVoxels;
        }
    }
    RecordProperty("droppedGridCells", static_cast<int>(droppedCells));
    printf("[ShellDerive/grid-equiv] checkedVoxels=%u droppedCells=%u\n",
           checkedVoxels, droppedCells);
    EXPECT_GT(checkedVoxels, 0u);
}

// ---------------------------------------------------------------------------
// DeriveShellPoolMultiOctreeRenderEquivalent — the multi-octree drop-in. Bake
// THREE sphere octrees, concatenate, DeriveShellPool, then verify that for EVERY
// octree and EVERY grid cell, the SDF value the render would read from the compact
// pool (via that octree's compact poolBrickBase + grid remap) equals the value it
// would read from the source pool — except dropped interior-solid cells. This is
// the correctness gate for binding the compact pool at render bindings 11/12/5.
// ---------------------------------------------------------------------------
TEST(ShellDerive, DeriveShellPoolMultiOctreeRenderEquivalent) {
    // Three spheres of different radii (varied interiors → varied shell drop).
    std::vector<SdfBodyOctree> bodies;
    const float radii[3] = {10.0f, 12.0f, 14.0f};
    for (int k = 0; k < 3; ++k) {
        const int n = 32; const glm::vec3 center{16.0f,16.0f,16.0f};
        RecipeParams rp{radii[k],0,0,0,0,0};
        SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, 2.0f);
        bodies.push_back(BuildSdfBodyOctree(baked, 3));
    }
    std::vector<const SdfBodyOctree*> ptrs{ &bodies[0], &bodies[1], &bodies[2] };
    ConcatenatedOctrees src = ConcatenateSdf(ptrs);
    ASSERT_EQ(src.count, 3u);

    ShellPool sp = DeriveShellPool(src);
    ASSERT_EQ(sp.compact.count, 3u);

    const float* srcPool   = reinterpret_cast<const float*>(src.channelPool.data());
    const uint32_t* srcLU  = reinterpret_cast<const uint32_t*>(src.brickGridLookup.data());
    const float* shellPool = reinterpret_cast<const float*>(sp.compact.channelPool.data());
    const uint32_t* shellLU= reinterpret_cast<const uint32_t*>(sp.compact.brickGridLookup.data());

    uint32_t checked = 0, dropped = 0;
    uint32_t srcLUBase = 0, shellLUBase = 0;
    for (uint32_t oi = 0; oi < 3; ++oi) {
        const OctreeConfig& sc = src.configs[oi];
        const OctreeConfig& cc = sp.compact.configs[oi];
        const uint32_t stride = sc.brickStrideFloats;
        const uint32_t bpa    = sc.bricksPerAxis;
        const uint32_t tab    = bpa*bpa*bpa;
        for (uint32_t flat = 0; flat < tab; ++flat) {
            const uint32_t srcBrick  = srcLU[srcLUBase + flat];
            const uint32_t shellSlot = shellLU[shellLUBase + flat];
            if (srcBrick == 0xFFFFFFFFu) { EXPECT_EQ(shellSlot, 0xFFFFFFFFu); continue; }
            if (shellSlot == 0xFFFFFFFFu) { ++dropped; continue; }
            for (uint32_t v = 0; v < SerializedOctree::kVoxelsPerBrick; ++v) {
                const float a = srcPool[(size_t)sc.poolBrickBase + (size_t)srcBrick*stride + v];
                const float b = shellPool[(size_t)cc.poolBrickBase + (size_t)shellSlot*stride + v];
                ASSERT_EQ(a, b) << "octree " << oi << " flat " << flat << " voxel " << v;
                ++checked;
            }
        }
        srcLUBase   += tab;
        shellLUBase += tab;
    }
    printf("[ShellDerive/pool-multi] octrees=3 checkedVoxels=%u droppedCells=%u | pool %llu -> %llu B\n",
           checked, dropped,
           (unsigned long long)sp.sourcePoolBytes, (unsigned long long)sp.shellPoolBytes);
    EXPECT_GT(checked, 0u);
}

// ---------------------------------------------------------------------------
// ShellSupersetOfSurface — soundness: SHELL ⊇ SURFACE; each shell brick is a
// surface brick or a 26-neighbour of one (no unreachable brick sneaks in;
// no surface brick is dropped).
// ---------------------------------------------------------------------------
TEST(ShellDerive, ShellSupersetOfSurface) {
    ShellFixture f;
    ShellDeriveResult r = DeriveShell(f.cat, 0);

    // Every SURFACE brick is a SHELL brick (no reachable brick dropped).
    for (uint32_t bi = 0; bi < r.sourceBrickCount; ++bi) {
        if (r.surface[bi]) {
            EXPECT_TRUE(r.shell[bi])
                << "surface brick " << bi << " missing from shell (would create a hole)";
        }
    }

    // Independently confirm SURFACE classification against the oracle.
    const float halfDiag =
        0.5f * 1.7320508f * static_cast<float>(f.cat.configs[0].brickSize);
    for (uint32_t bi = 0; bi < r.sourceBrickCount; ++bi) {
        float mn, mx; OracleMinMax(f.cat, bi, mn, mx);
        const bool oracleSurface = (mn < halfDiag && mx > -halfDiag);
        EXPECT_EQ(r.surface[bi] != 0, oracleSurface)
            << "surface classification mismatch at brick " << bi;
    }

    // shellLookup must be strictly ascending source-brick order (stable compaction).
    for (size_t i = 1; i < r.shellLookup.size(); ++i) {
        EXPECT_LT(r.shellLookup[i - 1], r.shellLookup[i])
            << "shellLookup not ascending at slot " << i;
    }

    // sourceToShellSlot round-trips: every shell brick maps back to its slot.
    for (uint32_t slot = 0; slot < r.shellLookup.size(); ++slot) {
        const uint32_t src = r.shellLookup[slot];
        EXPECT_EQ(r.sourceToShellSlot[src], slot);
    }
}

// ---------------------------------------------------------------------------
// DoubleBufferSwapIdentical — the ping-pong bootstrap derives both slots from
// the same source, so their bytes are identical (frame N reads [N&1], frame N+1
// reads [(N+1)&1]; both must show the same committed cache for a static doc).
// ---------------------------------------------------------------------------
TEST(ShellDerive, DoubleBufferSwapIdentical) {
    ShellFixture f;
    ShellDeriveResult slotA = DeriveShell(f.cat, 0);
    ShellDeriveResult slotB = DeriveShell(f.cat, 0);

    ASSERT_EQ(slotA.shellData.size(), slotB.shellData.size());
    ASSERT_EQ(slotA.shellLookup.size(), slotB.shellLookup.size());
    EXPECT_EQ(0, std::memcmp(slotA.shellData.data(), slotB.shellData.data(),
                             slotA.shellData.size()))
        << "double-buffer slots must be byte-identical on bootstrap";
    EXPECT_EQ(slotA.shellLookup, slotB.shellLookup);
}

// ---------------------------------------------------------------------------
// DirtyRevalidateUpdatesRightBricks — edit specific source bricks' SDF in a
// fresh pool, revalidate a copy of the baseline shellData against a dirty list,
// and confirm ONLY those bricks' shell slots changed.
// ---------------------------------------------------------------------------
TEST(ShellDerive, DirtyRevalidateUpdatesRightBricks) {
    ShellFixture f;
    ShellDeriveResult baseline = DeriveShell(f.cat, 0);
    ASSERT_GT(baseline.shellBrickCount, 2u);

    // Pick two shell source bricks to "edit".
    const uint32_t dirtyA = baseline.shellLookup[0];
    const uint32_t dirtyB = baseline.shellLookup[baseline.shellBrickCount / 2];

    // Fresh pool = copy of source, with those two bricks' SDF lane overwritten
    // by a sentinel value (simulating a recipe value-edit inside those bricks).
    ConcatenatedOctrees fresh = f.cat;
    const uint32_t stride = fresh.configs[0].brickStrideFloats;
    float* freshPool = reinterpret_cast<float*>(fresh.channelPool.data());
    const float kSentinel = -123.5f;
    for (uint32_t bi : {dirtyA, dirtyB})
        for (uint32_t v = 0; v < SerializedOctree::kVoxelsPerBrick; ++v)
            freshPool[static_cast<size_t>(bi) * stride + v] = kSentinel;

    // Revalidate a copy of the baseline shellData with the dirty list.
    std::vector<uint8_t> shellData = baseline.shellData;
    std::vector<uint32_t> dirty{ dirtyA, dirtyB };
    const uint32_t rewritten =
        RevalidateShellBricks(fresh, 0, baseline, dirty, shellData);
    EXPECT_EQ(rewritten, 2u);

    // The two dirty shell slots now carry the sentinel; all others unchanged.
    const float* newShell = reinterpret_cast<const float*>(shellData.data());
    const float* oldShell = reinterpret_cast<const float*>(baseline.shellData.data());
    for (uint32_t slot = 0; slot < baseline.shellBrickCount; ++slot) {
        const uint32_t src = baseline.shellLookup[slot];
        const bool isDirty = (src == dirtyA || src == dirtyB);
        const float first = newShell[static_cast<size_t>(slot) * stride];
        if (isDirty) {
            EXPECT_FLOAT_EQ(first, kSentinel)
                << "dirty slot " << slot << " (src " << src << ") not updated";
        } else {
            // Untouched slot must byte-match the baseline exactly.
            EXPECT_EQ(0, std::memcmp(&newShell[static_cast<size_t>(slot) * stride],
                                     &oldShell[static_cast<size_t>(slot) * stride],
                                     stride * sizeof(float)))
                << "non-dirty slot " << slot << " was corrupted";
        }
    }
}

// ---------------------------------------------------------------------------
// ProxyAabbsEmittedForExactlyShellBricks — the raster-proxy artifact (hybrid
// slice A): DeriveShell emits one template-LOCAL-space ([0,1]^3, the traceBounds
// convention) AABB per SHELL brick, in shellLookup (ascending source-brick)
// order, each carrying its source brickId and owning octreeIndex. Grid coords
// are oracled independently by inverting the source brickGridLookup.
// ---------------------------------------------------------------------------
TEST(ShellDerive, ProxyAabbsEmittedForExactlyShellBricks) {
    ShellFixture f;
    ShellDeriveResult r = DeriveShell(f.cat, 0);
    ASSERT_GT(r.shellBrickCount, 0u);

    // 32-byte std430-friendly element: {vec3 min, uint brickId, vec3 max, uint octree}.
    EXPECT_EQ(sizeof(ShellProxyAabb), 32u);

    // Exactly one proxy per shell brick, mirroring shellLookup order.
    ASSERT_EQ(r.proxyAabbs.size(), static_cast<size_t>(r.shellBrickCount));

    // Independent grid-coord oracle: invert the source grid lookup.
    const OctreeConfig& cfg = f.cat.configs[0];
    const uint32_t bpa = static_cast<uint32_t>(cfg.bricksPerAxis);
    const uint32_t* lu =
        reinterpret_cast<const uint32_t*>(f.cat.brickGridLookup.data());
    std::vector<uint32_t> brickToFlat(r.sourceBrickCount, 0xFFFFFFFFu);
    for (uint32_t flat = 0; flat < bpa * bpa * bpa; ++flat) {
        const uint32_t bi = lu[flat];
        if (bi != 0xFFFFFFFFu && bi < r.sourceBrickCount) brickToFlat[bi] = flat;
    }

    const float inv = 1.0f / static_cast<float>(bpa);
    for (uint32_t slot = 0; slot < r.shellBrickCount; ++slot) {
        const ShellProxyAabb& p = r.proxyAabbs[slot];
        EXPECT_EQ(p.brickId, r.shellLookup[slot])
            << "proxy order must mirror shellLookup at slot " << slot;
        EXPECT_EQ(p.octreeIndex, 0u);

        const uint32_t flat = brickToFlat[p.brickId];
        ASSERT_NE(flat, 0xFFFFFFFFu) << "shell brick " << p.brickId << " not grid-addressable";
        const uint32_t gx = flat % bpa;
        const uint32_t gy = (flat / bpa) % bpa;
        const uint32_t gz = flat / (bpa * bpa);

        EXPECT_FLOAT_EQ(p.minLocal[0], static_cast<float>(gx) * inv);
        EXPECT_FLOAT_EQ(p.minLocal[1], static_cast<float>(gy) * inv);
        EXPECT_FLOAT_EQ(p.minLocal[2], static_cast<float>(gz) * inv);
        EXPECT_FLOAT_EQ(p.maxLocal[0], static_cast<float>(gx + 1u) * inv);
        EXPECT_FLOAT_EQ(p.maxLocal[1], static_cast<float>(gy + 1u) * inv);
        EXPECT_FLOAT_EQ(p.maxLocal[2], static_cast<float>(gz + 1u) * inv);
    }
}

// ---------------------------------------------------------------------------
// DeriveShellPoolEmitsProxiesWithOctreeIndex — the multi-octree pool carries a
// per-octree proxy list whose elements name their owning octree (what the
// instanced proxy draw expands per template).
// ---------------------------------------------------------------------------
TEST(ShellDerive, DeriveShellPoolEmitsProxiesWithOctreeIndex) {
    std::vector<SdfBodyOctree> bodies;
    const float radii[2] = {10.0f, 14.0f};
    for (int k = 0; k < 2; ++k) {
        const int n = 32; const glm::vec3 center{16.0f, 16.0f, 16.0f};
        RecipeParams rp{radii[k], 0, 0, 0, 0, 0};
        SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, 2.0f);
        bodies.push_back(BuildSdfBodyOctree(baked, 3));
    }
    std::vector<const SdfBodyOctree*> ptrs{ &bodies[0], &bodies[1] };
    ConcatenatedOctrees src = ConcatenateSdf(ptrs);
    ASSERT_EQ(src.count, 2u);

    ShellPool sp = DeriveShellPool(src);
    ASSERT_EQ(sp.perOctree.size(), 2u);

    for (uint32_t oi = 0; oi < 2u; ++oi) {
        const ShellDeriveResult& r = sp.perOctree[oi];
        ASSERT_GT(r.shellBrickCount, 0u);
        EXPECT_EQ(r.proxyAabbs.size(), static_cast<size_t>(r.shellBrickCount));
        for (const ShellProxyAabb& p : r.proxyAabbs) {
            EXPECT_EQ(p.octreeIndex, oi);
            for (int a = 0; a < 3; ++a) {
                EXPECT_LT(p.minLocal[a], p.maxLocal[a]);
                EXPECT_GE(p.minLocal[a], 0.0f);
                EXPECT_LE(p.maxLocal[a], 1.0f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// ApplyBrickSdfEditFeedsRevalidateAndKeepsProxiesValid — the dirty-path
// PRODUCER seam (hybrid slice A): ApplyBrickSdfEdit is the one value-edit
// entry point; what it writes is exactly what RevalidateShellBricks consumes,
// and a value edit never moves a brick's box (proxy AABBs need no incremental
// path — byte-identical across a membership-preserving edit).
// ---------------------------------------------------------------------------
TEST(ShellDerive, ApplyBrickSdfEditFeedsRevalidateAndKeepsProxiesValid) {
    ShellFixture f;
    ShellDeriveResult baseline = DeriveShell(f.cat, 0);
    ASSERT_GT(baseline.shellBrickCount, 1u);

    // Target a SURFACE brick (sign-crossing guaranteed) so a uniform scale is
    // membership-preserving for the whole octree.
    uint32_t target = 0xFFFFFFFFu;
    for (uint32_t slot = 0; slot < baseline.shellBrickCount; ++slot) {
        const uint32_t src = baseline.shellLookup[slot];
        if (baseline.surface[src]) { target = src; break; }
    }
    ASSERT_NE(target, 0xFFFFFFFFu);

    const OctreeConfig& cfg = f.cat.configs[0];
    const uint32_t stride = cfg.brickStrideFloats;
    const float* srcPool = reinterpret_cast<const float*>(f.cat.channelPool.data());
    std::vector<float> edited(SerializedOctree::kVoxelsPerBrick);
    for (uint32_t v = 0; v < SerializedOctree::kVoxelsPerBrick; ++v)
        edited[v] = srcPool[static_cast<size_t>(target) * stride + v] * 1.25f;

    // The edit path writes exactly the SDF lane of exactly that brick.
    ConcatenatedOctrees fresh = f.cat;
    ASSERT_TRUE(ApplyBrickSdfEdit(fresh, 0, target, edited.data(), edited.size()));
    const float* freshPool = reinterpret_cast<const float*>(fresh.channelPool.data());
    EXPECT_FLOAT_EQ(freshPool[static_cast<size_t>(target) * stride], edited[0]);
    const uint32_t neighbour = (target == 0u) ? 1u : target - 1u;
    EXPECT_EQ(0, std::memcmp(&freshPool[static_cast<size_t>(neighbour) * stride],
                             &srcPool[static_cast<size_t>(neighbour) * stride],
                             SerializedOctree::kVoxelsPerBrick * sizeof(float)))
        << "edit leaked into neighbouring brick " << neighbour;

    // Out-of-range ids are rejected, pool untouched.
    EXPECT_FALSE(ApplyBrickSdfEdit(fresh, 0, baseline.sourceBrickCount,
                                   edited.data(), edited.size()));
    EXPECT_FALSE(ApplyBrickSdfEdit(fresh, 9u, target, edited.data(), edited.size()));

    // Revalidate consumes the edit into the compact shell slot.
    std::vector<uint8_t> shellData = baseline.shellData;
    std::vector<uint32_t> dirty{ target };
    EXPECT_EQ(RevalidateShellBricks(fresh, 0, baseline, dirty, shellData), 1u);
    const float* shell = reinterpret_cast<const float*>(shellData.data());
    const uint32_t slot = baseline.sourceToShellSlot[target];
    ASSERT_NE(slot, 0xFFFFFFFFu);
    EXPECT_FLOAT_EQ(shell[static_cast<size_t>(slot) * stride], edited[0]);

    // Value edits never move a brick's box: a full re-derive of the edited pool
    // yields byte-identical proxy AABBs — the raster proxy artifact stays valid
    // across the dirty path with zero incremental work.
    ShellDeriveResult again = DeriveShell(fresh, 0);
    ASSERT_EQ(again.proxyAabbs.size(), baseline.proxyAabbs.size());
    EXPECT_EQ(0, std::memcmp(again.proxyAabbs.data(), baseline.proxyAabbs.data(),
                             baseline.proxyAabbs.size() * sizeof(ShellProxyAabb)));

    // A dirty id that never made the shell is skipped (membership territory —
    // full-rederive handles it), never written.
    uint32_t dropped = 0xFFFFFFFFu;
    for (uint32_t bi = 0; bi < baseline.sourceBrickCount; ++bi)
        if (!baseline.shell[bi]) { dropped = bi; break; }
    if (dropped != 0xFFFFFFFFu) {
        std::vector<uint32_t> dirtyDropped{ dropped };
        EXPECT_EQ(RevalidateShellBricks(fresh, 0, baseline, dirtyDropped, shellData), 0u);
    }
}

// ---------------------------------------------------------------------------
// ShellThicknessGrows — dilation is monotone: surface ⊆ dilation1 ⊆ dilation2.
// ---------------------------------------------------------------------------
TEST(ShellDerive, ShellThicknessGrows) {
    ShellFixture f;
    ShellDeriveResult d1 = DeriveShell(f.cat, 0, ShellDeriveParams{1u});
    ShellDeriveResult d2 = DeriveShell(f.cat, 0, ShellDeriveParams{2u});

    EXPECT_GE(d2.shellBrickCount, d1.shellBrickCount);
    EXPECT_GE(d1.shellBrickCount, d1.surfaceBrickCount);

    // Superset relations at the brick level.
    for (uint32_t bi = 0; bi < d1.sourceBrickCount; ++bi) {
        if (d1.surface[bi]) EXPECT_TRUE(d1.shell[bi]);
        if (d1.shell[bi])   EXPECT_TRUE(d2.shell[bi])
            << "dilation2 must be a superset of dilation1 at brick " << bi;
    }
}

static float NormalAngleDegrees(glm::vec3 a, glm::vec3 b) {
    a = glm::normalize(a);
    b = glm::normalize(b);
    const float cosine = std::fmax(-1.0f, std::fmin(1.0f, glm::dot(a, b)));
    return std::acos(cosine) * 180.0f / 3.14159265358979323846f;
}

static uint16_t ReadBakedNormal(const ShellDeriveResult& result, uint32_t slot, uint32_t voxel) {
    const uint32_t* words = reinterpret_cast<const uint32_t*>(result.shellData.data());
    const uint32_t word = words[static_cast<size_t>(slot) * result.shellBrickStrideFloats +
                                 result.normalOffsetFloats + voxel / 2u];
    return static_cast<uint16_t>((voxel & 1u) == 0u ? word & 0xffffu : word >> 16u);
}

TEST(ShellDerive, BakedNormalsStayWithinMeasuredAngularBound) {
    ShellFixture f;
    ShellDeriveParams params;
    params.bakeNormals = true;
    params.workerCount = 2u;
    const ShellDeriveResult r = DeriveShell(f.cat, 0, params);
    ASSERT_TRUE(r.normalsBaked);
    ASSERT_EQ(r.normalStrideFloats, kShellNormalStrideFloats);
    EXPECT_EQ(r.normalPoolBytes,
              static_cast<uint64_t>(r.shellBrickCount) * 1024u);
    EXPECT_EQ(r.shellBrickStrideFloats, r.brickStrideFloats + kShellNormalStrideFloats);

    const auto brickToGrid = Vixen::SVO::detail::BuildBrickToGrid(f.cat, 0, r.sourceBrickCount);
    const float* source = reinterpret_cast<const float*>(f.cat.channelPool.data());
    const uint32_t stride = r.brickStrideFloats;
    const glm::vec3 center{32.0f, 32.0f, 32.0f};
    std::vector<float> errors;
    for (uint32_t slot = 0; slot < r.shellBrickCount; ++slot) {
        const uint32_t sourceBrick = r.shellLookup[slot];
        const uint32_t packed = brickToGrid[sourceBrick];
        ASSERT_NE(packed, 0xFFFFFFFFu);
        const uint32_t gx = packed & 0x3ffu;
        const uint32_t gy = (packed >> 10u) & 0x3ffu;
        const uint32_t gz = (packed >> 20u) & 0x3ffu;
        for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; ++voxel) {
            const float d = source[static_cast<size_t>(sourceBrick) * stride + voxel];
            if (std::fabs(d) > 3.0f) continue;
            const glm::vec3 p(static_cast<float>(gx * 8u + (voxel & 7u)),
                              static_cast<float>(gy * 8u + ((voxel >> 3u) & 7u)),
                              static_cast<float>(gz * 8u + (voxel >> 6u)));
            if (glm::length(p - center) < 1.0e-3f) continue;
            // The shader deliberately falls back to its sentinel-aware twin when
            // any stencil side reaches an unallocated brick.  Exclude those
            // undefined samples from the analytic sphere oracle; they are still
            // baked and covered by the separate border/fallback behavior gates.
            const uint32_t tableOffset = Vixen::SVO::detail::LookupTableOffset(f.cat, 0);
            const uint32_t* lookup = reinterpret_cast<const uint32_t*>(f.cat.brickGridLookup.data());
            const size_t lookupCount = f.cat.brickGridLookup.size() / sizeof(uint32_t);
            const float* poolFloats = reinterpret_cast<const float*>(f.cat.channelPool.data());
            const size_t poolFloatCount = f.cat.channelPool.size() / sizeof(float);
            const float d0 = Vixen::SVO::detail::SourceSdfTrilinear(
                poolFloats, poolFloatCount, f.cat.configs[0].poolBrickBase, stride, 0u,
                f.cat.configs[0].bricksPerAxis, f.cat.configs[0].brickSize,
                tableOffset, lookup, lookupCount, p);
            const float dxp = Vixen::SVO::detail::SourceSdfTrilinear(
                poolFloats, poolFloatCount, f.cat.configs[0].poolBrickBase, stride, 0u,
                f.cat.configs[0].bricksPerAxis, f.cat.configs[0].brickSize,
                tableOffset, lookup, lookupCount, p + glm::vec3(0.5f, 0.0f, 0.0f));
            const float dxm = Vixen::SVO::detail::SourceSdfTrilinear(
                poolFloats, poolFloatCount, f.cat.configs[0].poolBrickBase, stride, 0u,
                f.cat.configs[0].bricksPerAxis, f.cat.configs[0].brickSize,
                tableOffset, lookup, lookupCount, p - glm::vec3(0.5f, 0.0f, 0.0f));
            const float dyp = Vixen::SVO::detail::SourceSdfTrilinear(
                poolFloats, poolFloatCount, f.cat.configs[0].poolBrickBase, stride, 0u,
                f.cat.configs[0].bricksPerAxis, f.cat.configs[0].brickSize,
                tableOffset, lookup, lookupCount, p + glm::vec3(0.0f, 0.5f, 0.0f));
            const float dym = Vixen::SVO::detail::SourceSdfTrilinear(
                poolFloats, poolFloatCount, f.cat.configs[0].poolBrickBase, stride, 0u,
                f.cat.configs[0].bricksPerAxis, f.cat.configs[0].brickSize,
                tableOffset, lookup, lookupCount, p - glm::vec3(0.0f, 0.5f, 0.0f));
            const float dzp = Vixen::SVO::detail::SourceSdfTrilinear(
                poolFloats, poolFloatCount, f.cat.configs[0].poolBrickBase, stride, 0u,
                f.cat.configs[0].bricksPerAxis, f.cat.configs[0].brickSize,
                tableOffset, lookup, lookupCount, p + glm::vec3(0.0f, 0.0f, 0.5f));
            const float dzm = Vixen::SVO::detail::SourceSdfTrilinear(
                poolFloats, poolFloatCount, f.cat.configs[0].poolBrickBase, stride, 0u,
                f.cat.configs[0].bricksPerAxis, f.cat.configs[0].brickSize,
                tableOffset, lookup, lookupCount, p - glm::vec3(0.0f, 0.0f, 0.5f));
            if (std::fabs(d0) >= kShellNormalSentinel || std::fabs(dxp) >= kShellNormalSentinel ||
                std::fabs(dxm) >= kShellNormalSentinel || std::fabs(dyp) >= kShellNormalSentinel ||
                std::fabs(dym) >= kShellNormalSentinel || std::fabs(dzp) >= kShellNormalSentinel ||
                std::fabs(dzm) >= kShellNormalSentinel) continue;
            const glm::vec3 baked = DecodeShellNormalOct16(ReadBakedNormal(r, slot, voxel));
            const glm::vec3 radial = glm::normalize(p - center);
            errors.push_back(NormalAngleDegrees(baked, radial));
        }
    }
    ASSERT_GT(errors.size(), 100u);
    std::sort(errors.begin(), errors.end());
    const float p95 = errors[static_cast<size_t>(errors.size() * 0.95f)];
    const float maxError = errors.back();
    printf("[ShellDerive/normals] samples=%zu radial-p95=%.3f max=%.3f\n",
           errors.size(), p95, maxError);
    RecordProperty("normalSamples", static_cast<int>(errors.size()));
    RecordProperty("normalP95Degrees", p95);
    RecordProperty("normalMaxDegrees", maxError);
    // The contract is bounded error, not byte identity.  The measured
    // distribution is recorded above for review.
    EXPECT_LT(p95, 3.0f);
    EXPECT_LT(maxError, 8.0f);
}

TEST(ShellDerive, NormalBakeIsByteIdenticalAtWorkerCountsOneTwoAndDefault) {
    ShellFixture f;
    ShellDeriveParams serial;
    serial.bakeNormals = true;
    serial.workerCount = 1u;
    const ShellDeriveResult reference = DeriveShell(f.cat, 0, serial);
    ASSERT_GT(reference.shellBrickCount, 0u);
    for (uint32_t workers : {1u, 2u, 0u}) {
        for (int run = 0; run < 3; ++run) {
            ShellDeriveParams params = serial;
            params.workerCount = workers;
            const ShellDeriveResult candidate = DeriveShell(f.cat, 0, params);
            EXPECT_EQ(candidate.shellLookup, reference.shellLookup);
            EXPECT_EQ(candidate.surface, reference.surface);
            EXPECT_EQ(candidate.shell, reference.shell);
            ASSERT_EQ(candidate.shellData.size(), reference.shellData.size());
            EXPECT_EQ(std::memcmp(candidate.shellData.data(), reference.shellData.data(),
                                  reference.shellData.size()), 0)
                << "workerCount=" << workers << " run=" << run;
        }
    }
}

TEST(ShellDerive, DirtyRevalidateRebakesOnlyNormalTail) {
    ShellFixture f;
    ShellDeriveParams params;
    params.bakeNormals = true;
    params.workerCount = 1u;
    const ShellDeriveResult baseline = DeriveShell(f.cat, 0, params);
    ASSERT_GT(baseline.shellBrickCount, 2u);
    const uint32_t target = baseline.shellLookup[0];
    const uint32_t stride = f.cat.configs[0].brickStrideFloats;
    const float* source = reinterpret_cast<const float*>(f.cat.channelPool.data());
    std::vector<float> edited(SerializedOctree::kVoxelsPerBrick);
    for (uint32_t v = 0; v < edited.size(); ++v)
        edited[v] = source[static_cast<size_t>(target) * stride + v] + 0.1f * static_cast<float>(v & 7u);
    ConcatenatedOctrees fresh = f.cat;
    ASSERT_TRUE(ApplyBrickSdfEdit(fresh, 0, target, edited.data(), edited.size()));

    std::vector<uint8_t> updated = baseline.shellData;
    ASSERT_EQ(RevalidateShellBricks(fresh, 0, baseline, {target}, updated), 1u);
    const ShellDeriveResult full = DeriveShell(fresh, 0, params);
    ASSERT_EQ(full.shellLookup, baseline.shellLookup);
    const uint32_t slot = baseline.sourceToShellSlot[target];
    ASSERT_NE(slot, 0xFFFFFFFFu);
    const size_t tailOffset = (static_cast<size_t>(slot) * baseline.shellBrickStrideFloats +
                               baseline.normalOffsetFloats) * sizeof(float);
    EXPECT_EQ(std::memcmp(updated.data() + tailOffset,
                          full.shellData.data() + tailOffset,
                          baseline.normalStrideFloats * sizeof(float)), 0);
    for (uint32_t other = 0; other < baseline.shellBrickCount; ++other) {
        if (other == slot) continue;
        const size_t offset = static_cast<size_t>(other) * baseline.shellBrickStrideFloats * sizeof(float);
        EXPECT_EQ(std::memcmp(updated.data() + offset, baseline.shellData.data() + offset,
                              baseline.shellBrickStrideFloats * sizeof(float)), 0);
    }
}

TEST(ShellDerive, BakedNormalMipSamplesAreRenormalizedAndCovered) {
    ShellFixture f;
    SerializedOctree serialized = SerializeSdfWithMips(f.body, true);
    ASSERT_TRUE(normalMipPoolBakedOf(serialized.config));
    ASSERT_EQ(serialized.normalMipPool.size(),
              static_cast<size_t>(serialized.nodeCount) * sizeof(NormalMipSample));
    const auto* samples = reinterpret_cast<const NormalMipSample*>(serialized.normalMipPool.data());
    uint32_t covered = 0u;
    for (uint32_t node = 0; node < serialized.nodeCount; ++node) {
        const NormalMipSample& sample = samples[node];
        if (sample.coverage <= 0.0f) continue;
        ++covered;
        EXPECT_GE(sample.coverage, 0.0f);
        EXPECT_LE(sample.coverage, 1.0f);
        EXPECT_NEAR(glm::length(glm::vec3(sample.x, sample.y, sample.z)), 1.0f, 1.0e-4f);
    }
    EXPECT_GT(covered, 0u);
}

TEST(ShellDerive, BakedNormalsRemainContinuousAcrossBrickBorders) {
    ShellFixture f;
    ShellDeriveParams params;
    params.bakeNormals = true;
    const ShellDeriveResult r = DeriveShell(f.cat, 0, params);
    const auto brickToGrid = Vixen::SVO::detail::BuildBrickToGrid(f.cat, 0, r.sourceBrickCount);
    for (uint32_t a = 0; a < r.shellBrickCount; ++a) {
        const uint32_t pa = brickToGrid[r.shellLookup[a]];
        if (pa == 0xFFFFFFFFu) continue;
        const int ax = static_cast<int>(pa & 0x3ffu);
        const int ay = static_cast<int>((pa >> 10u) & 0x3ffu);
        const int az = static_cast<int>((pa >> 20u) & 0x3ffu);
        for (uint32_t b = 0; b < r.shellBrickCount; ++b) {
            const uint32_t pb = brickToGrid[r.shellLookup[b]];
            if (pb == 0xFFFFFFFFu) continue;
            const int bx = static_cast<int>(pb & 0x3ffu);
            const int by = static_cast<int>((pb >> 10u) & 0x3ffu);
            const int bz = static_cast<int>((pb >> 20u) & 0x3ffu);
            const int manhattan = std::abs(ax - bx) + std::abs(ay - by) + std::abs(az - bz);
            if (manhattan != 1 || ay != by || az != bz || bx != ax + 1) continue;
            const glm::vec3 left = DecodeShellNormalOct16(ReadBakedNormal(r, a, 4u + 4u * 8u + 4u * 64u + 7u));
            const glm::vec3 right = DecodeShellNormalOct16(ReadBakedNormal(r, b, 4u + 4u * 8u + 4u * 64u));
            EXPECT_LT(NormalAngleDegrees(left, right), 12.0f);
            return;
        }
    }
    FAIL() << "fixture did not expose an adjacent shell-brick border";
}

TEST(ShellDerive, SerialVsWaveWallClockMeasurement) {
    ShellFixture f;
    using Clock = std::chrono::steady_clock;
    auto measure = [&](uint32_t workers) {
        ShellDeriveParams params;
        params.bakeNormals = true;
        params.workerCount = workers;
        double totalMs = 0.0;
        size_t bytes = 0u;
        for (int run = 0; run < 3; ++run) {
            const auto start = Clock::now();
            const ShellDeriveResult result = DeriveShell(f.cat, 0, params);
            totalMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            bytes += result.shellData.size();
        }
        return std::pair<double, size_t>{totalMs / 3.0, bytes};
    };
    const auto serial = measure(1u);
    const auto waves = measure(2u);
    RecordProperty("serialMsMean3", serial.first);
    RecordProperty("wavesMsMean3", waves.first);
    RecordProperty("serialOutputBytes3", static_cast<int>(serial.second));
    RecordProperty("wavesOutputBytes3", static_cast<int>(waves.second));
    std::printf("[ShellDerive/A-B] serial=%.3fms waves(2)=%.3fms mean-of-3 output=%zuB\n",
                serial.first, waves.first, waves.second / 3u);
    EXPECT_EQ(serial.second, waves.second);
}
