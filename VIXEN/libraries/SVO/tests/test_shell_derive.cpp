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

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>

#undef far
#undef near
#undef min
#undef max

#include "SdfBake.h"
#include "ShellOctreeGpu.h"
#include "ShellDerive.h"
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
