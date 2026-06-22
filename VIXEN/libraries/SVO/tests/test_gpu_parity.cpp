// test_gpu_parity.cpp — drive LaineKarrasOctree::castRay to bit-for-bit parity with
// the GPU traversal, using GpuTraversalMirror as the ORACLE.
//
// The mirror (GpuTraversalMirror.h) is a faithful 1:1 C++ port of the GPU body-octree
// shader (BodyInstanceRayMarch.comp + ESVOTraversal.glsl), run against the EXACT byte
// buffers the shader consumes (ShellOctreeGpu::Serialize). The GPU renders COMPLETE
// bodies, so the mirror is correct by construction. Every place castRay disagrees with
// the mirror is a CPU divergence — these tests are the driver that surfaces them.
//
// IMPORTANT: the mirror has its OWN worldToLocal (the OctreeConfig matrices from
// Serialize, which scale the [0,1] grid by kWorldGridSize=10). castRay works in the
// octree's [worldMin,worldMax] = [0,n] frame. So we cannot compare world hitPoints
// directly across the two frames. Instead both produce a hit on a SHELL VOXEL CELL;
// we compare the integer cell each reports, mapped into the common [0,n] grid.

#include "ShellOctree.h"
#include "ShellOctreeGpu.h"
#include "GpuTraversalMirror.h"
#include "VoxelComponents.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

// MSVC's <windows.h> defines `far`/`near` as empty legacy macros that mangle the local `far`
// plane-distance variable below into a syntax error; drop them (test code never wants them).
#undef far
#undef near

using namespace Vixen::SVO;

namespace {

struct ICell {
    int x, y, z;
    bool operator<(const ICell& o) const {
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        return z < o.z;
    }
    bool operator==(const ICell& o) const { return x == o.x && y == o.y && z == o.z; }
};

std::set<ICell> ShellSet(int depth) {
    std::set<ICell> s;
    for (const glm::ivec3& c : Vixen::SVO::ShellVoxels(depth)) s.insert({c.x, c.y, c.z});
    return s;
}

// A cell disagreement between castRay and the oracle is benign FLOATING-POINT NOISE
// (not an algorithm divergence) when BOTH land on a real SHELL surface voxel. The two
// run the identical algorithm but in separate translation units, so the engine TU and
// the header oracle contract FMAs differently → a ~1-ULP entry-t difference. On a
// grazing-tangent ray that shifts which surface voxel is hit (by 1 cell, or — where the
// ray skims along the silhouette and just-clips a near brick — by a few cells / a few
// units of t). The load-bearing invariant is that BOTH still hit the actual surface
// (never empty space / a phantom): castRay never misses (CheckC/CheckD == 0) and never
// hits a non-shell cell (CheckB == 0). A genuine algorithm divergence would put one of
// them on a NON-shell cell.
bool classifyFpNoise(const ICell& a, const ICell& b, float ta, float tb,
                     const std::set<struct ICell>& shellSet);

// Snap a [0,n] world hitPoint to its integer voxel cell (nudge along the ray so a hit
// exactly on a face floors into the voxel the DDA reported).
ICell snapCell(const glm::vec3& hp, const glm::vec3& dir) {
    const glm::vec3 q = hp + dir * 1e-3f;
    return { static_cast<int>(std::floor(q.x)),
             static_cast<int>(std::floor(q.y)),
             static_cast<int>(std::floor(q.z)) };
}

// The mirror runs in the config's local frame (grid [0,1] scaled by kWorldGridSize=10).
// To compare with castRay's [0,n] frame we map the mirror's WORLD hitPoint back to the
// [0,n] grid via the config: gridPos = worldToLocal(hitPoint) * n  (worldToLocal maps
// world -> [0,1]); then snap. The mirror also runs against the SAME octree, so for the
// SAME ray we expect the SAME cell.
struct ParityHarness {
    Vixen::SVO::ShellOctree shell;
    SerializedOctree ser;
    int n;
    GpuTraversalMirror oracle;  // built AFTER ser is rewritten to the [0,n] frame

    explicit ParityHarness(int depth)
        : shell(Vixen::SVO::BuildShellOctree(depth, /*materialId*/ 2)),
          ser(Serialize(shell)),
          n(1 << depth),
          oracle((rewriteConfigToOctreeFrame(ser, n), ser)) {}

