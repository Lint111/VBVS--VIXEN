// test_traceworld_mirror.cpp — gpu-shader-debug CPU mirror of shaders/TraceWorld.glsl's
// TraceWorldShadow (any-hit occlusion test for shadow rays).
//
// SYNC CONTRACT: TraceWorldShadowMirror::Trace is a semantic port of TraceWorldShadow --
// same any-hit early-out contract (first confirmed occluder in [tmin, tmax] wins, no
// nearest-hit bookkeeping, no shade-data extraction), same "occluder" definition (a solid
// hit within the queried [tmin, tmax] span). It does NOT reproduce the full ESVO
// tier-crossing traversal (traverseOctreeInstanced) -- that machinery has its own
// dedicated mirror coverage elsewhere; here the "occluder" is modeled as a flat list of
// world-space AABBs (one per voxel/brick in the test scene), which is sufficient to prove
// the any-hit/tmin/tmax contract TraceWorldShadow adds on top of TraceWorld's shared
// instance-loop skeleton. Any change to that contract in TraceWorld.glsl must be mirrored
// here.
//
// VERIFICATION STRATEGY: the mirror is checked against an INDEPENDENT reference
// (ReferenceAnyOccluder below) that re-derives "is anything between A and B" from
// scratch using the repo's existing Ray/AABB/IntersectRayAABB (Data/VoxelTraversal.h) --
// per project rule, never mirror-vs-copy-of-mirror. The reference walks every AABB in the
// scene and independently checks the [tmin, tmax] overlap condition; the mirror is
// expected to agree on the boolean occlusion result for every case (the reference is not
// early-out, so it also cross-checks that early-out doesn't change the answer).
//
// @shader shaders/TraceWorld.glsl (TraceWorldShadow)

#include <gtest/gtest.h>
#include <glm/glm.hpp>

// MSVC defines far/near/min/max as macros via <windows.h>.
#undef far
#undef near
#undef min
#undef max

#include "Data/VoxelTraversal.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace {

using Vixen::SVO::AABB;
using Vixen::SVO::IntersectRayAABB;
using Vixen::SVO::Ray;

// ===========================================================================
// TraceWorldShadowMirror — semantic port of shaders/TraceWorld.glsl's
// TraceWorldShadow, modeling "occluder" as a flat world-space AABB list
// (one per voxel/brick in the scene) rather than the full ESVO descent.
// ===========================================================================
namespace TraceWorldShadowMirror {

struct Occluder {
    AABB box;
};

// Mirrors TraceWorldShadow(vec3 origin, vec3 dir, float tmin, float tmax):
// returns true the moment ANY occluder's ray-entry distance falls within
// [tmin, tmax] -- any-hit, first-found-in-list-order wins, no normal/color/
// material extraction, no nearest-hit tracking across occluders.
bool Trace(const std::vector<Occluder>& occluders, const glm::vec3& origin,
           const glm::vec3& dir, float tmin, float tmax) {
    Ray ray(origin, dir);
    for (const auto& occ : occluders) {
        auto result = IntersectRayAABB(ray, occ.box);
        if (!result.hit) {
            continue;  // ray misses this occluder's AABB entirely
        }
        // The "hit distance" for an any-hit occlusion test is the entry point --
        // where the ray first touches the occluder -- clamped to 0 when the ray
        // origin already starts inside the box (tEnter < 0 in that case), which
        // mirrors traverseOctreeInstanced's own "already inside" convention.
        float hitT = std::max(result.tEnter, 0.0f);
        if (hitT >= tmin && hitT <= tmax) {
            return true;  // any-hit: stop at the first confirmed occluder
        }
    }
    return false;  // no occluder found in [tmin, tmax] -- lit
}

} // namespace TraceWorldShadowMirror

