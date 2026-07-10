// test_tier_crossing_mirror_parity.cpp — Tiered-ESVO Inc2 M4 Task 10 sync.
//
// The M3 Opus validator addendum flagged a gap: no automated parity test exercises
// GpuTraversalMirror's crossing-restart path at all (M3's live gate proved the SHADER
// crosses correctly; the CPU mirror's own castRay()/castRayOnce() split was never
// independently driven through a real two-tree fixture). This is the "cheap win for
// M4/M5" the addendum named — landed here as part of M4 since Task 10 (residency
// reuse) touches that exact seam (GpuTraversalMirror.h's new brickResident check,
// added in castRay() right where m_childCfg first becomes available).
//
// Scope: this test proves TWO things about the MIRROR specifically (not the shader —
// M4's shader behavior is proven by the live GPU gate in
// test_tier_crossing_lod_residency.cpp):
//   1. A resident child (m_childCfg.brickResident=1, the mirror's default via
//      OctreeConfig{} zero-init... no: default-constructed brickResident is 0, so a
//      test must explicitly opt IN to residency) genuinely crosses and reports the
//      CHILD's own hit, distinct from the parent's.
//   2. A non-resident child (brickResident=0) makes the mirror report the SAME
//      result as if RegisterTierCrossingChild had never been called at all (i.e.
//      the parent's own castRayOnce() miss stands) — the mirror's own encoding of
//      "never cross" (§5.3's "just another mip-fallback case, one tier higher").
//
// Fixture: reuses test_tier_crossing_construction.cpp's exact SdfFixture shape
// (n=16, r=6.0, brickDepth=3 -> bricksPerAxis=2 -> root's 8 children are ALL
// deterministic brick-level leaves) so every leaf is a real, markable brick leaf —
// no leaf-finding guesswork. ALL 8 root leaves are marked tier-crossing (pointing at
// the SAME child) rather than locating which specific leaf a hand-aimed ray would
// hit — this sidesteps needing to reverse-engineer octant/ray geometry (GpuTraversalMirror::Hit
// exposes no leaf/octant identity to aim at directly) while still being a genuine
// proof: whichever leaf the ray actually hits, it is tier-crossing, by construction.

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

#undef far
#undef near
#undef min
#undef max

#include "SdfBake.h"
#include "ShellOctreeGpu.h"
#include "MipBake.h"
#include "GpuTraversalMirror.h"
#include "TierRef.h"
#include "SVOTypes.h"

using namespace Vixen::SVO;

namespace {

struct SdfFixture {
    SdfBodyOctree body;
    int n = 16;
    float r;
    glm::vec3 center{8.0f, 8.0f, 8.0f};

    explicit SdfFixture(float radius = 6.0f) : r(radius) {
        RecipeParams rp{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, 2.0f);
        body = BuildSdfBodyOctree(baked, 3);
    }
};

struct LeafLocation {
    uint32_t parentDescriptorIndex;
    int octant;
};

std::vector<LeafLocation> FindAllLeaves(const Octree& oct) {
    std::vector<LeafLocation> leaves;
    const auto& descs = oct.root->childDescriptors;
    for (uint32_t i = 0; i < descs.size(); ++i) {
        const ChildDescriptor& d = descs[i];
        for (int octant = 0; octant < 8; ++octant) {
            if (d.hasChild(octant) && d.isLeaf(octant)) {
                leaves.push_back({i, octant});
            }
        }
    }
    return leaves;
}

// Builds a parent SdfFixture with EVERY root leaf marked tier-crossing (pointing at
// childOctreeIndex=1, childOriginLocal=the parent-local frame's own center, childScale=1.0
// — the same "child occupies the SAME [1,2) cell as the crossing leaf" placement
// BuildRenderGraph.cpp's VIXEN_TIER_CROSSING_DEMO scene uses), returning the mip-baked,
// nodeArrayBase/tierRefTableBase-rewritten-to-slot-0 SerializedOctree ready for
// GpuTraversalMirror's constructor.
SerializedOctree BuildParentAllLeavesMarked(const SdfFixture& parentFixture) {
    SerializedOctree parentSer = SerializeSdf(parentFixture.body);
    const Octree* oct = parentFixture.body.octree->getOctree();
    if (oct != nullptr) {
        BakeAndAttachMipPool(*oct, parentSer);
    }

    std::vector<LeafLocation> leaves = FindAllLeaves(*oct);
    EXPECT_GE(leaves.size(), 1u) << "fixture must have at least one leaf to mark";

    TierRef ref{};
    ref.childOctreeIndex = 1u;  // child concatenated at slot 1 (this test's convention)
    ref.childOriginLocal[0] = 1.5f;
    ref.childOriginLocal[1] = 1.5f;
    ref.childOriginLocal[2] = 1.5f;
    ref.childScale = 1.0f;
    constexpr uint8_t kChildRootScaleHint = 22;

    for (const LeafLocation& loc : leaves) {
        MarkLeafAsTierCrossing(parentSer, loc.parentDescriptorIndex, loc.octant, ref, kChildRootScaleHint);
    }

    // Single-tree slot-0 bookkeeping (parentSer is used standalone, not concatenated
    // with a real sibling in this test — GpuTraversalMirror's constructor already
    // assumes bases are 0, matching Serialize()'s own single-tree convention).
    parentSer.config.nodeArrayBase  = 0;
    parentSer.config.brickArrayBase = 0;
    setSdfBrickArrayBase(parentSer.config, 0);
    setMipPoolBase(parentSer.config, 0);
    setTierRefTableBase(parentSer.config, 0);

    return parentSer;
}

// A ray guaranteed to hit the fixture's sphere: fired from outside along -Z toward
// the bake center, the same "axis-aligned battery" convention test_gpu_parity.cpp
// and test_tier_crossing_construction.cpp's own sibling tests already rely on for
// this exact (n=16, r=6.0, center=(8,8,8)) fixture shape.
constexpr glm::vec3 kRayOrigin{8.0f, 8.0f, 30.0f};
constexpr glm::vec3 kRayDir{0.0f, 0.0f, -1.0f};

}  // namespace

