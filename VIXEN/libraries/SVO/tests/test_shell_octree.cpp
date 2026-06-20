#include "ShellVoxelizer.h"
#include "ShellOctree.h"
#include "SVOLOD.h"
#include "VoxelComponents.h"   // Material, Density
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

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

// ============================================================================
// PHANTOM-VOXEL ISOLATION  (Phase 1 — headless diagnosis, no Vulkan/GPU)
// ============================================================================
//
// Symptom: a dense ray render of BuildShellOctree shows detached blocky
// fragments in empty space, in a coherent/regular spatial pattern. These tests
// split BUILD vs TRAVERSAL to pinpoint where the phantoms enter.
//
//   ICell = packed integer cell key for set membership / map keys.
namespace {

struct ICell {
    int x, y, z;
    bool operator<(const ICell& o) const {
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        return z < o.z;
    }
};

std::set<ICell> ShellSet(int depth) {
    std::set<ICell> s;
    for (const glm::ivec3& c : Vixen::SVO::ShellVoxels(depth)) s.insert({c.x, c.y, c.z});
    return s;
}

}  // namespace

// ----------------------------------------------------------------------------
// CHECK A — BUILD correctness: is the DATA clean?
//
// Ground truth S = ShellVoxels(6). Independently enumerate what the build
// produced, two ways, and compare to S:
//   (A1) the GaiaVoxelWorld's created voxel entities (integer positions), and
//   (A2) the octree's occupied brick voxels (walk root->brickViews; query each
//        of the 8^3 slots via the SAME lazy EntityBrickView path the DDA uses).
// If either set contains cells OUTSIDE S, the phantoms are baked into the DATA.
// ----------------------------------------------------------------------------
TEST(PhantomIsolation, CheckA_BuildDataMatchesShellSet) {
    const int depth = 6;
    const std::set<ICell> S = ShellSet(depth);
    ASSERT_FALSE(S.empty());

    auto s = Vixen::SVO::BuildShellOctree(depth, /*materialId*/ 2);

    // ---- A1: world voxel entities (the source of truth the GPU serializer reads).
    std::set<ICell> created;
    for (const auto& e : s.world->querySolidVoxels()) {
        auto p = s.world->getPosition(e);
        ASSERT_TRUE(p.has_value());
        created.insert({ static_cast<int>(std::lround(p->x)),
                         static_cast<int>(std::lround(p->y)),
                         static_cast<int>(std::lround(p->z)) });
    }

    // A1 must equal S exactly (mint-one-voxel-per-cell, integer positions).
    std::vector<ICell> a1_extra, a1_missing;
    std::set_difference(created.begin(), created.end(), S.begin(), S.end(),
                        std::back_inserter(a1_extra));
    std::set_difference(S.begin(), S.end(), created.begin(), created.end(),
                        std::back_inserter(a1_missing));
    EXPECT_TRUE(a1_extra.empty())
        << "A1: world has " << a1_extra.size() << " voxel(s) OUTSIDE the shell set";
    EXPECT_TRUE(a1_missing.empty())
        << "A1: world is MISSING " << a1_missing.size() << " shell cell(s)";

    // ---- A2: octree brick contents via the lazy EntityBrickView lookup.
    // A "brick voxel" is occupied iff Density>0 at the queried cell — exactly the
    // predicate traverseBrickView() uses. Cell = brick localGridOrigin + (x,y,z).
    const Vixen::SVO::Octree* oct = s.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    ASSERT_NE(oct->root, nullptr);
    const int brickSide = oct->brickSideLength;  // 8

    std::set<ICell> brickOccupied;
    for (const auto& view : oct->root->brickViews) {
        const glm::ivec3 origin = view.getLocalGridOrigin();  // worldMin=0 ⇒ world coords
        for (int z = 0; z < brickSide; ++z)
          for (int y = 0; y < brickSide; ++y)
            for (int x = 0; x < brickSide; ++x) {
                auto entity = view.getEntity(x, y, z);
                if (!s.world->exists(entity)) continue;
                auto d = s.world->getComponentValue<Vixen::GaiaVoxel::Density>(entity);
                if (d.has_value() && *d > 0.0f)
                    brickOccupied.insert({ origin.x + x, origin.y + y, origin.z + z });
            }
    }

    std::vector<ICell> a2_extra, a2_missing;
    std::set_difference(brickOccupied.begin(), brickOccupied.end(), S.begin(), S.end(),
                        std::back_inserter(a2_extra));
    std::set_difference(S.begin(), S.end(), brickOccupied.begin(), brickOccupied.end(),
                        std::back_inserter(a2_missing));

    std::printf("[CheckA] |S|=%zu  |world|=%zu  |brickOccupied|=%zu  "
                "A1extra=%zu A1missing=%zu  A2extra=%zu A2missing=%zu\n",
                S.size(), created.size(), brickOccupied.size(),
                a1_extra.size(), a1_missing.size(), a2_extra.size(), a2_missing.size());
    for (size_t i = 0; i < a2_extra.size() && i < 8; ++i)
        std::printf("    A2 phantom-in-data cell (%d,%d,%d)\n",
                    a2_extra[i].x, a2_extra[i].y, a2_extra[i].z);

    EXPECT_TRUE(a2_extra.empty())
        << "A2: octree bricks expose " << a2_extra.size()
        << " occupied cell(s) OUTSIDE the shell set (DATA-side phantom)";
    EXPECT_TRUE(a2_missing.empty())
        << "A2: octree bricks are MISSING " << a2_missing.size() << " shell cell(s)";
}

