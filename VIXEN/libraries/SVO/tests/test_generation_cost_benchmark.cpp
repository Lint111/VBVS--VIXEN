/**
 * @file test_generation_cost_benchmark.cpp
 * @brief Lazy-Procedural-Delta-Baseline Inc0 M3 (Task 6): generation-cost benchmark.
 *
 * Measures, for the standard n=64 / band=2.5 / depth=3 recipe bake, wall-time split
 * across the CPU bake pipeline stages that BakeRegistryToPool (RecipeBaker.h) chains
 * together:
 *   - pass-1 dense eval        (BakeSdfWorld's occupancy pass — n^3 evalRecipe calls)
 *   - pass-2 active-cell eval  (BakeSdfWorld's populate pass — evalRecipe + ECS
 *                                createVoxel, fused in one loop; see note below)
 *   - ECS entity churn         (same loop as pass-2 — see note below)
 *   - rebuild()                (LaineKarrasOctree::rebuild, includes its Phase-4 DXT step)
 *   - SerializeSdf             (ShellOctreeGpu.h — pack world into GPU-layout buffers)
 *   - mip bake                 (BakeAndAttachMipPool — MipBake.h)
 *   - concat                   (ConcatenatedOctrees buffer appends)
 *
 * HONESTY NOTE on pass-2/ECS-churn: BakeSdfWorld's populate pass calls
 * eval(p) and world->createVoxel(...) back-to-back inside the same per-voxel loop
 * (SdfBake.h ~line 145-169) — there is no seam in the production code to split
 * "active-cell eval" from "ECS entity churn" without changing production code
 * (out of scope for this measurement-only milestone, see project rules on root-cause
 * fixes vs. measurement harnesses). Per the M3 task instructions ("if a stage isn't
 * cleanly callable in isolation, measure it at the coarsest honest boundary and say so"),
 * this harness measures the fused pass-2 loop as one bucket and reports it under BOTH
 * labels with an explicit note, rather than fabricating a split.
 *
 * This is a standalone gtest that calls the CPU bake/serialize pipeline stages directly
 * — no GPU dispatch, no raymarch pipeline, no Vulkan device. Pattern mirrors
 * RenderGraph/tests/Nodes/test_bandwidth_ab_measurement.cpp's role as an A/B measurement
 * gate, adapted for a pure-CPU generation-cost breakdown instead of a GPU upload A/B.
 */

#include <gtest/gtest.h>

#include "SdfBake.h"
#include "MipBake.h"
#include "ShellOctreeGpu.h"
#include "Recipe/SdfInstruction.h"
#include "Recipe/SdfRecipeEval.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace Vixen::SVO;
using Recipe::SdfInstruction;
using Recipe::SdfOpCode;

namespace {

// Standard bake params per the M3 task spec.
constexpr int         kN          = 64;
constexpr float       kBand       = 2.5f;
constexpr int         kDepth      = 3;
constexpr glm::vec3   kCenter     = glm::vec3(32.0f, 32.0f, 32.0f);
constexpr int          kRuns      = 3;   // WSL timings are jittery — report medians.

// Sparse-Mip ESVO LOD Inc1 M1's measured transfer-side reference figures (bytes),
// quoted verbatim from that increment's own measurement — this is the "transfer" side
// of the min(generation, transfer) comparison this benchmark's conclusion draws.
constexpr uint64_t kTransferRefTotalBytes  = 8'362'320ull;
constexpr uint64_t kTransferRefUpperBytes  = 26'759'424ull;

SdfInstruction sphereInstr(glm::vec3 c, float r) {
    SdfInstruction in{};
    in.opCode  = static_cast<uint8_t>(SdfOpCode::Sphere);
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
    return in;
}

double MsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
}

double Median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// One run's per-stage timings (ms), reproducing BakeRegistryToPool's stage sequence
// but with a timer around each stage instead of one call straight through.
struct StageTimings {
    double denseEvalMs   = 0.0;  // pass-1 occupancy pass
    double activeEvalMs  = 0.0;  // pass-2 populate pass (eval + ECS createVoxel, fused)
    double rebuildMs     = 0.0;  // LaineKarrasOctree::rebuild (incl. Phase-4 DXT)
    double serializeMs   = 0.0;  // SerializeSdf
    double mipBakeMs     = 0.0;  // BakeAndAttachMipPool
    double concatMs      = 0.0;  // ConcatenatedOctrees buffer appends

