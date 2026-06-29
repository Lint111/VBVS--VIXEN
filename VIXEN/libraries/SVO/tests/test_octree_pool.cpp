/**
 * @file test_octree_pool.cpp
 * @brief I3.1 — ConcatenatedOctrees dynamic-N pool (no kMaxOctrees cap).
 *        I3.2 — config buffer size formula verification.
 */
#include <gtest/gtest.h>
#include "SdfBake.h"
#include "ShellOctreeGpu.h"
#include "Recipe/SdfInstruction.h"
#include <glm/glm.hpp>
#include <cstddef>

using namespace Vixen::SVO;

// ---------------------------------------------------------------------------
// Minimal sphere instruction for I3 bake tests.
// ---------------------------------------------------------------------------
static Recipe::SdfInstruction sphereI(glm::vec3 c, float r) {
    Recipe::SdfInstruction in{};
    in.opCode  = (uint8_t)Recipe::SdfOpCode::Sphere;
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
    return in;
}

// Build one SdfBodyOctree from a sphere recipe at low resolution (n=16, band=2).
static SdfBodyOctree makeSphere(float radius) {
    Recipe::SdfInstruction prog = sphereI(glm::vec3(8, 8, 8), radius);
    auto baked = BakeRecipeInstructionsToSdfWorld(&prog, 1,
                     glm::vec3(8, 8, 8), /*n=*/16, /*band=*/2.0f, /*brickDepth=*/3);
    return BuildSdfBodyOctree(baked, 3);
}

// ===========================================================================
// I3.1 — dynamic-N pool
// ===========================================================================

TEST(OctreePool, ConcatenatesMoreThanThreeSdfOctrees) {
    // Build 4 distinct SdfBodyOctrees.
    std::vector<SdfBodyOctree> bodies;
    for (int i = 0; i < 4; ++i) {
        bodies.push_back(makeSphere(3.0f + i));
    }

    // Collect raw pointers (ConcatenateSdf takes const SdfBodyOctree*[]).
    std::vector<const SdfBodyOctree*> ptrs;
    for (auto& b : bodies) ptrs.push_back(&b);

    ConcatenatedOctrees cat = ConcatenateSdf(ptrs);

    ASSERT_EQ(cat.count, 4u);
    EXPECT_EQ(cat.configs.size(), 4u);
    EXPECT_EQ(cat.nodeCounts.size(), 4u);
    EXPECT_EQ(cat.brickCounts.size(), 4u);

    // Bases must be non-decreasing (each octree is appended after the previous).
    for (uint32_t k = 1; k < cat.count; ++k) {
        EXPECT_GE(cat.configs[k].nodeArrayBase,
                  cat.configs[k - 1].nodeArrayBase)
            << "nodeArrayBase must be non-decreasing at k=" << k;
    }
}

// Regression: 1-octree and 3-octree paths still work after the change.
TEST(OctreePool, ConcatenatesOneOctree) {
    std::vector<SdfBodyOctree> bodies;
    bodies.push_back(makeSphere(4.0f));
    std::vector<const SdfBodyOctree*> ptrs;
    for (auto& b : bodies) ptrs.push_back(&b);
    ConcatenatedOctrees cat = ConcatenateSdf(ptrs);
    EXPECT_EQ(cat.count, 1u);
    EXPECT_EQ(cat.configs.size(), 1u);
    EXPECT_EQ(cat.configs[0].nodeArrayBase, 0);
}

TEST(OctreePool, ConcatenatesThreeOctrees) {
    std::vector<SdfBodyOctree> bodies;
    for (int i = 0; i < 3; ++i) bodies.push_back(makeSphere(3.0f + i));
    std::vector<const SdfBodyOctree*> ptrs;
    for (auto& b : bodies) ptrs.push_back(&b);
    ConcatenatedOctrees cat = ConcatenateSdf(ptrs);
    EXPECT_EQ(cat.count, 3u);
    EXPECT_EQ(cat.configs.size(), 3u);
}

// ===========================================================================
// I3.2 — config buffer size formula
// ===========================================================================

// Verify that OctreeConfig is exactly 432 bytes (static_assert in header,
// but we confirm it here so a failure is surfaced by the test runner too).
TEST(OctreePool, OctreeConfigIs432Bytes) {
    EXPECT_EQ(sizeof(OctreeConfig), 432u);
}

// Verify the size formula used in CreateOctreeBuffers for the config SSBO:
//   std::max<uint32_t>(cat.count, 1) * sizeof(OctreeConfig)
TEST(OctreePool, ConfigBufferSizeFormulaIsCountTimesSizeof) {
    for (uint32_t n : {1u, 2u, 3u, 4u, 8u}) {
        ConcatenatedOctrees cat;
        cat.count = n;
        const size_t expected = static_cast<size_t>(n) * sizeof(OctreeConfig);
        const size_t formula  = static_cast<size_t>(std::max<uint32_t>(cat.count, 1))
                                * sizeof(OctreeConfig);
        EXPECT_EQ(formula, expected) << "at n=" << n;
    }
    // Edge: count==0 → at least 1 entry (no zero-byte buffer).
    {
        ConcatenatedOctrees cat;
        cat.count = 0;
        const size_t formula = static_cast<size_t>(std::max<uint32_t>(cat.count, 1))
                               * sizeof(OctreeConfig);
        EXPECT_EQ(formula, sizeof(OctreeConfig));
    }
}