// ----------------------------------------------------------------------------
// CHECK B — TRAVERSAL correctness: does castRay return hits not in the data?
//
// Cast a DENSE grid of rays straight at the octree in its NATIVE [0,n]^3 space
// (n=64): orthographic -Z over the XY face, plus -X and -Y faces, plus a couple
// of oblique angles. Snap each hit to its integer cell and check membership in
// S. Any out-of-set hit is a TRAVERSAL phantom; tabulate the spatial pattern.
// ----------------------------------------------------------------------------
TEST(PhantomIsolation, CheckB_CastRayHitsAreInShellSet) {
    const int depth = 6;
    const int n = 1 << depth;                    // 64
    const std::set<ICell> S = ShellSet(depth);
    auto s = Vixen::SVO::BuildShellOctree(depth, /*materialId*/ 2);

    // Snap a world-space hit point to the integer cell it sits in. Voxels are
    // created at integer positions p and span the unit cube [p, p+1) for the DDA
    // (voxelWorldMin = floor). Nudge by a tiny step ALONG the ray so a hit exactly
    // on a face floors into the voxel the DDA reported, not the neighbour.
    auto snap = [](const glm::vec3& hp, const glm::vec3& dir) -> ICell {
        const glm::vec3 q = hp + dir * 1e-3f;
        return { static_cast<int>(std::floor(q.x)),
                 static_cast<int>(std::floor(q.y)),
                 static_cast<int>(std::floor(q.z)) };
    };

    int hitCount = 0;
    int phantomCount = 0;
    std::map<ICell, int> phantomCells;   // phantom cell -> #rays that produced it

    auto sweep = [&](const glm::vec3& dir, auto originFn) {
        // 2x supersample per cell so we cross every column and catch thin phantoms.
        const float step = 0.5f;
        for (float a = 0.25f; a < static_cast<float>(n); a += step)
          for (float b = 0.25f; b < static_cast<float>(n); b += step) {
            const glm::vec3 origin = originFn(a, b);
            auto hit = s.octree->castRay(origin, dir, 0.0f, 1e30f);
            if (!hit.hit) continue;
            ++hitCount;
            const ICell cell = snap(hit.hitPoint, dir);
            if (S.find(cell) == S.end()) {
                ++phantomCount;
                phantomCells[cell]++;
            }
        }
    };

    const float far = static_cast<float>(n) + 8.0f;
    // Orthographic-ish faces (rays parallel to an axis, marching across the face).
    sweep(glm::vec3(0, 0, -1), [&](float a, float b){ return glm::vec3(a, b, far); });   // -Z over XY
    sweep(glm::vec3(-1, 0, 0), [&](float a, float b){ return glm::vec3(far, a, b); });   // -X over YZ
    sweep(glm::vec3(0, -1, 0), [&](float a, float b){ return glm::vec3(a, far, b); });   // -Y over XZ
    // Oblique angles (exercise multi-octant descent / mirrored-octant brick pick).
    {
        const glm::vec3 d1 = glm::normalize(glm::vec3(-0.2f, -0.15f, -1.0f));
        sweep(d1, [&](float a, float b){ return glm::vec3(a, b, far); });
        const glm::vec3 d2 = glm::normalize(glm::vec3(-1.0f, -0.25f, -0.2f));
        sweep(d2, [&](float a, float b){ return glm::vec3(far, a, b); });
    }

    // Characterize the phantom pattern: bounding box + per-axis modulo-8 (brick
    // stride) histogram. A coherent brick-aligned pattern reveals the mechanism.
    std::printf("[CheckB] hits=%d  phantomHits=%d  distinctPhantomCells=%zu\n",
                hitCount, phantomCount, phantomCells.size());
    if (!phantomCells.empty()) {
        ICell lo{ n, n, n }, hi{ -1, -1, -1 };
        int modHist[3][8] = {{0}};
        int shown = 0;
        for (const auto& [c, cnt] : phantomCells) {
            lo.x = std::min(lo.x, c.x); lo.y = std::min(lo.y, c.y); lo.z = std::min(lo.z, c.z);
            hi.x = std::max(hi.x, c.x); hi.y = std::max(hi.y, c.y); hi.z = std::max(hi.z, c.z);
            modHist[0][((c.x % 8) + 8) % 8]++;
            modHist[1][((c.y % 8) + 8) % 8]++;
            modHist[2][((c.z % 8) + 8) % 8]++;
            if (shown < 16) {
                std::printf("    phantom cell (%2d,%2d,%2d) x%d  | shellNeighborDist?\n",
                            c.x, c.y, c.z, cnt);
                ++shown;
            }
        }
        std::printf("    phantom bbox: (%d,%d,%d)..(%d,%d,%d)\n",
                    lo.x, lo.y, lo.z, hi.x, hi.y, hi.z);
        const char* ax = "xyz";
        for (int axis = 0; axis < 3; ++axis) {
            std::printf("    %c mod-8: ", ax[axis]);
            for (int m = 0; m < 8; ++m) std::printf("%d ", modHist[axis][m]);
            std::printf("\n");
        }
    }

    EXPECT_EQ(phantomCount, 0)
        << "castRay returned " << phantomCount << " hit(s) on " << phantomCells.size()
        << " cell(s) NOT in the shell set — TRAVERSAL phantom";
}

