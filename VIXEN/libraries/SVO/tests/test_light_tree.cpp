// test_light_tree.cpp — Sampled Lighting Inc3 M3: mip-cut light-tree.
//
// Covers Task 3's light-tree deliverables:
//   - BuildLightTreeCut produces a bounded, non-empty cut for an emissive scene.
//   - A non-emissive scene (no EmissionIntensity baked) produces an EMPTY cut —
//     the byte-identity escape hatch this whole increment relies on.
//   - The cut's aggregate radiance/power approximates the brute-force leaf-sum
//     within tolerance — the DISTINCT correctness check the plan calls out
//     (separate from ReSTIR's future unbiasedness, which is M4+).
//   - A tighter powerThreshold yields a cut that is no coarser (more nodes,
//     not fewer) than a looser one — the threshold's monotonic-refinement
//     property, a structural sanity check on the cut logic itself.
//
// GATE SCENE (Task 3's "author a gate scene with >=10^3 emissive voxels"):
// BakeEmissiveGateScene below IS that gate scene — a named, reusable CPU
// fixture (not a throwaway inline lambda) baking a sphere with a spatially-
// varying emissive intensity over its WHOLE occupied volume (not just the
// surface shell), giving the light-tree real multi-level structure to cut
// through. EmissiveGateSceneHasAtLeastAThousandEmissiveVoxels below asserts
// its scale directly; every other test in this file reuses the same fixture
// so the whole file's coverage — brute-force reference, cut-vs-brute-force
// tolerance, threshold monotonicity, well-formedness — runs against ONE
// scene, not scattered ad-hoc bakes. This is a CPU fixture, not a live
// VIXEN.exe demo scene (no VIXEN_xxx_DEMO env var/BuildRenderGraph.cpp
// wiring): M3 has no shading consumer for ReservoirConfig/the light-tree yet
// (M4/M5 do), so a CPU-testable gate scene is the correct-scope deliverable
// this milestone — wiring a live render-path demo now would touch
// BodyOctreeSceneNode's shared kind-baking loop for zero rendering payoff.

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <cmath>
#include <vector>

// MSVC's <windows.h> defines min/max as macros; same guard convention as
// every sibling SVO test file (see SdfRecipes.h / SdfRecipeEval.h for the
// header-side fix — this is defense-in-depth at the test-file level).
#undef far
#undef near
#undef min
#undef max

#include "SdfBake.h"
#include "ShellOctreeGpu.h"
#include "SdfRecipes.h"
#include "MipBake.h"
#include "LightTree.h"

using namespace Vixen::SVO;

// Bakes a sphere with a spatially-varying emissive intensity over its whole
// occupied volume (not just the surface) — gives the light-tree real
// structure to cut through, unlike a single-point emitter. THE gate scene
// (Task 3's ">=10^3 emissive voxels" deliverable — see this file's header).
SdfBodyOctree BakeEmissiveGateScene(int n, float r, const glm::vec3& center, float bandVoxels = 2.0f) {
    RecipeParams rp{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    SdfBakeResult baked = BakeRecipeToSdfWorldWithEmission(
        RECIPE_SPHERE, center, rp, n, bandVoxels,
        [](const glm::vec3& p) { return 1.0f + 0.1f * (p.x + p.y + p.z); });
    return BuildSdfBodyOctree(baked, 3);
}

// ---------------------------------------------------------------------------
// The gate scene itself must satisfy Task 3's ">=10^3 emissive voxels" bar,
// checked directly and independently of any light-tree/cut logic — a scale
// assertion on the SCENE, not on the algorithm under test.
// ---------------------------------------------------------------------------
TEST(LightTree, EmissiveGateSceneHasAtLeastAThousandEmissiveVoxels) {
    SdfBodyOctree body = BakeEmissiveGateScene(/*n=*/32, /*r=*/10.0f, glm::vec3(16.0f));
    SerializedOctree out = SerializeSdf(body);

    uint64_t emissiveVoxelCount = 0;
    for (uint32_t bi = 0; bi < out.brickCount; ++bi) {
        const uint32_t* mats = reinterpret_cast<const uint32_t*>(out.bricks.data()) +
                                static_cast<size_t>(bi) * SerializedOctree::kVoxelsPerBrick;
        for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; ++voxel) {
            if (mats[voxel] == 0u) continue;
            if (out.readPoolVoxel(SEM_EMISSION, bi, voxel, 0) > 0.0f) ++emissiveVoxelCount;
        }
    }
    EXPECT_GE(emissiveVoxelCount, 1000u)
        << "the gate scene (Task 3's '>=10^3 emissive voxels' deliverable) must have "
        << "at least 1000 emissive voxels; found " << emissiveVoxelCount;
}

