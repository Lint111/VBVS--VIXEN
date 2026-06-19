#include "ShellVoxelizer.h"
#include "ShellOctree.h"
#include "SVOLOD.h"
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

// LOD ladder: a coarse-LOD cast resolves the shell at a coarser voxel (lower user
// scale number) than a full-detail cast of the same ray. This is the headless half
// of the LOD that SP2 exploits.
//
// Scale convention (confirmed from source):
//   Full-detail path (traverseBrickView, SVOBrickDDA.cpp line 509):
//       hit.scale = m_maxLevels - 1              ← always the finest user scale
//   LOD early-terminate path (SVOTraversal.cpp line 647):
//       lodHit.scale = esvoToUserScale(state.scale)
//       where esvoToUserScale(s) = s - (ESVO_MAX_SCALE - m_maxLevels + 1)
//       state.scale starts at ESVO_MAX_SCALE (22) and DECREMENTS on each PUSH.
//       For depth=7 (maxLevels=10): esvoToUserScale(s) = s - 13.
//         root  esvoScale=22 → userScale=9  (same as leaf — root LOD is a no-op)
//         esvoScale=21       → userScale=8
//         esvoScale=20       → userScale=7  (← this is what we want LOD to return)
//       Full-detail brick leaf always: m_maxLevels - 1 = 9.
//
// Key: LOD must terminate at an INTERMEDIATE ESVO scale (not the root) to produce
// a userScale strictly below m_maxLevels-1. We tune rayDirSize to hit esvoScale=20.
//
// Assertion matches test_lod.cpp DistantVoxelTerminatesEarly pattern (strict <).
TEST(BodyOctree, CoarseLodGivesCoarserShell) {
    // Depth 7 → n = 128 cells, sphere radius = 64.
    const int depth = 7;
    const float n     = static_cast<float>(1 << depth);  // 128
    const float c     = n * 0.5f;                         // 64  (centre)

    auto s = Vixen::SVO::BuildShellOctree(depth, /*materialId*/ 2);

    // Same ray geometry as BuildsValidAndTraversable (entry along +x through centre).
    const glm::vec3 origin(-2.0f, c, c);
    const glm::vec3 dir(1.0f, 0.0f, 0.0f);

    // --- Full-detail cast (no LOD) ---
    auto fullHit = s.octree->castRay(origin, dir, 0.0f, 1e30f);
    ASSERT_TRUE(fullHit.hit)
        << "Full-detail ray must hit the shell before testing the LOD ladder";

    // --- Coarse-LOD cast: pick rayDirSize carefully so LOD terminates at an
    // INTERMEDIATE internal node (not the root, not the leaf) — giving a
    // user scale strictly less than the leaf scale (m_maxLevels - 1 = 9).
    //
    // LOD condition (SVOTraversal.cpp):
    //   shouldTerminate ⇔ worldDistance * rayDirSize >= worldVoxelSize
    //
    // With depth=7 (n=128, worldSize=128, maxLevels=10, brickDepth=3):
    //   esvoToUserScale(s) = s - 13  (for maxLevels=10: offset = 22-10+1 = 13)
    //   root  esvoScale=22 → voxelSize = 64  → userScale=9 (same as leaf!)
    //   esvoScale=21 → voxelSize = 32  → userScale=8
    //   esvoScale=20 → voxelSize = 16  → userScale=7
    //
    // Ray entry is at x=0 (origin x=-2 + 2 units travel), so worldDistance ≈ 2-3.
    // To NOT trigger at root (voxelSize=64): need 3 * rayDirSize < 64 → rayDirSize < 21.
    // To trigger at esvoScale=20 (voxelSize=16): need 2 * rayDirSize >= 16 → rayDirSize >= 8.
    // We use rayDirSize=10 (a safe midpoint): triggers at esvoScale≤20, not at 22 or 21.
    Vixen::SVO::LODParameters coarseParams(0.0f, 10.0f);  // rayOrigSize=0, rayDirSize=10
    ASSERT_TRUE(coarseParams.isEnabled())
        << "LOD must be enabled (non-zero cone spread)";

    auto coarseHit = s.octree->castRayWithLOD(origin, dir, coarseParams, 0.0f, 1e30f);
    ASSERT_TRUE(coarseHit.hit)
        << "Coarse-LOD ray must also hit the shell";

    // The LOD ladder: coarse hit terminates before reaching the brick leaf, so its
    // user scale is smaller (fewer descents → higher ESVO scale → lower user scale).
    // test_lod.cpp's DistantVoxelTerminatesEarly asserts EXPECT_LE (coarse <= full);
    // here the scale arithmetic is deterministic, so we assert the stronger strict <.
    EXPECT_LT(coarseHit.scale, fullHit.scale)
        << "Coarse-LOD hit (scale=" << coarseHit.scale
        << ") should have a lower user-scale number than full-detail hit (scale="
        << fullHit.scale << "); lower user scale = fewer descents = coarser voxel";
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