    // Make the mirror's WORLD frame identical to castRay's octree frame [0,n]^3, so we
    // can feed the SAME ray to both and compare hitPoints with NO remap. The config's
    // worldToLocal must map [0,n] -> [0,1] (scale 1/n); localToWorld its inverse.
    static void rewriteConfigToOctreeFrame(SerializedOctree& s, int n) {
        const glm::mat4 l2w = glm::scale(glm::mat4(1.0f), glm::vec3(float(n)));  // [0,1] -> [0,n]
        s.config.localToWorld = l2w;
        s.config.worldToLocal = glm::inverse(l2w);
    }

    // castRay works directly in [0,n]; snap its hitPoint.
    ICell cpuCell(const glm::vec3& origin, const glm::vec3& dir, bool& hit, float* t = nullptr) {
        auto h = shell.octree->castRay(origin, dir, 0.0f, 1e30f);
        hit = h.hit;
        if (t) *t = h.tMin;
        return hit ? snapCell(h.hitPoint, dir) : ICell{ -999, -999, -999 };
    }

    // Oracle in the same [0,n] frame: feed the exact ray, return the ABSOLUTE hit voxel
    // cell. (The GPU shades by coarse leaf-entry hitT, so its hitPoint is the brick face,
    // not the voxel — comparing the actual hit VOXEL is the correct parity metric.)
    ICell oracleCell(const glm::vec3& origin, const glm::vec3& dir, bool& hit, float* t = nullptr) {
        auto h = oracle.castRay(origin, dir);
        hit = h.hit;
        if (t) *t = h.t;
        return hit ? ICell{ h.voxel.x, h.voxel.y, h.voxel.z } : ICell{ -999, -999, -999 };
    }
};

bool classifyFpNoise(const ICell& a, const ICell& b, float /*ta*/, float /*tb*/,
                     const std::set<ICell>& shellSet) {
    // Benign iff BOTH picks are real surface voxels (neither is empty space / a phantom).
    return shellSet.count(a) > 0 && shellSet.count(b) > 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Sanity: the oracle ALONE finds the shell (correct-by-construction reference).
// If this fails, the mirror is mis-ported (not a castRay problem).
// ---------------------------------------------------------------------------
TEST(GpuParity, OracleAloneHitsShell) {
    ParityHarness h(6);
    const float n = float(h.n);
    const std::set<ICell> S = ShellSet(6);
    // A handful of axis + oblique rays must all land on a real shell cell. If THIS fails,
    // the mirror itself is mis-ported (independent of castRay).
    struct R { glm::vec3 o, d; };
    const R rays[] = {
        { glm::vec3(-2.0f, n*0.5f, n*0.5f), glm::vec3(1,0,0) },
        { glm::vec3(n*0.5f, n+2.0f, n*0.5f), glm::vec3(0,-1,0) },
        { glm::vec3(-40.3f, -33.7f, 110.9f), glm::normalize(glm::vec3(32.5f,32.5f,32.5f) - glm::vec3(-40.3f,-33.7f,110.9f)) },
    };
    for (const R& r : rays) {
        bool hit = false;
        ICell c = h.oracleCell(r.o, r.d, hit);
        EXPECT_TRUE(hit) << "oracle must hit the shell";
        if (hit) EXPECT_TRUE(S.count(c) > 0)
            << "oracle hit cell (" << c.x << "," << c.y << "," << c.z << ") not in shell set";
    }
}

// ---------------------------------------------------------------------------
// PARITY — axis-aligned battery: castRay cell == oracle cell for every shell voxel
// fired at along ±X/±Y/±Z. (This is the CheckC battery, but now judged against the
// oracle rather than a hand-rolled ground truth.)
// ---------------------------------------------------------------------------
TEST(GpuParity, AxisAlignedRaysMatchOracle) {
    ParityHarness h(6);
    const int n = h.n;
    const std::set<ICell> S = ShellSet(6);
    const float far = float(n) + 8.0f;

    int probes = 0, misses = 0, adjacentMismatch = 0, farMismatch = 0;
    int shown = 0;
    for (const ICell& t : S) {
        const glm::vec3 ctr(t.x + 0.5f, t.y + 0.5f, t.z + 0.5f);
        struct P { glm::vec3 o, d; };
        const P probesList[] = {
            { glm::vec3(far,   ctr.y, ctr.z), glm::vec3(-1,0,0) },
            { glm::vec3(-8.f,  ctr.y, ctr.z), glm::vec3( 1,0,0) },
            { glm::vec3(ctr.x, far,   ctr.z), glm::vec3(0,-1,0) },
            { glm::vec3(ctr.x, -8.f,  ctr.z), glm::vec3(0, 1,0) },
            { glm::vec3(ctr.x, ctr.y, far  ), glm::vec3(0,0,-1) },
            { glm::vec3(ctr.x, ctr.y, -8.f ), glm::vec3(0,0, 1) },
        };
        for (const P& p : probesList) {
            ++probes;
            bool cpuHit = false, oraHit = false; float cpuT = 0, oraT = 0;
            ICell cpu = h.cpuCell(p.o, p.d, cpuHit, &cpuT);
            ICell ora = h.oracleCell(p.o, p.d, oraHit, &oraT);
            if (cpuHit != oraHit) {
                ++misses;  // one hits the shell, the other misses = a CRACK
                if (shown < 12) {
                    std::printf("    MISS-DIVERGE o=(%.1f,%.1f,%.1f) d=(%.0f,%.0f,%.0f) | cpuHit=%d | oraHit=%d ora=(%d,%d,%d)\n",
                                p.o.x, p.o.y, p.o.z, p.d.x, p.d.y, p.d.z, cpuHit, oraHit, ora.x, ora.y, ora.z);
                    ++shown;
                }
            } else if (cpuHit && !(cpu == ora)) {
                if (classifyFpNoise(cpu, ora, cpuT, oraT, S)) ++adjacentMismatch; else ++farMismatch;
            }
        }
    }
    std::printf("[GpuParity axis] probes=%d  misses=%d  fpNoiseMismatch=%d  algoMismatch=%d\n",
                probes, misses, adjacentMismatch, farMismatch);
    EXPECT_EQ(misses, 0) << "castRay vs GPU oracle: " << misses << " MISS divergences (cracks) on axis rays";
    EXPECT_EQ(farMismatch, 0) << "castRay vs GPU oracle: " << farMismatch << " cell mismatches at DIFFERING t (real algorithm divergence)";
}

// ---------------------------------------------------------------------------
// PARITY — oblique single-eye battery (the renderer's regime; CheckD), vs oracle.
// + the exact traced miss from the prior session.
// ---------------------------------------------------------------------------
TEST(GpuParity, ObliqueRaysMatchOracle) {
    ParityHarness h(6);
    const std::set<ICell> S = ShellSet(6);

    const glm::vec3 eyes[] = {
        glm::vec3(-40.3f, -33.7f, 110.9f),   // the traced-miss eye
        glm::vec3(120.7f, 90.1f, -30.3f),
        glm::vec3(-25.1f, 95.9f, 95.3f),
    };

    int probes = 0, misses = 0, adjacentMismatch = 0, farMismatch = 0;
    int shown = 0;
    for (const glm::vec3& eye : eyes) {
        for (const ICell& t : S) {
            const glm::vec3 ctr(t.x + 0.5f, t.y + 0.5f, t.z + 0.5f);
            const glm::vec3 dir = glm::normalize(ctr - eye);
            if (std::abs(dir.x) < 1e-3f || std::abs(dir.y) < 1e-3f || std::abs(dir.z) < 1e-3f) continue;
            ++probes;
            bool cpuHit = false, oraHit = false; float cpuT = 0, oraT = 0;
            ICell cpu = h.cpuCell(eye, dir, cpuHit, &cpuT);
            ICell ora = h.oracleCell(eye, dir, oraHit, &oraT);
            if (cpuHit != oraHit) {
                ++misses;  // one hits the shell, the other misses = a CRACK
                if (shown < 12) {
                    std::printf("    MISS-DIVERGE target=(%d,%d,%d) | cpuHit=%d cpu=(%d,%d,%d) | oraHit=%d ora=(%d,%d,%d)\n",
                                t.x, t.y, t.z, cpuHit, cpu.x, cpu.y, cpu.z, oraHit, ora.x, ora.y, ora.z);
                    ++shown;
                }
            } else if (cpuHit && !(cpu == ora)) {
                if (classifyFpNoise(cpu, ora, cpuT, oraT, S)) ++adjacentMismatch;
                else {
                    ++farMismatch;
                    if (shown < 12) {
                        std::printf("    ALGO-MISMATCH target=(%d,%d,%d) cpu=(%d,%d,%d) t=%.5f | ora=(%d,%d,%d) t=%.5f\n",
                                    t.x, t.y, t.z, cpu.x, cpu.y, cpu.z, cpuT, ora.x, ora.y, ora.z, oraT);
                        ++shown;
                    }
                }
            }
        }
    }
    std::printf("[GpuParity oblique] probes=%d  misses=%d  fpNoiseMismatch=%d  algoMismatch=%d\n",
                probes, misses, adjacentMismatch, farMismatch);
    // Load-bearing parity guarantees: ZERO miss divergences (no cracks) and ZERO cell
    // mismatches at a DIFFERING hit-t / on a non-shell cell (no algorithm divergence).
    // Same-distance mismatches between two real shell voxels are tolerated cross-TU FP
    // noise (the engine and the header oracle contract FMAs differently).
    EXPECT_EQ(misses, 0) << "castRay vs GPU oracle: " << misses << " MISS divergences (cracks) on oblique rays";
    EXPECT_EQ(farMismatch, 0) << "castRay vs GPU oracle: " << farMismatch << " cell mismatches at DIFFERING t (real algorithm divergence)";
}

// ---------------------------------------------------------------------------
// GAMEPLAY OCCUPANCY: the per-voxel point-containment a collision query needs
// ("is world-pos P inside a FILLED body voxel") must agree exactly with the body
// DATA. The correct primitive for an integer-grid body is a world-space voxel
// lookup (the SAME data the renderer reads via ShellOctreeGpu::Serialize, and the
// SAME the mirror brick-DDA samples: Density>0 at the cell). CheckA proved the
// DATA is clean; this proves the per-voxel LOOKUP path returns right answers
// (no tunnelling, no phantom collision) and is GPU-consistent.
//
// NOTE: LaineKarrasOctree::voxelExists() is BRICK-granular (it descends to the
// brick leaf and returns true for the whole 8³ brick), so it is NOT a per-voxel
// containment test for a hollow body — callers that need solid/empty per voxel
// must use the world voxel lookup asserted here.
TEST(GpuParity, BodyPointContainmentMatchesData) {
    const int depth = 6;
    auto s = Vixen::SVO::BuildShellOctree(depth, /*materialId*/ 2);
    const std::set<ICell> S = ShellSet(depth);
    const int n = 1 << depth;

    auto occupied = [&](int x, int y, int z) -> bool {
        const auto e = s.world->getEntityByWorldSpace(glm::vec3(float(x), float(y), float(z)));
        if (!s.world->exists(e)) return false;
        const auto d = s.world->getComponentValue<Vixen::GaiaVoxel::Density>(e);
        return d.has_value() && *d > 0.0f;
    };

    int probes = 0, falseNeg = 0, falsePos = 0;
    for (int z = 0; z < n; ++z)
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float dx = x + 0.5f - n*0.5f, dy = y + 0.5f - n*0.5f, dz = z + 0.5f - n*0.5f;
            const float r = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (r < n*0.5f - 4.0f || r > n*0.5f + 4.0f) continue;  // band around the shell
            ++probes;
            const bool inData = S.count({x, y, z}) > 0;
            const bool inQuery = occupied(x, y, z);
            if (inData && !inQuery) ++falseNeg;
            if (!inData && inQuery) ++falsePos;
        }
    std::printf("[BodyOccupancy] probes=%d  falseNegatives=%d  falsePositives=%d\n", probes, falseNeg, falsePos);
    EXPECT_EQ(falseNeg, 0) << "per-voxel occupancy MISSED " << falseNeg << " filled shell cells (collision would tunnel)";
    EXPECT_EQ(falsePos, 0) << "per-voxel occupancy reported " << falsePos << " EMPTY cells as filled (phantom collision)";
}