// ---------------------------------------------------------------------------
// A scene with no emission baked (default NoEmission) must produce an EMPTY
// cut — the escape hatch: reservoirEnabled=0 / emissive-absent must reproduce
// the pre-emissive M1/M2 output, and an empty light-tree is the light-source
// side of that guarantee.
// ---------------------------------------------------------------------------
TEST(LightTree, NonEmissiveSceneProducesEmptyCut) {
    RecipeParams rp{6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, glm::vec3(8.0f), rp, 16, 2.0f);
    SdfBodyOctree body = BuildSdfBodyOctree(baked, 3);

    SerializedOctree out = SerializeSdf(body);
    const Octree* oct = body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    BakeAndAttachMipPool(*oct, out);
    MipPool pool = BakeMipPool(*oct, out);

    std::vector<LightTreeNode> cut = BuildLightTreeCut(*oct, out, pool, /*gridN=*/16);
    EXPECT_TRUE(cut.empty()) << "a scene with no baked emission must produce an empty light-tree cut";
}

// ---------------------------------------------------------------------------
// A genuinely emissive scene must produce a NON-EMPTY, BOUNDED cut — the
// "million glowing voxels -> handful of nodes" property. Bounded means far
// fewer cut nodes than raw emissive voxels (this fixture has thousands of
// emissive voxels across its active bricks).
// ---------------------------------------------------------------------------
TEST(LightTree, EmissiveSceneProducesBoundedNonEmptyCut) {
    // n=32/r=10 (not the larger n=64/r=20 originally used here): a 64^3 bake
    // with a large-radius sphere was found to hang/take >120s in this Debug-
    // unoptimized GaiaVoxelWorld ECS path (matches the pre-existing
    // SoaSdfSerialize.InteriorBricksAllocatedNotDropped hang at the same
    // n=64/large-radius scale — a known perf class, not a light-tree bug).
    // n=32/r=10 already gives >1000 emissive voxels (the precondition below),
    // enough to make "bounded cut" meaningful, and matches every OTHER
    // fixture in this file (all proven fast at this scale).
    SdfBodyOctree body = BakeEmissiveGateScene(/*n=*/32, /*r=*/10.0f, glm::vec3(16.0f));
    SerializedOctree out = SerializeSdf(body);
    const Octree* oct = body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    BakeAndAttachMipPool(*oct, out);
    MipPool pool = BakeMipPool(*oct, out);

    LightTreeCutParams params;
    params.powerThreshold = 50.0f;  // coarse cut — a handful of nodes, not per-voxel
    std::vector<LightTreeNode> cut = BuildLightTreeCut(*oct, out, pool, /*gridN=*/32, params);

    ASSERT_FALSE(cut.empty()) << "an emissive scene must produce at least one cut node";

    // Count raw emissive (occupied, EmissionIntensity>0) voxels for the "bounded" comparison.
    uint64_t emissiveVoxelCount = 0;
    for (uint32_t bi = 0; bi < out.brickCount; ++bi) {
        const uint32_t* mats = reinterpret_cast<const uint32_t*>(out.bricks.data()) +
                                static_cast<size_t>(bi) * SerializedOctree::kVoxelsPerBrick;
        for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; ++voxel) {
            if (mats[voxel] == 0u) continue;
            if (out.readPoolVoxel(SEM_EMISSION, bi, voxel, 0) > 0.0f) ++emissiveVoxelCount;
        }
    }
    ASSERT_GT(emissiveVoxelCount, 1000u)
        << "test precondition: fixture must have >1000 raw emissive voxels "
        << "to make 'bounded cut' a meaningful assertion (gate scene scale)";
    EXPECT_LT(cut.size(), emissiveVoxelCount)
        << "the cut (" << cut.size() << " nodes) must be far smaller than the "
        << "raw emissive voxel count (" << emissiveVoxelCount << ") — the "
        << "'million glowing voxels -> handful of nodes' property";
}