    uint32_t nodeCount   = 0;
    uint32_t brickCount  = 0;
    uint64_t poolBytes   = 0;  // nodes+bricks+channelPool+mipPool, this octree only
};

// Reproduces BakeSdfWorld's two passes (SdfBake.h) with independent timers around each,
// instead of calling the single fused BakeSdfWorld — same eval function, same occupancy/
// dilation/populate logic, just with a stopwatch seam BakeSdfWorld doesn't expose. Kept
// byte-for-byte algorithmically identical to SdfBake.h so the measured cost is real, not
// an approximation.
SdfBakeResult TimedBakeSdfWorld(uint32_t recipeId_unused,
                                 const Recipe::SdfInstruction* prog, uint32_t progCount,
                                 const glm::vec3& center, int n, float bandVoxels,
                                 int brickDepth, double& outDenseMs, double& outActiveMs) {
    (void)recipeId_unused;
    auto eval = [&](const glm::vec3& p) {
        return Recipe::evalRecipe(prog, progCount, p - center);
    };

    SdfBakeResult r;
    r.n      = n;
    r.center = center;
    r.registry = std::make_unique<AttributeRegistry>();
    r.registry->registerKey("density", Vixen::VoxelData::AttributeType::Float, 0.0f);
    r.registry->addAttribute("color", Vixen::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));
    r.registry->addAttribute("roughness", Vixen::VoxelData::AttributeType::Float, 0.5f);
    r.world = std::make_unique<Vixen::GaiaVoxel::GaiaVoxelWorld>();

    const int brickSide     = 1 << brickDepth;
    const int bricksPerAxis = (n + brickSide - 1) / brickSide;
    auto brickIndex = [&](int bx, int by, int bz) {
        return (bz * bricksPerAxis + by) * bricksPerAxis + bx;
    };

    const size_t numBricks = static_cast<size_t>(bricksPerAxis) * bricksPerAxis * bricksPerAxis;
    std::vector<uint8_t> occupiedBrick(numBricks, 0u);

    // --- pass-1 dense eval (occupancy marking) ---
    const auto t0 = std::chrono::steady_clock::now();
    for (int z = 0; z < n; ++z)
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float sd = eval(
                glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
            if (sd <= bandVoxels)
                occupiedBrick[brickIndex(x / brickSide, y / brickSide, z / brickSide)] = 1u;
        }
    outDenseMs = MsSince(t0);

    // Dilation is cheap bookkeeping over bricks (not voxels) — folded into pass-2's
    // timer window since it's a direct prerequisite of the populate pass and BakeSdfWorld
    // itself doesn't separate it either.
    std::vector<uint8_t> activeBrick(numBricks, 0u);

    const auto t1 = std::chrono::steady_clock::now();
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

    // --- pass-2 active-cell eval + ECS entity churn (fused, see file-header note) ---
    for (int z = 0; z < n; ++z)
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            if (!activeBrick[brickIndex(x / brickSide, y / brickSide, z / brickSide)])
                continue;
            const glm::vec3 p(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            const float sd = eval(p);
            const glm::vec3 col = 0.5f + 0.5f * glm::cos(
                glm::vec3(p.x, p.y, p.z) * 0.12f + glm::vec3(0.0f, 2.094f, 4.188f));
            const float rough = glm::clamp(0.2f + 0.6f * glm::fract(p.y * 0.0625f), 0.0f, 1.0f);
            const Vixen::GaiaVoxel::ComponentQueryRequest comps[] = {
                Vixen::GaiaVoxel::Density{sd},
                Vixen::GaiaVoxel::Color{col},
                Vixen::GaiaVoxel::Roughness{rough},
                Vixen::GaiaVoxel::Material{1u},
            };
            r.world->createVoxel(Vixen::GaiaVoxel::VoxelCreationRequest{p, comps});
        }
    outActiveMs = MsSince(t1);  // includes dilation bookkeeping + fused eval/ECS-churn loop

    return r;
}

