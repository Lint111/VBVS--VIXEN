// test_mip_sample_bake.cpp — Sparse-Mip ESVO LOD Inc1, M1 Task 2.
//
// Bottom-up bake-time fill: bake a small multi-level octree, assert every
// interior level's MipPool samples are present and match Task 1's filter
// applied to the ACTUAL child data (exact value assertions, not just
// non-zero checks).
//
// Fixture: the shared 16^3 sphere fixture (test_soa_sdf_serialize.cpp's
// SdfFixture: r=6, n=16, band=2, brickDepth=3) gives bricksPerAxis=2 — the
// smallest non-trivial case: exactly ONE level of interior nodes (the root)
// whose children are all brick-level leaves. This keeps expected values
// hand-computable directly from the octree's own brickViews/channelPool,
// independent of BakeMipPool's internals.

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <cmath>
#include <vector>

#undef far
#undef near
#undef min
#undef max

#include "SdfBake.h"
#include "ShellOctreeGpu.h"
#include "SdfRecipes.h"
#include "MipBake.h"

using namespace Vixen::SVO;

namespace {

struct SdfFixture {
    SdfBodyOctree body;
    int n = 16;
    float r = 6.0f;
    glm::vec3 center{8.0f, 8.0f, 8.0f};

    SdfFixture() {
        RecipeParams rp{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, 2.0f);
        body = BuildSdfBodyOctree(baked, 3);
    }
};

// Independently reduce one brick's SDF channel down to a single value using
// the exact same rule BakeMipPool's leaf-handling must apply (min-magnitude
// by octant-group, then min-magnitude across groups) — computed here from
// the SerializedOctree's channelPool directly, NOT by calling into MipBake.h,
// so this is a genuine independent check rather than testing the code
// against itself.
MipSample IndependentReduceBrickSdf(const SerializedOctree& out, uint32_t brickIndex) {
    std::array<MipChildSample, 8> octantGroups{};
    std::array<float, 8> octantBest{};
    std::array<float, 8> octantBestAbs{};
    std::array<bool, 8> octantOccupied{};
    octantBestAbs.fill(-1.0f);
    octantOccupied.fill(false);

    // Occupancy from the material bricks array (materialId==0 -> empty),
    // matching MipBake.h's ReduceBrickToMipSample exactly — NOT "value==0",
    // since a genuinely on-surface SDF voxel can legitimately hold exactly 0.0.
    const uint32_t* brickMaterials =
        reinterpret_cast<const uint32_t*>(out.bricks.data()) +
        static_cast<size_t>(brickIndex) * SerializedOctree::kVoxelsPerBrick;

    for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; ++voxel) {
        const uint32_t x = voxel & 0x7u;
        const uint32_t y = (voxel >> 3) & 0x7u;
        const uint32_t z = (voxel >> 6) & 0x7u;
        const uint32_t octant = (x >> 2) | ((y >> 2) << 1) | ((z >> 2) << 2);

        if (brickMaterials[voxel] == 0u) continue;  // unoccupied

        const float value = out.readPoolVoxel(SEM_SDF, brickIndex, voxel, 0);
        const float a = std::fabs(value);
        if (octantBestAbs[octant] < 0.0f || a < octantBestAbs[octant]) {
            octantBestAbs[octant] = a;
            octantBest[octant] = value;
        }
        octantOccupied[octant] = true;
    }

    for (int o = 0; o < 8; ++o) {
        octantGroups[o] = MipChildSample{octantBest[o], octantOccupied[o]};
    }
    return FilterMipMinMagnitude(octantGroups);
}

}  // namespace

// ---------------------------------------------------------------------------
// Root-level mip sample: bricksPerAxis=2 fixture -> exactly one interior
// level (the root), whose children are all brick-level leaves.
// ---------------------------------------------------------------------------
TEST(MipSampleBake, RootLevelSdfMatchesIndependentBrickReduction) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    const Octree* oct = f.body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    ASSERT_NE(oct->root, nullptr);
    ASSERT_GT(out.nodeCount, 0u);

    MipPool pool = BakeMipPool(*oct, out);
    ASSERT_EQ(pool.nodeCount, out.nodeCount);
    ASSERT_EQ(pool.channelCount, out.channelCount);

    // Find the root: BFS order places it at index 0 (SVORebuild.cpp pushes
    // the root first: `finalDescriptors.push_back(tempDescriptors[rootOldIndex])`
    // with bfsQueue.push({rootOldIndex, 0})).
    const uint32_t rootIdx = 0;
    const ChildDescriptor& rootDesc = oct->root->childDescriptors[rootIdx];

    // Every child of the root at this fixture size is a leaf (brick), since
    // bricksPerAxis=2 collapses directly to a root-over-bricks tree.
    bool anyNonLeafChild = false;
    for (int octant = 0; octant < 8; ++octant) {
        if (rootDesc.hasChild(octant) && !rootDesc.isLeaf(octant)) anyNonLeafChild = true;
    }
    ASSERT_FALSE(anyNonLeafChild)
        << "fixture assumption violated: expected bricksPerAxis=2 (root's children all leaves)";

    // Independently reduce every child brick of the root, then apply the
    // SAME min-magnitude-across-children rule Task 1 defines, and compare
    // against BakeMipPool's actual output for the SDF channel.
    std::array<MipChildSample, 8> expectedChildren{};
    for (int octant = 0; octant < 8; ++octant) {
        if (!rootDesc.hasChild(octant)) continue;
        ASSERT_TRUE(rootDesc.isLeaf(octant));

        const uint64_t key = (static_cast<uint64_t>(rootIdx) << 3) | static_cast<uint64_t>(octant);
        auto it = oct->root->leafToBrickView.find(key);
        ASSERT_NE(it, oct->root->leafToBrickView.end())
            << "root child at octant " << octant << " must map to a brick view";
        const uint32_t brickIndex = it->second;

        MipSample reduced = IndependentReduceBrickSdf(out, brickIndex);
        expectedChildren[octant] = MipChildSample{reduced.value, reduced.coverage > 0.0f};
    }
    MipSample expectedRootSdf = FilterMipMinMagnitude(expectedChildren);

    // channels[] canonical order (SerializeSdf): SDF is channel 0.
    ASSERT_EQ(out.channels[0].semanticId, static_cast<uint32_t>(SEM_SDF));
    MipSample actualRootSdf = pool.Get(rootIdx, /*channelIdx=*/0);

    EXPECT_NEAR(actualRootSdf.value, expectedRootSdf.value, 1e-5f)
        << "root SDF mip sample must equal min-magnitude-across-children of the "
        << "independently-reduced brick values";
    EXPECT_NEAR(actualRootSdf.coverage, expectedRootSdf.coverage, 1e-5f);

    // Sanity: the sphere fixture guarantees at least one active (occupied) brick,
    // so the root's SDF mip sample must have non-zero coverage.
    EXPECT_GT(actualRootSdf.coverage, 0.0f)
        << "root must have non-zero coverage for a non-trivial sphere bake";
}