// ----------------------------------------------------------------------------
// CHECK B' — characterize phantom HIT POINTS: are they in-bounds (a real wrong
// voxel) or out-of-bounds (a corrupt hitPoint)? Trace the full RayHit for the
// phantoms found by an axis-aligned -Z sweep. This separates "DDA reports a hit
// in a neighbouring filled cell" from "the returned hitPoint is garbage".
// ----------------------------------------------------------------------------
TEST(PhantomIsolation, CheckBPrime_PhantomHitPointDiagnostics) {
    const int depth = 6;
    const int n = 1 << depth;
    const std::set<ICell> S = ShellSet(depth);
    auto s = Vixen::SVO::BuildShellOctree(depth, /*materialId*/ 2);

    const float far = static_cast<float>(n) + 8.0f;

    auto inBounds = [n](const glm::vec3& p) {
        const float lo = -0.01f, hi = static_cast<float>(n) + 0.01f;
        return p.x >= lo && p.x <= hi && p.y >= lo && p.y <= hi && p.z >= lo && p.z <= hi;
    };

    auto sweepDiag = [&](const char* label, const glm::vec3& dir, auto originFn) {
        int inB = 0, outB = 0, legit = 0, shown = 0;
        for (float a = 0.25f; a < static_cast<float>(n); a += 0.5f)
          for (float b = 0.25f; b < static_cast<float>(n); b += 0.5f) {
            const glm::vec3 origin = originFn(a, b);
            auto hit = s.octree->castRay(origin, dir, 0.0f, 1e30f);
            if (!hit.hit) continue;
            const glm::vec3 q = hit.hitPoint + dir * 1e-3f;
            ICell cell{ static_cast<int>(std::floor(q.x)),
                        static_cast<int>(std::floor(q.y)),
                        static_cast<int>(std::floor(q.z)) };
            if (S.find(cell) != S.end()) { ++legit; continue; }
            const bool ib = inBounds(hit.hitPoint);
            if (ib) ++inB; else ++outB;
            if (shown < 6) {
                std::printf("      %s ray@(%.2f,%.2f) hitPoint=(%.2f,%.2f,%.2f) tMin=%.3f inBounds=%d\n",
                            label, a, b, hit.hitPoint.x, hit.hitPoint.y, hit.hitPoint.z, hit.tMin, ib);
                ++shown;
            }
          }
        std::printf("[CheckB'] %-8s legit=%d  inBoundsPhantom=%d  outBoundsPhantom=%d\n",
                    label, legit, inB, outB);
    };

    sweepDiag("-Z", glm::vec3(0,0,-1), [&](float a,float b){ return glm::vec3(a, b, far); });
    sweepDiag("-X", glm::vec3(-1,0,0), [&](float a,float b){ return glm::vec3(far, a, b); });
    sweepDiag("-Y", glm::vec3(0,-1,0), [&](float a,float b){ return glm::vec3(a, far, b); });
    sweepDiag("obliqZ", glm::normalize(glm::vec3(-0.2f,-0.15f,-1.0f)),
              [&](float a,float b){ return glm::vec3(a, b, far); });
    sweepDiag("obliqX", glm::normalize(glm::vec3(-1.0f,-0.25f,-0.2f)),
              [&](float a,float b){ return glm::vec3(far, a, b); });
    SUCCEED();  // diagnostic only
}