// One full run of the standard bake, stage-timed.
StageTimings RunOnce() {
    StageTimings st;

    std::vector<SdfInstruction> prog = { sphereInstr(glm::vec3(0, 0, 0), 26.0f) };

    SdfBakeResult baked = TimedBakeSdfWorld(
        0u, prog.data(), static_cast<uint32_t>(prog.size()),
        kCenter, kN, kBand, kDepth, st.denseEvalMs, st.activeEvalMs);

    // --- rebuild() (incl. Phase-4 DXT step) ---
    const auto tRebuild0 = std::chrono::steady_clock::now();
    SdfBodyOctree body = BuildSdfBodyOctree(baked, kDepth);
    st.rebuildMs = MsSince(tRebuild0);
    // BuildSdfBodyOctree wraps rebuild() plus trivial pointer moves/ctor work (see
    // SdfBake.h) — the octree ctor + rebuild() call dominates; measuring the whole
    // wrapper is the coarsest honest boundary without duplicating BuildSdfBodyOctree's
    // internals (which would risk drifting from the real rebuild() call site).

    // --- SerializeSdf ---
    const auto tSer0 = std::chrono::steady_clock::now();
    SerializedOctree ser = SerializeSdf(body);
    st.serializeMs = MsSince(tSer0);

    // --- mip bake (BakeAndAttachMipPool) ---
    const Octree* oct = body.octree->getOctree();
    if (oct == nullptr) {
        throw std::runtime_error(
            "RunOnce: baked octree has no LaineKarrasOctree — mip bake would be skipped");
    }
    const auto tMip0 = std::chrono::steady_clock::now();
    BakeAndAttachMipPool(*oct, ser);
    st.mipBakeMs = MsSince(tMip0);

    // --- concat (single-octree ConcatenatedOctrees buffer append, mirroring
    // ConcatenateSdfWithMips's per-octree append block) ---
    const auto tCat0 = std::chrono::steady_clock::now();
    ConcatenatedOctrees cat;
    cat.count = 1;
    cat.configs.push_back(ser.config);
    cat.nodeCounts.push_back(ser.nodeCount);
    cat.brickCounts.push_back(ser.brickCount);
    cat.tierRefCounts.push_back(static_cast<uint32_t>(ser.tierRefs.size()));
    cat.nodes.insert(cat.nodes.end(), ser.nodes.begin(), ser.nodes.end());
    cat.bricks.insert(cat.bricks.end(), ser.bricks.begin(), ser.bricks.end());
    cat.channelPool.insert(cat.channelPool.end(), ser.channelPool.begin(), ser.channelPool.end());
    cat.brickGridLookup.insert(cat.brickGridLookup.end(),
                                ser.brickGridLookup.begin(), ser.brickGridLookup.end());
    cat.mipPool.insert(cat.mipPool.end(), ser.mipPool.begin(), ser.mipPool.end());
    cat.tierRefTable.insert(cat.tierRefTable.end(), ser.tierRefs.begin(), ser.tierRefs.end());
    cat.materials = ser.materials;
    st.concatMs = MsSince(tCat0);

    st.nodeCount  = ser.nodeCount;
    st.brickCount = ser.brickCount;
    st.poolBytes  = static_cast<uint64_t>(ser.nodes.size()) +
                    static_cast<uint64_t>(ser.bricks.size()) +
                    static_cast<uint64_t>(ser.channelPool.size()) +
                    static_cast<uint64_t>(ser.mipPool.size());

    return st;
}

}  // namespace

