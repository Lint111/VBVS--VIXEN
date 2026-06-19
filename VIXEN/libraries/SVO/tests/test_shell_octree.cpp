#include "ShellVoxelizer.h"
#include "ShellOctree.h"
#include <gtest/gtest.h>
#include <algorithm>

TEST(ShellVoxelizer, IsHollowSurfaceShell) {
    int d = 5;                          // 32^3 lattice
    auto cells = Vixen::SVO::ShellVoxels(d);   // vector<glm::ivec3> in [0, 2^d)
    ASSERT_FALSE(cells.empty());
    double N = double(1 << d);
    for (auto c : cells) {              // every emitted cell straddles the sphere surface
        glm::dvec3 p = (glm::dvec3(c) + 0.5) / N * 2.0 - 1.0;   // cell center in [-1,1]
        double r = glm::length(p);
        EXPECT_NEAR(r, 1.0, 2.0 / N);   // within ~one voxel of the unit surface
    }
    // hollow: the exact center cell is NOT in the set
    // (use glm::all(glm::equal(...)) — glm's operator== is component-wise on some versions)
    glm::ivec3 center(1 << (d - 1));
    bool centerPresent = std::any_of(cells.begin(), cells.end(),
        [&](const glm::ivec3& c) { return glm::all(glm::equal(c, center)); });
    EXPECT_FALSE(centerPresent);
}

TEST(ShellVoxelizer, CountScalesAsSurface) {
    auto c4 = Vixen::SVO::ShellVoxels(4).size();
    auto c5 = Vixen::SVO::ShellVoxels(5).size();
    EXPECT_GT(c5, c4 * 3);   // surface ~O(2^2d): doubling N ~4x the shell, not ~8x (solid) nor ~2x
    EXPECT_LT(c5, c4 * 6);
}

// ============================================================================
// BodyOctree — surface-shell ESVO built via the entity/voxel path (SP1 Task 2)
// ============================================================================
//
// Coordinate convention (mirrors test_ray_casting_comprehensive.cpp, which
// creates voxels at INTEGER grid positions and reads hitPoint back in the same
// integer world coords — Morton encoding floors positions onto the integer grid):
//
//   depth d  ->  n = 2^d cells per axis, cells in [0, n)
//   voxel world position = glm::vec3(cell)          (integer grid centres)
//   sphere centre = (n/2), radius = n/2
//   octree bounds: min = (0,0,0), max = (n,n,n)     (set by BuildShellOctree)
//
// NOTE on the validity proxy: LaineKarrasOctree::getVoxelCount() returns a member
// that rebuild() does NOT update (it sets the octree's internal totalVoxels, read
// by getStats(), but leaves getVoxelCount()'s backing field stale at 0). The whole
// existing ray-casting suite therefore validates a populated octree via a ray HIT,
// not getVoxelCount() — we do the same.

// A ray fired from outside along the centre line must strike the near shell.
// That proves the entity path built a populated, traversable octree.
TEST(BodyOctree, BuildsValidAndTraversable) {
    const int depth = 6;
    const float n = static_cast<float>(1 << depth);  // 64
    const float c = n * 0.5f;                         // centre line (y = z = c)

    auto s = Vixen::SVO::BuildShellOctree(depth, /*materialId*/ 2);

    auto entry = s.octree->castRay(glm::vec3(-2.0f, c, c), glm::vec3(1, 0, 0), 0.0f, 1e30f);
    ASSERT_TRUE(entry.hit) << "Ray along centre line should hit the shell — "
                              "octree built via the entity path must be populated";
    EXPECT_LT(entry.hitPoint.x, c)
        << "Entry hit must be on the NEAR half of the sphere (x < centre)";
}

// Hollow vs solid: cast from just inside the centre OUTWARD. For a HOLLOW shell
// the ray crosses the empty interior and strikes the far shell at distance ~radius.
// A SOLID sphere would hit immediately (distance ~0, the centre cell is filled).
// The large centre->shell distance is the hollowness proof. (Counter-check during
// development: a fully solid sphere gives distance 0 for the identical cast.)
TEST(BodyOctree, ShellIsHollowNotSolid) {
    const int depth = 6;
    const float n = static_cast<float>(1 << depth);  // 64
    const float c = n * 0.5f;                         // sphere centre = 32
    const float radius = n * 0.5f;                    // = 32

    auto s = Vixen::SVO::BuildShellOctree(depth, /*materialId*/ 2);

    // Origin a half-cell off the exact centre (the exact centre lands on brick
    // boundaries — an ESVO traversal degeneracy — so nudge by +0.5 on each axis).
    const glm::vec3 origin(c + 0.5f, c + 0.5f, c + 0.5f);

    // Three axis-aligned positive directions all cross the hollow interior and
    // hit the FAR shell; the hit distance must be ~radius, NOT ~0.
    const glm::vec3 dirs[] = { {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
    for (const glm::vec3& dir : dirs) {
        auto hit = s.octree->castRay(origin, dir, 0.0f, 1e30f);
        ASSERT_TRUE(hit.hit) << "Centre-outward ray should hit the far shell";
        float dist = glm::length(hit.hitPoint - origin);
        // Interior is empty: the nearest surface is ~a full radius away. A solid
        // body would report ~0 here. Require most of the radius as the margin.
        EXPECT_GT(dist, radius * 0.75f)
            << "centre->shell distance (" << dist << ") should be ~radius (" << radius
            << ") — a small distance would mean a SOLID interior, not a hollow shell";
        EXPECT_LT(dist, radius * 1.5f)
            << "hit should land on the shell, not overshoot the volume";
    }
}
