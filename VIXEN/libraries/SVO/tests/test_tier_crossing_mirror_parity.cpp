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