// ===========================================================================
// Independent reference — re-derived directly from "is anything between A
// and B", NOT copied from the mirror. Walks the full occluder list (no
// early-out) and independently recomputes the entry-distance/tmin/tmax
// overlap test, then cross-checks that the exhaustive answer matches the
// mirror's early-out answer.
// ===========================================================================
namespace Reference {

// Independent slab-method ray/AABB entry-distance, hand-derived rather than
// calling IntersectRayAABB a second time under a different name.
std::optional<float> RayAabbEntry(const glm::vec3& origin, const glm::vec3& dir,
                                   const AABB& box) {
    float tEnter = -std::numeric_limits<float>::infinity();
    float tExit = std::numeric_limits<float>::infinity();

    for (int axis = 0; axis < 3; ++axis) {
        float o = origin[axis];
        float d = dir[axis];
        float lo = box.min[axis];
        float hi = box.max[axis];

        if (std::abs(d) < 1e-8f) {
            // Ray parallel to this slab: must already lie within it.
            if (o < lo || o > hi) return std::nullopt;
            continue;
        }
        float t0 = (lo - o) / d;
        float t1 = (hi - o) / d;
        if (t0 > t1) std::swap(t0, t1);
        tEnter = std::max(tEnter, t0);
        tExit = std::min(tExit, t1);
        if (tEnter > tExit) return std::nullopt;
    }
    if (tExit < 0.0f) return std::nullopt;  // box entirely behind the ray
    return tEnter;
}

bool AnyOccluderExhaustive(const std::vector<TraceWorldShadowMirror::Occluder>& occluders,
                            const glm::vec3& origin, const glm::vec3& dir, float tmin,
                            float tmax) {
    bool found = false;
    for (const auto& occ : occluders) {
        auto entry = RayAabbEntry(origin, dir, occ.box);
        if (!entry.has_value()) continue;
        float hitT = std::max(*entry, 0.0f);
        if (hitT >= tmin && hitT <= tmax) {
            found = true;  // deliberately keep scanning -- exhaustive, not any-hit
        }
    }
    return found;
}

} // namespace Reference

// ===========================================================================
// Test fixtures / helpers
// ===========================================================================

using TraceWorldShadowMirror::Occluder;

AABB UnitVoxelAt(const glm::vec3& center) {
    return AABB(center - glm::vec3(0.5f), center + glm::vec3(0.5f));
}

// ===========================================================================
// 1) Unoccluded ray: a point with a clear line to the light returns false (lit).
// ===========================================================================

TEST(TraceWorldShadowMirror, UnoccludedRayIsLit) {
    // Single voxel far off to the side; ray goes straight up toward the light,
    // nothing in its path.
    std::vector<Occluder> occluders = { Occluder{ UnitVoxelAt(glm::vec3(10, 0, 10)) } };

    glm::vec3 origin(0, 0, 0);
    glm::vec3 dir(0, 1, 0);  // straight up toward an overhead light
    float tmin = 0.001f;
    float tmax = 100.0f;

    bool mirrorResult = TraceWorldShadowMirror::Trace(occluders, origin, dir, tmin, tmax);
    bool refResult = Reference::AnyOccluderExhaustive(occluders, origin, dir, tmin, tmax);

    EXPECT_FALSE(mirrorResult) << "ray with no blocker in its path should be lit";
    EXPECT_EQ(mirrorResult, refResult);
}

// ===========================================================================
// 2) Known blocker: a ray with a voxel directly between origin and light
//    returns true (shadowed).
// ===========================================================================

TEST(TraceWorldShadowMirror, KnownBlockerIsShadowed) {
    // Blocker voxel sits directly on the path from origin toward the light.
    std::vector<Occluder> occluders = { Occluder{ UnitVoxelAt(glm::vec3(0, 5, 0)) } };

    glm::vec3 origin(0, 0, 0);
    glm::vec3 dir(0, 1, 0);
    float tmin = 0.001f;
    float tmax = 100.0f;  // well past the blocker at t~4.5..5.5

    bool mirrorResult = TraceWorldShadowMirror::Trace(occluders, origin, dir, tmin, tmax);
    bool refResult = Reference::AnyOccluderExhaustive(occluders, origin, dir, tmin, tmax);

    EXPECT_TRUE(mirrorResult) << "ray with a known blocker in its path should be shadowed";
    EXPECT_EQ(mirrorResult, refResult);
}