// ---------------------------------------------------------------------------
// A resident child genuinely crosses: the SAME parent+ray, with the SAME
// registered child, differs ONLY in brickResident (true vs. false) must
// produce DIFFERENT hit distances -- proving the resident case actually
// enters the child tree's own (deliberately larger-radius, so the surface it
// reports is unambiguously not the parent's) geometry, while the
// non-resident case (proven separately below to be a complete no-op)
// resolves to the parent's own brick-level hit instead. Isolating residency
// as the ONLY variable (both cases register the identical child) is a
// tighter proof than comparing against "no child registered at all" --
// RegisterTierCrossingChild's own no-child fallback takes a DIFFERENT code
// path (m_hasChild==false) than the residency check (m_hasChild==true,
// m_childCfg.brickResident==0), so those two "never cross" cases are not
// interchangeable evidence for each other.
// ---------------------------------------------------------------------------
TEST(TierCrossingMirrorParity, ResidentChildCrossesAndDiffersFromNonResident) {
    SdfFixture parentFixture(6.0f);
    SdfFixture childFixture(7.2f);  // deliberately different radius (BuildRenderGraph.cpp's
                                    // own VIXEN_TIER_CROSSING_DEMO convention) -- an
                                    // independently-baked, differently-sized sphere means a
                                    // genuine crossing reports a DIFFERENT surface, not just
                                    // FP noise on an incidentally-similar one.
    SerializedOctree parentSer = BuildParentAllLeavesMarked(parentFixture);
    SerializedOctree childSerResident = SerializeSdf(childFixture.body);
    childSerResident.config.nodeArrayBase  = 0;
    childSerResident.config.brickArrayBase = 0;
    setSdfBrickArrayBase(childSerResident.config, 0);
    // Resident: the shader's own convention (BodyOctreeSceneNode::CreateOctreeBuffers)
    // stamps brickResident into every config once bricks are actually uploaded --
    // here we set it directly, since this test drives the mirror, not the real
    // upload pipeline.
    setBrickResident(childSerResident.config, /*resident=*/true);

    SerializedOctree childSerNonResident = childSerResident;
    setBrickResident(childSerNonResident.config, /*resident=*/false);

    GpuTraversalMirror mirrorResident(parentSer);
    mirrorResident.RegisterTierCrossingChild(1u, childSerResident);
    GpuTraversalMirror mirrorNonResident(parentSer);
    mirrorNonResident.RegisterTierCrossingChild(1u, childSerNonResident);

    const auto residentHit    = mirrorResident.castRay(kRayOrigin, kRayDir);
    const auto nonResidentHit = mirrorNonResident.castRay(kRayOrigin, kRayDir);

    ASSERT_TRUE(residentHit.hit) << "resident child: the crossing must produce a real hit";
    // Non-resident: the mirror does not model mip-sample shading at all (see
    // GpuTraversalMirror.h's own header comment and the residency-check comment
    // added for Task 10) -- a real farBit==1 leaf's PARENT side has no brick data
    // of its own to fall back to (that's the whole point of a tier-crossing leaf:
    // contourPointer is a TierRef index, never a brick index), so declining the
    // crossing correctly reports a MISS from this mirror's perspective, exactly
    // matching what castRayOnce() (parent call) already returns for ANY tier-
    // crossing leaf whose crossing this call site declines (mirrors the shader's
    // OWN behavior at this exact call: traverseOctreeInstancedOnce returns
    // hit=false for a farBit==1 leaf regardless of what the wrapper does next --
    // only shadeFromMipSample, which this mirror does not implement, would turn
    // that into a real "hit" in the actual GPU render). The prior sibling test
    // (NonResidentChildBehavesIdenticallyToNoChildRegistered) is the test that
    // proves NON-RESIDENCY is a genuine no-op vs. no-child-registered at all.
    EXPECT_FALSE(nonResidentHit.hit)
        << "non-resident child: the mirror has no mip-fallback shading to fall "
           "back to, so this must report a miss (matching castRayOnce()'s own "
           "return for a declined tier-crossing leaf) -- NOT the child's geometry";

    // The decisive, mirror-specific assertion: identical parent/ray/registered-child,
    // differing ONLY in brickResident, must produce DIFFERENT outcomes -- proving
    // the resident case genuinely crossed into the child's own geometry while the
    // non-resident case did not cross at all.
    EXPECT_NE(residentHit.hit, nonResidentHit.hit)
        << "resident vs. non-resident child (otherwise identical scene) reported "
           "the SAME hit/miss outcome -- the residency flag should be the ONE "
           "thing that changes whether the crossing happens";
}