// ---------------------------------------------------------------------------
// THE MEASUREMENT
// ---------------------------------------------------------------------------
TEST(GenerationCostBenchmark, StandardBakeStageBreakdown) {
    std::vector<StageTimings> runs;
    runs.reserve(kRuns);
    for (int i = 0; i < kRuns; ++i) {
        StageTimings st = RunOnce();
        // Sanity: no stage should measure as implausibly zero — a zero here is an
        // instrumentation bug, not a real "free" stage, for an n=64 sphere bake.
        EXPECT_GT(st.denseEvalMs, 0.0)  << "run " << i << ": pass-1 dense eval measured zero";
        EXPECT_GT(st.activeEvalMs, 0.0) << "run " << i << ": pass-2 active-cell eval measured zero";
        EXPECT_GT(st.rebuildMs, 0.0)    << "run " << i << ": rebuild() measured zero";
        EXPECT_GT(st.serializeMs, 0.0)  << "run " << i << ": SerializeSdf measured zero";
        EXPECT_GT(st.mipBakeMs, 0.0)    << "run " << i << ": mip bake measured zero";
        EXPECT_GT(st.nodeCount, 0u)     << "run " << i << ": zero-node octree — bake produced nothing";
        EXPECT_GT(st.poolBytes, 0u)     << "run " << i << ": zero pool bytes — bake produced nothing";
        runs.push_back(st);
    }

    std::vector<double> denseEvalMs, activeEvalMs, rebuildMs, serializeMs, mipBakeMs, concatMs, totalMs;
    for (const auto& r : runs) {
        denseEvalMs.push_back(r.denseEvalMs);
        activeEvalMs.push_back(r.activeEvalMs);
        rebuildMs.push_back(r.rebuildMs);
        serializeMs.push_back(r.serializeMs);
        mipBakeMs.push_back(r.mipBakeMs);
        concatMs.push_back(r.concatMs);
        totalMs.push_back(r.denseEvalMs + r.activeEvalMs + r.rebuildMs +
                           r.serializeMs + r.mipBakeMs + r.concatMs);
    }

    const double medDense  = Median(denseEvalMs);
    const double medActive = Median(activeEvalMs);
    const double medRebuild= Median(rebuildMs);
    const double medSer    = Median(serializeMs);
    const double medMip    = Median(mipBakeMs);
    const double medConcat = Median(concatMs);
    const double medTotal  = Median(totalMs);

    // Derived per-brick / per-region numbers use the LAST run's node/brick/byte counts
    // (all runs bake the identical deterministic recipe, so counts are constant across
    // runs — only wall-time jitters).
    const StageTimings& counts = runs.back();
    const double msPerBrick   = counts.brickCount > 0
        ? medTotal / static_cast<double>(counts.brickCount) : 0.0;
    // "16^3-region" = 8 bricks/axis of 8^3-voxel bricks worth of generation cost,
    // i.e. msPerBrick * (16/8)^3 = msPerBrick * 8, scaled by this bake's actual brick count.
    const double msPer16CubedRegion = msPerBrick * 8.0;

    const double genBytesPerMs = medTotal > 0.0
        ? static_cast<double>(counts.poolBytes) / medTotal : 0.0;

    std::printf(
        "\n"
        "=== Lazy-Procedural-Delta-Baseline Inc0 M3 -- generation-cost benchmark ===\n"
        "  Recipe: single sphere, n=%d, band=%.1f, depth=%d (%d runs, medians reported)\n"
        "  Environment: WSL2 (Linux), CPU-only measurement, vixen-wsl preset build\n"
        "\n"
        "  Stage                          | median ms  | run1 ms   | run2 ms   | run3 ms\n"
        "  --------------------------------|------------|-----------|-----------|----------\n"
        "  pass-1 dense eval (n^3)         | %10.4f | %9.4f | %9.4f | %9.4f\n"
        "  pass-2 active-cell eval+ECS*    | %10.4f | %9.4f | %9.4f | %9.4f\n"
        "  ECS entity churn* (see note)    | %10.4f | %9.4f | %9.4f | %9.4f\n"
        "  rebuild() (incl. Phase-4 DXT)   | %10.4f | %9.4f | %9.4f | %9.4f\n"
        "  SerializeSdf                    | %10.4f | %9.4f | %9.4f | %9.4f\n"
        "  mip bake                        | %10.4f | %9.4f | %9.4f | %9.4f\n"
        "  concat                          | %10.4f | %9.4f | %9.4f | %9.4f\n"
        "  --------------------------------|------------|-----------|-----------|----------\n"
        "  TOTAL (sum of stages)           | %10.4f | %9.4f | %9.4f | %9.4f\n"
        "\n"
        "  * pass-2 eval and ECS churn are FUSED in production code (SdfBake.h's populate\n"
        "    loop calls evalRecipe then world->createVoxel back-to-back per active voxel) --\n"
        "    no seam exists to split them without changing production code, so both rows\n"
        "    report the SAME fused measurement per the task's coarsest-honest-boundary rule.\n"
        "\n"
        "  Bake output: nodeCount=%u, brickCount=%u, poolBytes(nodes+bricks+channelPool+mipPool)=%llu\n"
        "\n"
        "  DERIVED per-brick / per-region generation cost:\n"
        "    ms per brick (8^3 voxels)        = %.6f ms\n"
        "    ms per 16^3-region-equivalent     = %.6f ms  (8 bricks' worth)\n"
        "    generation throughput             = %.1f bytes/ms\n"
        "\n"
        "  TRANSFER-SIDE REFERENCE (Sparse-Mip ESVO LOD Inc1 M1, measured, quoted verbatim):\n"
        "    totalBytesUploaded (per-region, lower bound) = %llu bytes\n"
        "    totalBytesUploaded (per-region, upper bound) = %llu bytes\n"
        "===========================================================================\n",
        kN, kBand, kDepth, kRuns,
        medDense,  denseEvalMs[0],  denseEvalMs.size()>1?denseEvalMs[1]:0.0,  denseEvalMs.size()>2?denseEvalMs[2]:0.0,
        medActive, activeEvalMs[0], activeEvalMs.size()>1?activeEvalMs[1]:0.0, activeEvalMs.size()>2?activeEvalMs[2]:0.0,
        medActive, activeEvalMs[0], activeEvalMs.size()>1?activeEvalMs[1]:0.0, activeEvalMs.size()>2?activeEvalMs[2]:0.0,
        medRebuild, rebuildMs[0], rebuildMs.size()>1?rebuildMs[1]:0.0, rebuildMs.size()>2?rebuildMs[2]:0.0,
        medSer, serializeMs[0], serializeMs.size()>1?serializeMs[1]:0.0, serializeMs.size()>2?serializeMs[2]:0.0,
        medMip, mipBakeMs[0], mipBakeMs.size()>1?mipBakeMs[1]:0.0, mipBakeMs.size()>2?mipBakeMs[2]:0.0,
        medConcat, concatMs[0], concatMs.size()>1?concatMs[1]:0.0, concatMs.size()>2?concatMs[2]:0.0,
        medTotal, totalMs[0], totalMs.size()>1?totalMs[1]:0.0, totalMs.size()>2?totalMs[2]:0.0,
        counts.nodeCount, counts.brickCount, static_cast<unsigned long long>(counts.poolBytes),
        msPerBrick, msPer16CubedRegion, genBytesPerMs,
        static_cast<unsigned long long>(kTransferRefTotalBytes),
        static_cast<unsigned long long>(kTransferRefUpperBytes));

    // --- min(generation, transfer) conclusion: compare this bake's total generation
    // wall-time against a rough transfer-time estimate for the SAME byte count, using a
    // conservative PCIe-class bandwidth floor (1 GB/s = 1e6 bytes/ms) as a stand-in for
    // "moving these bytes over a bus/network" -- the design's premise is generation must
    // be cheaper than transfer for a lazy region to be worth generating instead of
    // fetching. This is a coarse floor, not a measured transfer number (no transfer
    // harness exists in this CPU-only benchmark) -- printed for context, not asserted on.
    constexpr double kAssumedTransferBytesPerMs = 1.0e6;  // 1 GB/s floor
    const double estTransferMsForPoolBytes =
        static_cast<double>(counts.poolBytes) / kAssumedTransferBytesPerMs;
    std::printf(
        "  min(generation, transfer) CONTEXT (not asserted, informational only):\n"
        "    this bake's poolBytes=%llu generated in %.4f ms (median total)\n"
        "    same byte count over a 1 GB/s floor would transfer in ~%.6f ms\n"
        "    => at this recipe's small scale, generation is %s than a 1GB/s transfer of\n"
        "       the same bytes; the design's real payoff is for LAZY/UNGENERATED regions\n"
        "       where the alternative is transfer-then-store, not gen-vs-transfer of an\n"
        "       already-baked octree -- see this report's prose conclusion.\n"
        "===========================================================================\n",
        static_cast<unsigned long long>(counts.poolBytes), medTotal, estTransferMsForPoolBytes,
        medTotal > estTransferMsForPoolBytes ? "SLOWER" : "FASTER");

    SUCCEED();
}