// Mixed scene: one clear path, one blocked path, using the SAME occluder list
// -- proves the any-hit test is per-ray, not scene-global.
TEST(TraceWorldShadowMirror, MixedSceneDistinguishesRays) {
    std::vector<Occluder> occluders = {
        Occluder{ UnitVoxelAt(glm::vec3(0, 5, 0)) },   // blocks straight-up rays
        Occluder{ UnitVoxelAt(glm::vec3(20, 20, 20)) }, // far away, blocks nothing here
    };

    glm::vec3 origin(0, 0, 0);
    float tmin = 0.001f, tmax = 100.0f;

    EXPECT_TRUE(TraceWorldShadowMirror::Trace(occluders, origin, glm::vec3(0, 1, 0), tmin, tmax));
    EXPECT_FALSE(TraceWorldShadowMirror::Trace(occluders, origin, glm::vec3(1, 0, 0), tmin, tmax));
}

// ===========================================================================
// 3) Self-shadow guard: a ray starting exactly ON a surface facing the light
//    is NOT self-occluded, provided tmin/origin bias per the documented
//    convention (either origin nudged off the surface, or tmin > 0 skipping
//    the first epsilon).
// ===========================================================================

TEST(TraceWorldShadowMirror, SelfShadowGuard_OriginBiasAvoidsAcne) {
    // The "surface" is the +Y face of a voxel centered at the origin-ish position;
    // a shadow ray leaving that face straight toward the light must not immediately
    // re-hit the voxel it just left. Convention (a): origin nudged along the normal.
    std::vector<Occluder> occluders = { Occluder{ UnitVoxelAt(glm::vec3(0, 0, 0)) } };

    glm::vec3 surfacePoint(0, 0.5f, 0);  // exactly on the +Y face
    glm::vec3 normal(0, 1, 0);
    float eps = 1e-3f;
    glm::vec3 biasedOrigin = surfacePoint + normal * eps;  // convention (a)

    float tmin = 0.0f;  // no additional tmin bias needed -- origin already off the surface
    float tmax = 100.0f;

    bool mirrorResult = TraceWorldShadowMirror::Trace(occluders, biasedOrigin, normal, tmin, tmax);
    EXPECT_FALSE(mirrorResult) << "origin-biased shadow ray must not self-occlude the surface it left";
}

TEST(TraceWorldShadowMirror, SelfShadowGuard_TminBiasAvoidsAcne) {
    // Convention (b): origin left exactly on the surface, tmin skips the acne epsilon.
    std::vector<Occluder> occluders = { Occluder{ UnitVoxelAt(glm::vec3(0, 0, 0)) } };

    glm::vec3 surfacePoint(0, 0.5f, 0);
    glm::vec3 normal(0, 1, 0);
    float eps = 1e-3f;

    float tmin = eps;  // convention (b): skip the acne band instead of biasing origin
    float tmax = 100.0f;

    bool mirrorResult = TraceWorldShadowMirror::Trace(occluders, surfacePoint, normal, tmin, tmax);
    EXPECT_FALSE(mirrorResult) << "tmin-biased shadow ray must not self-occlude the surface it left";
}

TEST(TraceWorldShadowMirror, NoBiasProducesAcne_DemonstratesWhyBiasIsRequired) {
    // Negative-control: no bias at all (origin ON the surface, tmin=0) DOES
    // self-occlude at t=0 -- this is the "shadow acne" failure mode the bias
    // conventions above exist to avoid. Documents the requirement rather than
    // asserting desired behavior.
    std::vector<Occluder> occluders = { Occluder{ UnitVoxelAt(glm::vec3(0, 0, 0)) } };

    glm::vec3 surfacePoint(0, 0.5f, 0);
    glm::vec3 normal(0, 1, 0);

    bool mirrorResult = TraceWorldShadowMirror::Trace(occluders, surfacePoint, normal, 0.0f, 100.0f);
    EXPECT_TRUE(mirrorResult) << "unbiased shadow ray from an on-surface origin self-occludes (acne) "
                                  "-- this is why TraceWorldShadow requires caller-side bias";
}