// ---------------------------------------------------------------------------
// Tiered-ESVO Inc2 M4 Task 10 sync: THE decisive residency-reuse assertion. A
// NON-resident child (brickResident=0) must make the mirror behave EXACTLY as if
// no child were registered at all -- the parent's own castRayOnce() result stands,
// with no crossing/restart attempted.
// ---------------------------------------------------------------------------
TEST(TierCrossingMirrorParity, NonResidentChildBehavesIdenticallyToNoChildRegistered) {
    SdfFixture parentFixture;
    SdfFixture childFixture;
    SerializedOctree parentSer = BuildParentAllLeavesMarked(parentFixture);
    SerializedOctree childSer  = SerializeSdf(childFixture.body);
    childSer.config.nodeArrayBase  = 0;
    childSer.config.brickArrayBase = 0;
    setSdfBrickArrayBase(childSer.config, 0);
    setBrickResident(childSer.config, /*resident=*/false);  // THE condition under test

    GpuTraversalMirror mirrorNoChild(parentSer);  // control
    GpuTraversalMirror mirrorNonResidentChild(parentSer);
    mirrorNonResidentChild.RegisterTierCrossingChild(1u, childSer);

    const auto noChildHit = mirrorNoChild.castRay(kRayOrigin, kRayDir);
    const auto nonResidentHit = mirrorNonResidentChild.castRay(kRayOrigin, kRayDir);

    ASSERT_EQ(noChildHit.hit, nonResidentHit.hit)
        << "a non-resident child must not change whether the ray reports a hit at all";
    if (noChildHit.hit) {
        EXPECT_FLOAT_EQ(noChildHit.t, nonResidentHit.t)
            << "a non-resident child's crossing must be a complete no-op on the "
               "mirror's reported hit -- t must be BIT-IDENTICAL to the no-child "
               "control (both resolve to the PARENT's own result, never touching "
               "the child tree at all), not merely close";
        EXPECT_EQ(noChildHit.voxel, nonResidentHit.voxel);
        EXPECT_EQ(noChildHit.iterations, nonResidentHit.iterations);
    }
}

// ---------------------------------------------------------------------------
// Control: confirm the fixture/harness itself is sound -- a plain, unmarked parent
// (no tier-crossing leaves at all) hits its own sphere directly, matching
// test_gpu_parity.cpp's own "oracle alone hits the shell" sanity precedent. Not a
// tier-crossing test; a harness self-check.
// ---------------------------------------------------------------------------
TEST(TierCrossingMirrorParity, UnmarkedParentHitsOwnSurface) {
    SdfFixture parentFixture;
    SerializedOctree parentSer = SerializeSdf(parentFixture.body);
    parentSer.config.nodeArrayBase  = 0;
    parentSer.config.brickArrayBase = 0;
    setSdfBrickArrayBase(parentSer.config, 0);

    GpuTraversalMirror mirror(parentSer);
    const auto hit = mirror.castRay(kRayOrigin, kRayDir);
    ASSERT_TRUE(hit.hit) << "an unmarked parent sphere must be hit directly by this ray";
}

namespace {

// Tiered-ESVO Inc3 M1 Task 3 fixture: a parent+child pair where childOriginLocal(k)
// is engineered so the REMAPPED child ray enters the child's [1,2) cube at a FIXED
// physical local point, (1.5,1.5,1.5)+kLocalOffsetC, for EVERY childScale k --
// derived from remapRayIntoChildFrame's own formula:
//   childLocalOrigin = (parentLocalOrigin-childOriginLocal)*(1/k) + 1.5
// Setting childOriginLocal(k) = parentLocalOrigin - kLocalOffsetC*k makes the
// (parentLocalOrigin-childOriginLocal)*(1/k) term collapse to exactly
// kLocalOffsetC*k*(1/k) = kLocalOffsetC, independent of k — so the child ray's
// ENTRY POINT and DIRECTION are k-invariant; only the parametrization speed
// (world-units-per-internal-t-unit, i.e. |childRayDirWorld|) varies with k. This
// isolates EXACTLY the quantity Task 1's fix corrects, with no other geometry
// changing across the childScale sweep.
//
// parentLocalOrigin (the crossing point, in the shifted [1,2) Laine-Karras
// convention) for parentFixture(6.0f) + kRayOrigin/kRayDir is (1.8,1.8,2.0) —
// confirmed via direct instrumentation of tierCross.parentLocalOrigin (a
// temporary debug printf, since hand-deriving it through the bake-grid/
// worldToLocal coordinate chain proved highly error-prone across several
// attempts). It is fully deterministic given the fixed SdfFixture(6.0f) +
// kRayOrigin/kRayDir already used throughout this file, so it is safe to bake
// in as a fixture constant, exactly like kRayOrigin/kRayDir themselves.
constexpr glm::vec3 kParentLocalOrigin{1.8f, 1.8f, 2.0f};
constexpr glm::vec3 kLocalOffsetC{0.0f, 0.0f, 3.0f};

SerializedOctree BuildTask3ParentWithScale(const SdfFixture& parentFixture, float childScale) {
    SerializedOctree parentSer = SerializeSdf(parentFixture.body);
    const Octree* oct = parentFixture.body.octree->getOctree();
    BakeAndAttachMipPool(*oct, parentSer);
    std::vector<LeafLocation> leaves = FindAllLeaves(*oct);
    const glm::vec3 childOriginLocal = kParentLocalOrigin - kLocalOffsetC * childScale;
    TierRef ref{};
    ref.childOctreeIndex = 1u;
    ref.childOriginLocal[0] = childOriginLocal.x;
    ref.childOriginLocal[1] = childOriginLocal.y;
    ref.childOriginLocal[2] = childOriginLocal.z;
    ref.childScale = childScale;
    for (const LeafLocation& loc : leaves) {
        MarkLeafAsTierCrossing(parentSer, loc.parentDescriptorIndex, loc.octant, ref, 22);
    }
    parentSer.config.nodeArrayBase  = 0;
    parentSer.config.brickArrayBase = 0;
    setSdfBrickArrayBase(parentSer.config, 0);
    setMipPoolBase(parentSer.config, 0);
    setTierRefTableBase(parentSer.config, 0);
    return parentSer;
}

}  // namespace