// ----------------------------------------------------------------------------
// CHECK B'' — single-ray trace of ONE confirmed phantom. Fire exactly the ray
// obliqZ@(1.75,2.25) that produced hitPoint=(-6.25,-3.75,32.0). With
// LKOCTREE_DEBUG_TRAVERSAL=1 in SVOBrickDDA.cpp this prints the selected brick,
// its grid origin, and the DDA hit. The hitPoint is OUTSIDE [0,64] yet hit=true,
// so the brick DDA accepted a voxel the ray never geometrically traverses.
// ----------------------------------------------------------------------------
TEST(PhantomIsolation, CheckBPrime2_SinglePhantomRayTrace) {
    const int depth = 6;
    const int n = 1 << depth;
    auto s = Vixen::SVO::BuildShellOctree(depth, /*materialId*/ 2);

    const float far = static_cast<float>(n) + 8.0f;
    const glm::vec3 origin(1.75f, 2.25f, far);
    const glm::vec3 dir = glm::normalize(glm::vec3(-0.2f, -0.15f, -1.0f));

    std::printf("[CheckB''] origin=(%.3f,%.3f,%.3f) dir=(%.4f,%.4f,%.4f)\n",
                origin.x, origin.y, origin.z, dir.x, dir.y, dir.z);

    auto hit = s.octree->castRay(origin, dir, 0.0f, 1e30f);
    std::printf("[CheckB''] hit=%d hitPoint=(%.4f,%.4f,%.4f) tMin=%.4f entityAlive=%d\n",
                hit.hit ? 1 : 0, hit.hitPoint.x, hit.hitPoint.y, hit.hitPoint.z,
                hit.tMin, s.world->exists(hit.entity) ? 1 : 0);

    // The TRUE ray, evaluated at the reported hit's z, gives where the ray
    // actually is — compare to the returned hitPoint to expose the mismatch.
    if (hit.hit && std::abs(dir.z) > 1e-6f) {
        const float tAtHitZ = (hit.hitPoint.z - origin.z) / dir.z;
        const glm::vec3 trueRayAtZ = origin + dir * tAtHitZ;
        std::printf("[CheckB''] TRUE ray @ z=%.2f is (%.4f,%.4f,%.4f); returned hitPoint x/y "
                    "differ by (%.3f,%.3f) — a real hit would have ZERO difference\n",
                    hit.hitPoint.z, trueRayAtZ.x, trueRayAtZ.y, trueRayAtZ.z,
                    hit.hitPoint.x - trueRayAtZ.x, hit.hitPoint.y - trueRayAtZ.y);
    }
    SUCCEED();  // diagnostic only
}