// The root's mip value must be a value that ACTUALLY occurred in one of its
// child bricks' voxels (min-magnitude never invents a value; it only ever
// selects among existing samples) — a structural property distinguishing it
// from a mean filter, checked directly against this fixture's real data.
TEST(MipSampleBake, RootLevelSdfIsAnActualChildValueNotAnAverage) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    const Octree* oct = f.body.octree->getOctree();
    MipPool pool = BakeMipPool(*oct, out);

    MipSample actualRootSdf = pool.Get(0, 0);
    ASSERT_GT(actualRootSdf.coverage, 0.0f);

    // Scan every occupied voxel across every brick for this channel; the
    // root's mip value must match one of them exactly (it was selected, not
    // synthesized by averaging). Occupancy from the material bricks array
    // (materialId==0 -> empty), matching MipBake.h's own occupancy signal.
    bool foundMatchingVoxel = false;
    for (uint32_t bi = 0; bi < out.brickCount && !foundMatchingVoxel; ++bi) {
        const uint32_t* mats = reinterpret_cast<const uint32_t*>(out.bricks.data()) +
                                static_cast<size_t>(bi) * SerializedOctree::kVoxelsPerBrick;
        for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; ++voxel) {
            if (mats[voxel] == 0u) continue;
            const float v = out.readPoolVoxel(SEM_SDF, bi, voxel, 0);
            if (std::fabs(v - actualRootSdf.value) < 1e-5f) {
                foundMatchingVoxel = true;
                break;
            }
        }
    }
    EXPECT_TRUE(foundMatchingVoxel)
        << "min-magnitude root sample (" << actualRootSdf.value
        << ") must exactly match some real voxel's SDF value, never an average";
}

// Color channel (mean-filtered) must land strictly between the min and max
// of its children's contributing values — the defining property of a mean,
// distinguishing it from the SDF channel's min-magnitude selection above.
TEST(MipSampleBake, RootLevelColorIsAMeanWithinChildRange) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    const Octree* oct = f.body.octree->getOctree();
    MipPool pool = BakeMipPool(*oct, out);

    // channels[] canonical order: SDF(0), Color(1), Roughness(2).
    ASSERT_EQ(out.channels[1].semanticId, static_cast<uint32_t>(SEM_COLOR));
    MipSample rootColor = pool.Get(0, /*channelIdx=*/1);
    ASSERT_GT(rootColor.coverage, 0.0f);

    float minV = 1e9f, maxV = -1e9f;
    bool any = false;
    for (uint32_t bi = 0; bi < out.brickCount; ++bi) {
        const uint32_t* mats = reinterpret_cast<const uint32_t*>(out.bricks.data()) +
                                static_cast<size_t>(bi) * SerializedOctree::kVoxelsPerBrick;
        for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; ++voxel) {
            if (mats[voxel] == 0u) continue;  // unoccupied
            const float v = out.readPoolVoxel(SEM_COLOR, bi, voxel, 0);
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
            any = true;
        }
    }
    ASSERT_TRUE(any);
    EXPECT_GE(rootColor.value, minV - 1e-4f);
    EXPECT_LE(rootColor.value, maxV + 1e-4f);
}

// ---------------------------------------------------------------------------
// Every node in the fixture must have a MipPool entry (interior levels'
// samples are "present", per Task 2's gate) for every live channel.
// ---------------------------------------------------------------------------
TEST(MipSampleBake, EveryNodeHasSamplesForEveryChannel) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    const Octree* oct = f.body.octree->getOctree();
    MipPool pool = BakeMipPool(*oct, out);

    ASSERT_EQ(pool.samples.size(),
              static_cast<size_t>(pool.nodeCount) * pool.channelCount);

    // Root (index 0) must have non-zero coverage on every channel for this
    // non-trivial sphere fixture.
    for (uint32_t ch = 0; ch < pool.channelCount; ++ch) {
        MipSample s = pool.Get(0, ch);
        EXPECT_GT(s.coverage, 0.0f) << "root channel " << ch << " must have coverage";
    }
}