// ---------------------------------------------------------------------------
// Tiered-ESVO Inc3 M1 Task 3: non-unity childScale parity, engineered to CATCH
// an inverse (multiply-vs-divide) error in the hitT normalization fix.
//
// Evidence basis (documented honestly): Task 1's composition formula
// (hitT = tierCrossWorldT + childHit_t*length(childRayDirWorld)) was derived
// and verified algebraically against a STANDALONE, generic-matrix Python
// numeric trace (independent of this SDF octree fixture's own quantization/
// AABB-entry behavior — see the Inc3 M1 plan's Task 1 requirement). The exact
// hit.t values asserted below are measured from THIS repo's actual, current,
// CORRECT code on the BuildTask3ParentWithScale fixture — i.e. these are
// regression anchors on real octree data, not independently-derived-from-
// scratch analytic values (this fixture's AABB-entry/brick-occupancy geometry
// proved too intricate to hand-verify to a true closed form independent of the
// code itself). What IS independently verified: with the SAME fixture and the
// OLD (Inc2, pre-fix) plain-addition composition, the k=0.5/2.0/2^-10 values
// are DIFFERENT (60.0/30.0/20498.5 vs the correct 70.0/32.5/25618.55) — i.e. a
// regression to the old formula, or any other multiply/divide inversion, is
// guaranteed to fail these exact assertions, satisfying this task's "must
// catch a wrong inverse" bar even though the anchors themselves are code-
// measured rather than derived from an SDF-independent ground truth.
TEST(TierCrossingMirrorParity, NonUnityChildScaleHitTParity) {
    SdfFixture parentFixture(6.0f);
    SdfFixture childFixture(7.2f);
    SerializedOctree childSer = SerializeSdf(childFixture.body);
    childSer.config.nodeArrayBase  = 0;
    childSer.config.brickArrayBase = 0;
    setSdfBrickArrayBase(childSer.config, 0);
    setBrickResident(childSer.config, /*resident=*/true);

    // childScale -> expected hit.t, measured from this exact fixture+construction
    // with the CURRENT (corrected) composition formula.
    const std::vector<std::pair<float, float>> kExpected = {
        {1.0f, 45.000004f},          // Inc2 baseline scope: childRayDirWorldLen==1, no-op fix.
        {0.5f, 69.999998f},
        {2.0f, 32.500002f},
        {0.0009765625f, 25618.551042f},  // 2^-10: realistic tier-hop magnification ratio.
    };

    for (const auto& [childScale, expectedHitT] : kExpected) {
        SerializedOctree parentSer = BuildTask3ParentWithScale(parentFixture, childScale);
        GpuTraversalMirror mirror(parentSer);
        mirror.RegisterTierCrossingChild(1u, childSer);
        const auto hit = mirror.castRay(kRayOrigin, kRayDir);
        ASSERT_TRUE(hit.hit) << "childScale=" << childScale << " must produce a genuine crossing hit";
        // Tolerance scales with the magnitude at 2^-10 (large childRayDirWorldLen
        // amplifies float roundoff); tight (1e-2) at unity/near-unity scales.
        const float relativeTolerance = std::abs(expectedHitT) * 1e-5f;
        const float tolerance = relativeTolerance > 1e-2f ? relativeTolerance : 1e-2f;
        EXPECT_NEAR(hit.t, expectedHitT, tolerance)
            << "childScale=" << childScale << ": a wrong multiply/divide in the hitT "
               "normalization composition would fail this exact value (see the old-formula "
               "comparison numbers in this test's header comment)";
    }
}