// ---------------------------------------------------------------------------
// Cut-vs-brute-force radiance validation (Task 3's distinct correctness
// check): the cut's aggregate power must approximate the true brute-force
// leaf-sum within tolerance. A COARSE cut (few, large nodes) is expected to
// carry MORE approximation error than a FINE cut (many, small nodes) — so
// this test uses a fine-ish threshold and a generous-but-bounded tolerance,
// not exact equality (the cut is an approximation by design).
// ---------------------------------------------------------------------------
TEST(LightTree, CutAggregatePowerApproximatesBruteForceLeafSum) {
    SdfBodyOctree body = BakeEmissiveGateScene(/*n=*/32, /*r=*/10.0f, glm::vec3(16.0f));
    SerializedOctree out = SerializeSdf(body);
    const Octree* oct = body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    BakeAndAttachMipPool(*oct, out);
    MipPool pool = BakeMipPool(*oct, out);

    const double bruteForce = BruteForceTotalEmissivePower(out);
    ASSERT_GT(bruteForce, 0.0) << "test precondition: fixture must have real emissive power";

    // A fine cut (low threshold) — every non-trivial node with any power
    // becomes its own cut node, so the approximation should be tight.
    LightTreeCutParams fineParams;
    fineParams.powerThreshold = 0.001f;
    std::vector<LightTreeNode> fineCut = BuildLightTreeCut(*oct, out, pool, /*gridN=*/32, fineParams);
    ASSERT_FALSE(fineCut.empty());
    const double fineCutPower = LightTreeCutTotalPower(fineCut);

    const double relError = std::fabs(fineCutPower - bruteForce) / bruteForce;
    EXPECT_LT(relError, 0.5)
        << "fine cut aggregate power (" << fineCutPower << ") must approximate "
        << "the brute-force leaf-sum (" << bruteForce << ") within tolerance "
        << "(relative error " << relError << ")";
}

// ---------------------------------------------------------------------------
// Monotonic refinement: a LOWER powerThreshold must never produce FEWER cut
// nodes than a higher one (looser thresholds cut earlier/coarser; tighter
// thresholds only ever descend further or equally far) — a structural
// invariant of the cut algorithm, independent of the tolerance check above.
// ---------------------------------------------------------------------------
TEST(LightTree, TighterThresholdNeverProducesFewerCutNodes) {
    SdfBodyOctree body = BakeEmissiveGateScene(/*n=*/32, /*r=*/10.0f, glm::vec3(16.0f));
    SerializedOctree out = SerializeSdf(body);
    const Octree* oct = body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    BakeAndAttachMipPool(*oct, out);
    MipPool pool = BakeMipPool(*oct, out);

    LightTreeCutParams coarse;
    coarse.powerThreshold = 100.0f;
    LightTreeCutParams fine;
    fine.powerThreshold = 0.001f;

    std::vector<LightTreeNode> coarseCut = BuildLightTreeCut(*oct, out, pool, 32, coarse);
    std::vector<LightTreeNode> fineCut   = BuildLightTreeCut(*oct, out, pool, 32, fine);

    EXPECT_GE(fineCut.size(), coarseCut.size())
        << "a tighter powerThreshold (" << fine.powerThreshold << ", "
        << fineCut.size() << " nodes) must not produce fewer cut nodes than a "
        << "looser one (" << coarse.powerThreshold << ", " << coarseCut.size() << " nodes)";
}

// ---------------------------------------------------------------------------
// Every returned cut node must carry positive intensity, positive coverage,
// and a positive extent — a structurally well-formed emitter record (no
// degenerate/zero-power nodes leaking through, which BuildLightTreeCut's own
// pruning is supposed to prevent).
// ---------------------------------------------------------------------------
TEST(LightTree, CutNodesAreWellFormed) {
    SdfBodyOctree body = BakeEmissiveGateScene(/*n=*/32, /*r=*/10.0f, glm::vec3(16.0f));
    SerializedOctree out = SerializeSdf(body);
    const Octree* oct = body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    BakeAndAttachMipPool(*oct, out);
    MipPool pool = BakeMipPool(*oct, out);

    std::vector<LightTreeNode> cut = BuildLightTreeCut(*oct, out, pool, 32);
    ASSERT_FALSE(cut.empty());
    for (const LightTreeNode& n : cut) {
        EXPECT_GT(n.intensity, 0.0f);
        EXPECT_GT(n.coverage, 0.0f);
        EXPECT_LE(n.coverage, 1.0f);
        EXPECT_GT(n.worldExtent, 0.0f);
    }
}