// ===========================================================================
// 4) tmax honored: an occluder beyond the light distance does not count.
// ===========================================================================

TEST(TraceWorldShadowMirror, OccluderBeyondTmaxDoesNotCount) {
    // Blocker sits at t~9.5..10.5, but the light is only at t=5 -- tmax=5 must
    // exclude it (it's beyond the light, not between the surface and the light).
    std::vector<Occluder> occluders = { Occluder{ UnitVoxelAt(glm::vec3(0, 10, 0)) } };

    glm::vec3 origin(0, 0, 0);
    glm::vec3 dir(0, 1, 0);
    float tmin = 0.001f;
    float tmax = 5.0f;  // light distance, well short of the blocker at t~9.5

    bool mirrorResult = TraceWorldShadowMirror::Trace(occluders, origin, dir, tmin, tmax);
    bool refResult = Reference::AnyOccluderExhaustive(occluders, origin, dir, tmin, tmax);

    EXPECT_FALSE(mirrorResult) << "occluder beyond tmax (the light) must not count as shadowing";
    EXPECT_EQ(mirrorResult, refResult);
}

TEST(TraceWorldShadowMirror, OccluderJustInsideTmaxCounts) {
    // Same blocker, but tmax now reaches past it -- must be caught.
    std::vector<Occluder> occluders = { Occluder{ UnitVoxelAt(glm::vec3(0, 10, 0)) } };

    glm::vec3 origin(0, 0, 0);
    glm::vec3 dir(0, 1, 0);
    float tmin = 0.001f;
    float tmax = 11.0f;  // past the blocker's far face at t=10.5

    bool mirrorResult = TraceWorldShadowMirror::Trace(occluders, origin, dir, tmin, tmax);
    bool refResult = Reference::AnyOccluderExhaustive(occluders, origin, dir, tmin, tmax);

    EXPECT_TRUE(mirrorResult) << "occluder within tmax must count as shadowing";
    EXPECT_EQ(mirrorResult, refResult);
}

// ===========================================================================
// 5) Any-hit early-out equivalence: with multiple occluders in the [tmin,tmax]
//    span, the mirror's early-out answer must agree with the reference's
//    exhaustive scan regardless of list order (early-out must not change the
//    boolean result, only how much work is done to get it).
// ===========================================================================

TEST(TraceWorldShadowMirror, EarlyOutAgreesWithExhaustiveScan_MultipleOccludersInRange) {
    std::vector<Occluder> occluders = {
        Occluder{ UnitVoxelAt(glm::vec3(0, 3, 0)) },
        Occluder{ UnitVoxelAt(glm::vec3(0, 6, 0)) },
        Occluder{ UnitVoxelAt(glm::vec3(0, 9, 0)) },
    };

    glm::vec3 origin(0, 0, 0);
    glm::vec3 dir(0, 1, 0);
    float tmin = 0.001f;
    float tmax = 100.0f;

    bool mirrorResult = TraceWorldShadowMirror::Trace(occluders, origin, dir, tmin, tmax);
    bool refResult = Reference::AnyOccluderExhaustive(occluders, origin, dir, tmin, tmax);

    EXPECT_TRUE(mirrorResult);
    EXPECT_EQ(mirrorResult, refResult);

    // Reversed list order must not change the answer -- any-hit is order-independent
    // in its boolean result even though early-out visits fewer elements.
    std::vector<Occluder> reversed(occluders.rbegin(), occluders.rend());
    bool reversedResult = TraceWorldShadowMirror::Trace(reversed, origin, dir, tmin, tmax);
    EXPECT_EQ(mirrorResult, reversedResult);
}

TEST(TraceWorldShadowMirror, EmptySceneIsAlwaysLit) {
    std::vector<Occluder> occluders;  // no geometry at all
    bool mirrorResult = TraceWorldShadowMirror::Trace(occluders, glm::vec3(0), glm::vec3(0, 1, 0),
                                                        0.001f, 100.0f);
    EXPECT_FALSE(mirrorResult);
}

} // namespace