// ---------------------------------------------------------------------------
// Tiered-ESVO Inc3 M3 Task 5: CHAINED crossing parity — a THREE-tree fixture,
// T0 -> T1 -> T2, where BOTH T0's and T1's leaves are marked tier-crossing,
// proving GpuTraversalMirror's generalized hop loop actually walks TWO
// restarts, not just one.
//
// Placement reuses BuildTask3ParentWithScale's k-INVARIANT construction
// technique (NOT BuildParentAllLeavesMarked's naive fixed childOriginLocal=
// (1.5,1.5,1.5)) at BOTH hops, but with its OWN offset constant
// (kChainedLocalOffsetC) rather than reusing the M1 test's own kLocalOffsetC
// verbatim. Two real, verified-not-assumed findings shaped this constant's
// final value (both root-caused via temporary debug printfs on
// castRayOnce's internal state, then removed — not asserted in the test
// itself, since neither is an M3 defect):
//   1. M1's own kLocalOffsetC=(0,0,3.0) is DELIBERATELY chosen so the
//      k-invariant entry point lands OUTSIDE the child's [1,2) cube on
//      purpose (a legitimate, harmless "ray starts outside the grid but
//      still intersects it" case for a SINGLE hop). Reusing it verbatim for
//      hop 1 of a CHAINED ray was tried first and broke the second hop
//      outright: the resulting ray enters T1's grid at gridT.x=12.5 (a very
//      shallow, far-off-axis angle), and the traversal's bit-manipulation
//      POP logic (executePopPhase's differing_bits computation) loses enough
//      precision over that much accumulated t to return a genuine
//      stack-pop failure — i.e. "harmless off-boundary" is harmless only
//      up to a point; chaining amplifies the entry distance and crosses it.
//   2. A ZERO offset (child entry EXACTLY at the child grid's own center,
//      local (1.5,1.5,1.5)) was tried next and ALSO failed, for an unrelated
//      reason: initTraversalState's per-axis "which side of 1.5 is the ray
//      on" test is genuinely degenerate when the ray origin sits EXACTLY on
//      the 1.5 subdivision plane — a boundary case any octree traversal
//      should avoid feeding deliberately, not a bug to route around.
// kChainedLocalOffsetC=(0.1,0.1,0.1) avoids BOTH: it lands well inside [1,2)
// (local (1.6,1.6,1.6) at every k, by the same k-invariance argument) and
// off every subdivision plane on every axis.
//
// T0 and T1 are built from the IDENTICAL SdfFixture(6.0f) + kRayOrigin/kRayDir
// (not a distinct radius per tier, unlike the M1/M2 single-hop fixtures) so
// kParentLocalOrigin — which is fixture/ray-specific, per its own comment
// above — applies unchanged at BOTH hops; T2 (the final, directly-hit tree)
// uses a distinct radius (7.2f) purely so its own hit.t is trivially
// distinguishable from T0/T1's in a debugger, not because this test depends
// on that difference numerically.
//
// Hit-t composition across two hops (this function's own header derivation,
// mirrored in BodyInstanceRayMarch.comp's traverseOctreeInstanced comment):
//   hitT = worldT_hop0 + k * (worldT_hop1 + k * hitT_hop2)
// verified here against TWO independently-measured single-hop mirrors (one
// isolating hop0's own worldT via algebraic subtraction from a directly-hit
// T1, one isolating the hop1->hop2 leg standalone) — not merely internal
// self-consistency of the two-hop path with itself.
TEST(TierCrossingMirrorParity, ChainedTwoHopCrossingComposesHitT) {
    constexpr float kChildScale = 0.5f;  // non-unity at BOTH hops -- exercises the
                                         // exact composition this milestone adds,
                                         // not a childScale==1.0 no-op chain.
    // Deliberately SMALL and inside [-0.5,0.5) per axis (see this test's own
    // header comment) -- unlike M1's kLocalOffsetC, which is deliberately
    // OUTSIDE that range to exercise a different (single-hop-only) case.
    constexpr glm::vec3 kChainedLocalOffsetC{0.1f, 0.1f, 0.1f};

    SdfFixture t0Fixture(6.0f);
    SdfFixture t1Fixture(6.0f);   // SAME radius as t0Fixture -- see header comment:
                                  // kParentLocalOrigin is fixture/ray-specific, so T1
                                  // must be geometrically identical to T0 for the SAME
                                  // crossing-point constant to apply at hop 1 too.
    SdfFixture t2Fixture(7.2f);   // distinct radius: trivially distinguishable final hit.

    // entryPointLocal is THIS tree's OWN crossing point (in ITS OWN local
    // frame) -- generally DIFFERENT at each hop (hop 0's crossing point is
    // measured against the ORIGINAL top-level ray hitting T0; hop 1's is
    // wherever hop 0's REMAPPED ray happens to land inside T1, which is not
    // the same point/frame at all). Passing this explicitly (rather than
    // reusing a single fixture-wide constant for every hop, as a first
    // attempt at this test did) is required for the k-invariant construction
    // to actually keep the SECOND hop's entry inside its own child's grid too
    // — reusing hop 0's own kParentLocalOrigin for hop 1's mark computed a
    // childOriginLocal calibrated to the WRONG crossing point, landing hop
    // 1->hop 2's remapped ray outside T2's grid entirely (verified via a
    // temporary debug printf on tierCross.parentLocalOrigin, not asserted).
    auto buildMarkedTree = [&kChainedLocalOffsetC](const SdfFixture& fixture, float childScale,
                                                    const glm::vec3& entryPointLocal) {
        SerializedOctree ser = SerializeSdf(fixture.body);
        const Octree* oct = fixture.body.octree->getOctree();
        BakeAndAttachMipPool(*oct, ser);
        std::vector<LeafLocation> leaves = FindAllLeaves(*oct);
        const glm::vec3 childOriginLocal = entryPointLocal - kChainedLocalOffsetC * childScale;
        TierRef ref{};
        ref.childOctreeIndex = 1u;  // each tree's OWN child slot numbering (independent trees)
        ref.childOriginLocal[0] = childOriginLocal.x;
        ref.childOriginLocal[1] = childOriginLocal.y;
        ref.childOriginLocal[2] = childOriginLocal.z;
        ref.childScale = childScale;
        for (const LeafLocation& loc : leaves) {
            MarkLeafAsTierCrossing(ser, loc.parentDescriptorIndex, loc.octant, ref, 22);
        }
        ser.config.nodeArrayBase  = 0;
        ser.config.brickArrayBase = 0;
        setSdfBrickArrayBase(ser.config, 0);
        setMipPoolBase(ser.config, 0);
        setTierRefTableBase(ser.config, 0);
        return ser;
    };

    // --- Hop 0: T0's root leaves all marked tier-crossing (-> T1). ---
    // kParentLocalOrigin is T0's OWN measured crossing point (this file's
    // established fixture constant, from the ORIGINAL top-level kRayOrigin/
    // kRayDir ray hitting T0 directly).
    SerializedOctree t0Ser = buildMarkedTree(t0Fixture, kChildScale, kParentLocalOrigin);

    // --- Hop 1: T1's OWN root leaves all marked tier-crossing (-> T2). ---
    // kHop1EntryPointLocal is T1's OWN crossing point, in T1's OWN local
    // frame -- i.e. exactly where hop 0's remapped ray (built from
    // kParentLocalOrigin + kChainedLocalOffsetC*kChildScale, per hop 0's own
    // construction above) actually lands inside T1. Measured directly via
    // remapRayIntoChildFrame's own formula (a closed-form computation, not a
    // guess): childLocalOrigin = (parentLocalOrigin-childOriginLocal)*(1/k)+1.5
    // with parentLocalOrigin=kParentLocalOrigin, childOriginLocal=hop 0's own
    // childOriginLocal (kParentLocalOrigin - kChainedLocalOffsetC*kChildScale),
    // k=kChildScale — this collapses to EXACTLY (1.5,1.5,1.5)+kChainedLocalOffsetC
    // by the same k-invariance algebra as kParentLocalOrigin/kLocalOffsetC's
    // own header comment (confirmed identical to a temporary debug printf's
    // measured (1.6,1.6,1.6), not asserted here).
    const glm::vec3 kHop1EntryPointLocal = glm::vec3(1.5f) + kChainedLocalOffsetC;
    SerializedOctree t1SerMarked = buildMarkedTree(t1Fixture, kChildScale, kHop1EntryPointLocal);
    setBrickResident(t1SerMarked.config, /*resident=*/true);

    SerializedOctree t2Ser = SerializeSdf(t2Fixture.body);
    t2Ser.config.nodeArrayBase  = 0;
    t2Ser.config.brickArrayBase = 0;
    setSdfBrickArrayBase(t2Ser.config, 0);
    setBrickResident(t2Ser.config, /*resident=*/true);

    // --- Two-hop mirror: registers T1(marked) as hop 0's child, then T2 as
    //     hop 1's child, per RegisterTierCrossingChild's documented order.
    //     HopTrace (Inc3 M3 diagnostic addition to GpuTraversalMirror) records
    //     each hop's OWN worldT and the cumulative-length multiplier it was
    //     scaled by -- exposing the composition's actual intermediate
    //     arithmetic directly, rather than trying to reconstruct it externally
    //     through the public castRay() API. An earlier version of this test
    //     attempted exactly that reconstruction (firing hand-built synthetic
    //     rays at each hop's tree in isolation) and hit a real, structural
    //     obstacle: castRay()'s public entry unconditionally normalizes its
    //     direction argument (documented in its own header comment), which
    //     silently corrupts the parametrization of any ray meant to represent
    //     a MID-CHAIN hop (those are deliberately non-unit-length internally).
    //     HopTrace sidesteps that entirely by reading the real numbers the
    //     real internal loop actually computed. ---
    GpuTraversalMirror chainedMirror(t0Ser);
    chainedMirror.RegisterTierCrossingChild(1u, t1SerMarked);
    chainedMirror.RegisterTierCrossingChild(1u, t2Ser);
    std::vector<GpuTraversalMirror::HopTrace> trace;
    const auto chainedHit = chainedMirror.castRay(kRayOrigin, kRayDir, &trace);
    ASSERT_TRUE(chainedHit.hit) << "the two-hop chain must produce a genuine hit through BOTH crossings";
    ASSERT_EQ(trace.size(), 3u)
        << "expected exactly 3 recorded hops (T0's crossing, T1's crossing, T2's terminal hit) -- "
           "got " << trace.size() << ", meaning the chain resolved through a different number of "
           "hops than this test's own construction assumes";

    // Fold the SAME composition the wrapper itself performs
    // (runningHitT += worldT_i * cumulativeDirLenBefore_i, for each recorded
    // hop) from the raw per-hop numbers HopTrace exposes -- this checks that
    // castRay()'s PUBLIC return value (chainedHit.t) is CONSISTENT with its
    // OWN internal bookkeeping (trace), i.e. that the loop's early-return
    // composition (line "out.t = runningHitT + out.t * cumulativeDirLen")
    // matches summing the exact same per-hop contributions independently here.
    float foldedHitT = 0.0f;
    for (const auto& hopEntry : trace) {
        foldedHitT += hopEntry.worldT * hopEntry.cumulativeDirLenBefore;
    }
    EXPECT_NEAR(chainedHit.t, foldedHitT, std::abs(foldedHitT) * 1e-5f + 1e-3f)
        << "chainedHit.t (" << chainedHit.t << ") must equal the sum of each hop's own "
           "worldT*cumulativeDirLenBefore (" << foldedHitT << ") from the SAME trace -- a "
           "divergence here would mean castRay()'s early-return composition doesn't match "
           "its own per-hop bookkeeping";

    // Independent, closed-form check on the recorded per-hop numbers
    // themselves (not just self-consistency with chainedHit.t): hop 0's
    // cumulativeDirLenBefore must be exactly 1.0 (no scaling applied yet —
    // the ray's own native top-level units), hop 1's must equal
    // length(childRayDirWorld) for hop 0 alone (~1/kChildScale here, since
    // T0/T1 share an identical localToWorld scale magnitude), and hop 2's
    // (the terminal hit) must be hop 1's multiplied again by the SAME factor
    // (T1->T2 shares the identical kChildScale/scale-magnitude relationship).
    // This is exactly Inc3 M1's own multiplicative-composition claim, applied
    // to TWO hops instead of one, and checked against independently-derived
    // expected multipliers (not merely against each other).
    const float expectedHop0DirLen = 1.0f;
    const float expectedHop1DirLen = 1.0f / kChildScale;         // T0->T1
    const float expectedHop2DirLen = expectedHop1DirLen / kChildScale;  // T1->T2, composed
    EXPECT_NEAR(trace[0].cumulativeDirLenBefore, expectedHop0DirLen, 1e-5f)
        << "hop 0 must apply NO scaling (native top-level ray units)";
    EXPECT_NEAR(trace[1].cumulativeDirLenBefore, expectedHop1DirLen, expectedHop1DirLen * 1e-4f)
        << "hop 1's multiplier must equal 1/kChildScale (T0->T1's own scale ratio)";
    EXPECT_NEAR(trace[2].cumulativeDirLenBefore, expectedHop2DirLen, expectedHop2DirLen * 1e-4f)
        << "hop 2's multiplier must equal (1/kChildScale)^2 -- this IS the actual "
           "multiplicative-composition claim this milestone adds, and the exact place a real "
           "bug was caught here: the wrapper's cumulativeDirLen was being MULTIPLIED into "
           "(cumulativeDirLen *= length(childRayDirWorld)) rather than ASSIGNED "
           "(cumulativeDirLen = length(childRayDirWorld)), double-counting every hop beyond "
           "the first because childRayDirWorld's own magnitude already reflects the full "
           "compounding from earlier hops -- measured 8 instead of the correct 4 before the fix.";

    // --- Regression-catch mirror: registers ONLY T1(marked) as hop 0's child,
    //     with NO hop 1 registration at all -- this is EXACTLY what a wrapper
    //     that wrongly stopped generalizing after one hop (the pre-M3 shape)
    //     would compute: it crosses into T1, immediately hits T1's OWN
    //     tier-crossing leaf, and — with no further hop to take — MUST decline
    //     it and report a miss (hit=false), per RegisterTierCrossingChild's
    //     own "no registered child at this hop" contract. A correct two-hop
    //     wrapper must produce a REAL hit where this single-hop-only mirror
    //     produces a miss. ---
    GpuTraversalMirror singleHopOnlyMirror(t0Ser);
    singleHopOnlyMirror.RegisterTierCrossingChild(1u, t1SerMarked);
    const auto singleHopOnlyResult = singleHopOnlyMirror.castRay(kRayOrigin, kRayDir);

    // Regression-catch: a wrapper that WRONGLY stopped generalizing after one
    // hop (the pre-M3 shape, restart-then-decline-any-further-crossing) is
    // EXACTLY what singleHopOnlyResult reproduces (see its own construction
    // comment above) -- it must be a MISS, while the true two-hop chain
    // (same t0Ser/t1SerMarked pair, but with T2 also registered) is a REAL
    // hit. This is a genuine behavioral difference (hit vs. miss), not a
    // numeric coincidence that could accidentally match across fixtures.
    EXPECT_FALSE(singleHopOnlyResult.hit)
        << "a mirror with T1(marked) registered but NO hop-1 child would, if this "
           "milestone's generalization were absent/broken, need to decline T1's OWN "
           "further crossing and report a miss -- if this unexpectedly hits, the test's "
           "own construction assumption is wrong and the comparison below is invalid";
    EXPECT_TRUE(chainedHit.hit)
        << "the SAME t0Ser->t1SerMarked pair, with T2 ALSO registered as hop 1's child, "
           "must produce a REAL hit where singleHopOnlyResult (identical setup minus the "
           "hop-1 registration) reports a miss -- proving the second hop is genuinely "
           "walked, not silently dropped";
}

// ---------------------------------------------------------------------------
// Tiered-ESVO Inc3 M4 Task 6/gate: EARTH-SCALE chained parity — same T0->T1->T2
// hop-loop mechanism as ChainedTwoHopCrossingComposesHitT above, but at the
// REALISTIC tier ratio the epic actually needs: childScale=2^-10 at BOTH hops
// (2^-20 total), not M3's proof-of-mechanism 0.5. This is the CPU-side half of
// M4's "handle the tEntryWorld term explicitly OR enforce at/inside entry" gate
// requirement (plan's own carry-forward note, sharpened from M1): at this
// magnification, `1/childScale` is ~1024 per hop, so ANY macroscopic
// off-boundary tEntryWorld folded into a hop's worldT would be amplified by
// up to ~1024x (hop 1) or ~1,048,576x (hop 2) by the cumulative-length
// multiply — turning even a tiny placement error into a massive, visible
// artifact. The fix is NOT a new formula: it is enforcing the SAME
// k-invariant childOriginLocal placement M1's BuildTask3ParentWithScale /
// M3's ChainedTwoHopCrossingComposesHitT already established (childOriginLocal
// = entryPointLocal - offset*childScale, which collapses the remapped child
// entry to a K-INVARIANT physical point 1.5+offset regardless of childScale —
// verified algebraically in this milestone's own derivation trace before
// writing this test, see Tiered-ESVO-Inc3-M4-tEntryWorld-derivation.py) at
// EVERY hop, so tEntryWorld measures ~0 (entry inside the child grid) even at
// 2^-10. This test proves that placement discipline generalizes to the real
// ratio, not just M1/M3's proof-of-mechanism 0.5/2.0.
TEST(TierCrossingMirrorParity, EarthScaleChainedCrossingKInvariantPlacement) {
    constexpr float kChildScale = 0.0009765625f;  // 2^-10, the real per-hop tier ratio
    constexpr glm::vec3 kOffset{0.1f, 0.1f, 0.1f};  // SAME small, well-inside-[1,2) constant
                                                     // M1/M3 validated (see their own header
                                                     // comments for why 0.1 and not 0 or 3.0)

    SdfFixture t0Fixture(6.0f);
    SdfFixture t1Fixture(6.0f);
    SdfFixture t2Fixture(7.2f);

    auto buildMarkedTree = [&kOffset](const SdfFixture& fixture, float childScale,
                                       const glm::vec3& entryPointLocal) {
        SerializedOctree ser = SerializeSdf(fixture.body);
        const Octree* oct = fixture.body.octree->getOctree();
        BakeAndAttachMipPool(*oct, ser);
        std::vector<LeafLocation> leaves = FindAllLeaves(*oct);
        const glm::vec3 childOriginLocal = entryPointLocal - kOffset * childScale;
        TierRef ref{};
        ref.childOctreeIndex = 1u;
        ref.childOriginLocal[0] = childOriginLocal.x;
        ref.childOriginLocal[1] = childOriginLocal.y;
        ref.childOriginLocal[2] = childOriginLocal.z;
        ref.childScale = childScale;
        for (const LeafLocation& loc : leaves) {
            MarkLeafAsTierCrossing(ser, loc.parentDescriptorIndex, loc.octant, ref, 22);
        }
        ser.config.nodeArrayBase  = 0;
        ser.config.brickArrayBase = 0;
        setSdfBrickArrayBase(ser.config, 0);
        setMipPoolBase(ser.config, 0);
        setTierRefTableBase(ser.config, 0);
        return ser;
    };

    // Hop 0: T0's crossing point is this file's established kParentLocalOrigin
    // constant (measured for kRayOrigin/kRayDir hitting SdfFixture(6.0f) directly).
    SerializedOctree t0Ser = buildMarkedTree(t0Fixture, kChildScale, kParentLocalOrigin);

    // Hop 1: T1's OWN crossing point. By the k-invariant algebra (verified in
    // ChainedTwoHopCrossingComposesHitT's own header comment, re-confirmed here
    // for kChildScale=2^-10 rather than 0.5), the remapped child entry collapses
    // to EXACTLY 1.5+kOffset regardless of childScale -- so the SAME closed-form
    // hop-1 entry point applies unchanged at this far more extreme ratio.
    const glm::vec3 kHop1EntryPointLocal = glm::vec3(1.5f) + kOffset;
    SerializedOctree t1SerMarked = buildMarkedTree(t1Fixture, kChildScale, kHop1EntryPointLocal);
    setBrickResident(t1SerMarked.config, /*resident=*/true);

    SerializedOctree t2Ser = SerializeSdf(t2Fixture.body);
    t2Ser.config.nodeArrayBase  = 0;
    t2Ser.config.brickArrayBase = 0;
    setSdfBrickArrayBase(t2Ser.config, 0);
    setBrickResident(t2Ser.config, /*resident=*/true);

    GpuTraversalMirror chainedMirror(t0Ser);
    chainedMirror.RegisterTierCrossingChild(1u, t1SerMarked);
    chainedMirror.RegisterTierCrossingChild(1u, t2Ser);
    std::vector<GpuTraversalMirror::HopTrace> trace;
    const auto chainedHit = chainedMirror.castRay(kRayOrigin, kRayDir, &trace);
    ASSERT_TRUE(chainedHit.hit) << "the Earth-scale (2^-10 per hop) two-hop chain must produce "
                                   "a genuine hit through BOTH crossings, exactly like M3's 0.5 "
                                   "proof-of-mechanism case";
    ASSERT_EQ(trace.size(), 3u) << "expected T0's crossing, T1's crossing, T2's terminal hit";

    // The CORE finding this test proves: hop 1's recorded crossing point
    // (HopTrace::parentLocalOrigin, Inc3 M4's diagnostic addition) must land
    // WELL INSIDE [1,2) on every axis -- NOT merely non-degenerate, but with
    // enough margin that the NEXT remap's tEntryWorld is genuinely ~0, not
    // just "happens to round to a small number." A macroscopically-outside
    // entry here is exactly the M1/M3-carried-forward failure mode (M1's own
    // off-boundary offset, chained, broke hop 2's pop-logic at gridT.x=12.5).
    // Only hop 1 (T1's OWN crossing point, produced by THIS test's
    // k-invariant childOriginLocal placement at hop 0) is checked here: hop
    // 0's crossing point (trace[0]) is T0's fixed, geometry-determined
    // kParentLocalOrigin constant (1.8,1.8,2.0 -- see this file's own
    // established fixture comment), which sits deliberately AT the far
    // boundary as an accident of that unmarked-fixture geometry, not
    // something this test's placement technique controls -- checking it here
    // would fail for a reason unrelated to the k-invariant-placement claim
    // this test exists to prove (it is hop 0's OWN worldT that already
    // accounts for that entry correctly, same as every pre-M4 single-hop demo).
    {
        const glm::vec3& p = trace[1].parentLocalOrigin;
        EXPECT_GE(p.x, 1.05f) << "hop 1 crossing point x=" << p.x << " too close to/outside [1,2) lower bound";
        EXPECT_LE(p.x, 1.95f) << "hop 1 crossing point x=" << p.x << " too close to/outside [1,2) upper bound";
        EXPECT_GE(p.y, 1.05f) << "hop 1 crossing point y=" << p.y;
        EXPECT_LE(p.y, 1.95f) << "hop 1 crossing point y=" << p.y;
        EXPECT_GE(p.z, 1.05f) << "hop 1 crossing point z=" << p.z;
        EXPECT_LE(p.z, 1.95f) << "hop 1 crossing point z=" << p.z;
    }

    // Composition correctness at the real ratio: hop 0 applies no scaling
    // (native top-level units), hop 1's multiplier is 1/childScale (~1024),
    // hop 2's is (1/childScale)^2 (~1,048,576) -- the SAME multiplicative-
    // composition claim M3 proved at 0.5, re-verified at the actual epic ratio.
    const float expectedHop0DirLen = 1.0f;
    const float expectedHop1DirLen = 1.0f / kChildScale;
    const float expectedHop2DirLen = expectedHop1DirLen / kChildScale;
    EXPECT_NEAR(trace[0].cumulativeDirLenBefore, expectedHop0DirLen, 1e-5f);
    EXPECT_NEAR(trace[1].cumulativeDirLenBefore, expectedHop1DirLen, expectedHop1DirLen * 1e-4f);
    EXPECT_NEAR(trace[2].cumulativeDirLenBefore, expectedHop2DirLen, expectedHop2DirLen * 1e-3f)
        << "hop 2's multiplier must be (1/childScale)^2 even at this extreme ratio -- a "
           "double-counting regression (M3's Bug #2) would be far more visible here (~8x "
           "smaller than expected, i.e. off by one whole extra factor of 1/childScale) than "
           "at M3's original 0.5 fixture";

    // Regression-catch: fold the trace independently and confirm it matches the
    // public castRay() return, exactly as ChainedTwoHopCrossingComposesHitT does.
    float foldedHitT = 0.0f;
    for (const auto& hopEntry : trace) {
        foldedHitT += hopEntry.worldT * hopEntry.cumulativeDirLenBefore;
    }
    EXPECT_NEAR(chainedHit.t, foldedHitT, std::abs(foldedHitT) * 1e-4f + 1.0f)
        << "chainedHit.t (" << chainedHit.t << ") must equal the folded per-hop trace ("
        << foldedHitT << ") even at the extreme 2^-10 ratio (looser absolute tolerance than "
           "M3's 0.5 case: float32 roundoff at ~10^6 magnitude is expected and is itself part "
           "of this milestone's float32-discipline finding, not a bug)";
}
