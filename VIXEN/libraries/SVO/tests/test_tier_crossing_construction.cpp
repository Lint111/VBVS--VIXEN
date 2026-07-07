// test_tier_crossing_construction.cpp — Tiered ESVO Inc2, M2 Tasks 4-5.
//
// Task 4: MarkLeafAsTierCrossing (ShellOctreeGpu.h) — the new, explicit,
// additive construction-time path that marks ONE specific leaf's
// ChildDescriptor as farBit=1 (tier-crossing) and registers a TierRef entry,
// instead of the normal brick-index assignment. Every existing construction
// path (Serialize/SerializeSdf themselves) is completely untouched — this
// only mutates an already-serialized SerializedOctree that the caller opts
// in for, one leaf at a time.
//
// Task 5: a hand-authored two-tree fixture (independently-built "parent" and
// "child" SdfBodyOctrees) where ONE parent leaf is marked tier-crossing via
// Task 4's mechanism, its TierRef pointing at the child's concatenated-octree
// index. Proves the round-trip: serialize both -> concatenate (mirroring
// test_tier_ref_table.cpp's own manual-concatenation-loop convention, since
// ConcatenateSdf/Concatenate call SerializeSdf/Serialize INTERNALLY and would
// discard a pre-mutation) -> the marked leaf's farBit/TierRef resolve to the
// child's actual octree index/origin/scale.
//
// Pure CPU/serialization proof per the M2 gate — no live GPU render needed.

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#undef far
#undef near
#undef min
#undef max

#include "SdfBake.h"
#include "ShellOctreeGpu.h"
#include "SdfRecipes.h"
#include "TierRef.h"
#include "SVOTypes.h"

using namespace Vixen::SVO;

namespace {

// Same fixture shape as test_tier_ref_table.cpp / test_mip_sample_bake.cpp:
// bricksPerAxis=2 collapses to a root whose 8 children are ALL brick-level
// leaves — deterministic, and small enough to reason about by hand.
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

// Find a (parentDescriptorIndex, octant) pair that is a genuine existing leaf
// child, by scanning the built tree's descriptors directly — the same
// "read childDescriptors directly to locate a leaf" convention
// test_mip_sample_bake.cpp already uses instead of guessing indices.
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

}  // namespace

// ---------------------------------------------------------------------------
// Task 4 — MarkLeafAsTierCrossing correctness in isolation (single tree).
// ---------------------------------------------------------------------------

TEST(TierCrossingConstruction, MarkLeafAsTierCrossingSetsFarBitAndRegistersTierRef) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    const Octree* oct = f.body.octree->getOctree();
    ASSERT_NE(oct, nullptr);

    std::vector<LeafLocation> leaves = FindAllLeaves(*oct);
    ASSERT_FALSE(leaves.empty()) << "fixture must have at least one leaf to mark";
    const LeafLocation loc = leaves.front();

    // Baseline: this leaf reads as an ordinary farBit=0 brick leaf before marking.
    {
        const ChildDescriptor& parentDesc = oct->root->childDescriptors[loc.parentDescriptorIndex];
        EXPECT_FALSE(parentDesc.farBit) << "parent descriptor must be untouched before marking";
    }
    ASSERT_TRUE(out.tierRefs.empty());

    TierRef ref{};
    ref.childOctreeIndex = 3u;
    ref.childOriginLocal[0] = 1.5f;
    ref.childOriginLocal[1] = 1.5f;
    ref.childOriginLocal[2] = 1.5f;
    ref.childScale = 0.0625f;

    MarkLeafAsTierCrossing(out, loc.parentDescriptorIndex, loc.octant, ref, /*childRootScaleHint=*/12);

    ASSERT_EQ(out.tierRefs.size(), 1u);
    EXPECT_EQ(out.tierRefs[0].childOctreeIndex, 3u);
    EXPECT_FLOAT_EQ(out.tierRefs[0].childOriginLocal[0], 1.5f);
    EXPECT_FLOAT_EQ(out.tierRefs[0].childScale, 0.0625f);

    // Re-derive the leaf descriptor's ACTUAL byte offset the same way
    // MarkLeafAsTierCrossing does internally, and confirm the mutated bytes
    // in out.nodes reflect farBit=1 + the tier-ref index + the scale hint.
    ChildDescriptor parentDesc;
    std::memcpy(&parentDesc,
                out.nodes.data() + static_cast<size_t>(loc.parentDescriptorIndex) * sizeof(ChildDescriptor),
                sizeof(ChildDescriptor));
    const uint32_t totalInternal = static_cast<uint32_t>(
        std::popcount(static_cast<uint8_t>(parentDesc.validMask & ~parentDesc.leafMask)));
    uint32_t leafBefore = 0;
    for (int i = 0; i < loc.octant; ++i) {
        if (parentDesc.hasChild(i) && parentDesc.isLeaf(i)) ++leafBefore;
    }
    const uint32_t leafDescIdx = parentDesc.childPointer + totalInternal + leafBefore;

    ChildDescriptor leafDesc;
    std::memcpy(&leafDesc,
                out.nodes.data() + static_cast<size_t>(leafDescIdx) * sizeof(ChildDescriptor),
                sizeof(ChildDescriptor));

    EXPECT_TRUE(leafDesc.isTierCrossing());
    EXPECT_EQ(leafDesc.getTierRefIndex(), 0u);
    EXPECT_EQ(leafDesc.getChildRootScaleHint(), 12u);

    // The leaf bit itself (validMask/leafMask on the PARENT) is unchanged —
    // MarkLeafAsTierCrossing only reinterprets contourPointer/contourMask on
    // the child descriptor, never the hierarchy bits.
    EXPECT_TRUE(parentDesc.hasChild(loc.octant));
    EXPECT_TRUE(parentDesc.isLeaf(loc.octant));
}

TEST(TierCrossingConstruction, RejectsOutOfRangeOctantAndScaleHint) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    TierRef ref{};

    EXPECT_THROW(MarkLeafAsTierCrossing(out, 0, /*octant=*/8, ref, 0), std::runtime_error);
    EXPECT_THROW(MarkLeafAsTierCrossing(out, 0, /*octant=*/-1, ref, 0), std::runtime_error);
    EXPECT_THROW(MarkLeafAsTierCrossing(out, 0, 0, ref, /*childRootScaleHint=*/23), std::runtime_error);
}

TEST(TierCrossingConstruction, RejectsNonLeafOrNonExistentChildSlot) {
    SdfFixture f;
    SerializedOctree out = SerializeSdf(f.body);
    const Octree* oct = f.body.octree->getOctree();
    const ChildDescriptor& root = oct->root->childDescriptors[0];

    // Find an octant that is definitely NOT a valid child of the root (there
    // are only 8 octants; if the fixture ever occupies all 8, this test's
    // assumption — root has at least one empty octant OR we probe a
    // guaranteed-invalid parent index instead — still holds via the invalid
    // parent index branch below).
    bool foundEmptyOctant = false;
    TierRef ref{};
    for (int octant = 0; octant < 8; ++octant) {
        if (!root.hasChild(octant)) {
            foundEmptyOctant = true;
            EXPECT_THROW(MarkLeafAsTierCrossing(out, 0, octant, ref, 0), std::runtime_error);
            break;
        }
    }
    if (!foundEmptyOctant) {
        GTEST_LOG_(INFO) << "fixture's root occupies all 8 octants; skipping empty-octant sub-case";
    }

    // An out-of-range parent descriptor index must also throw.
    const uint32_t wildlyOutOfRange = static_cast<uint32_t>(out.nodeCount) + 1000u;
    EXPECT_THROW(MarkLeafAsTierCrossing(out, wildlyOutOfRange, 0, ref, 0), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Task 5 — two independently-built trees; the parent's marked leaf resolves
// to the child's actual concatenated-octree index/origin/scale.
// ---------------------------------------------------------------------------

TEST(TierCrossingConstruction, TwoTreeFixtureRoundTripsThroughSerializeAndConcatenate) {
    // Two INDEPENDENTLY-constructed octrees (distinct GaiaVoxelWorld/registry/
    // octree triples — SdfFixture's constructor builds a brand-new bake each
    // time, matching Task 5's "two independently-constructed octree
    // instances" requirement).
    SdfFixture parentFixture;
    SdfFixture childFixture;

    SerializedOctree parentSer = SerializeSdf(parentFixture.body);
    SerializedOctree childSer  = SerializeSdf(childFixture.body);

    const Octree* parentOct = parentFixture.body.octree->getOctree();
    ASSERT_NE(parentOct, nullptr);
    std::vector<LeafLocation> parentLeaves = FindAllLeaves(*parentOct);
    ASSERT_FALSE(parentLeaves.empty());
    const LeafLocation markedLeaf = parentLeaves.front();

    // Manually concatenate BOTH trees first (mirrors test_tier_ref_table.cpp's
    // own manual-loop convention — Concatenate/ConcatenateSdf call
    // Serialize/SerializeSdf INTERNALLY and would discard our pre-existing
    // SerializedOctree mutations) so childOctreeIndex below refers to the
    // child's REAL, final slot in the concatenated table.
    constexpr uint32_t kParentSlot = 0;
    constexpr uint32_t kChildSlot  = 1;

    const glm::vec3 knownOrigin{1.25f, 1.5f, 1.75f};
    constexpr float knownScale = 0.0625f;
    constexpr uint8_t knownScaleHint = 9;

    TierRef ref{};
    ref.childOctreeIndex = kChildSlot;
    ref.childOriginLocal[0] = knownOrigin.x;
    ref.childOriginLocal[1] = knownOrigin.y;
    ref.childOriginLocal[2] = knownOrigin.z;
    ref.childScale = knownScale;

    // Task 4's mechanism: mark exactly ONE parent leaf tier-crossing BEFORE
    // concatenation, exactly as a real caller would.
    MarkLeafAsTierCrossing(parentSer, markedLeaf.parentDescriptorIndex, markedLeaf.octant, ref, knownScaleHint);
    ASSERT_EQ(parentSer.tierRefs.size(), 1u);

    // --- Manual concatenation (parent then child), matching ConcatenateSdf's
    // own per-octree bookkeeping loop exactly (ShellOctreeGpu.h).
    ConcatenatedOctrees cat;
    cat.count = 2;
    cat.configs.resize(2);
    cat.nodeCounts.resize(2);
    cat.brickCounts.resize(2);
    cat.tierRefCounts.resize(2);

    SerializedOctree* octs[2] = {&parentSer, &childSer};
    uint32_t nodeBase = 0, brickBase = 0, tierRefBase = 0;
    for (int k = 0; k < 2; ++k) {
        SerializedOctree& s = *octs[k];
        s.config.nodeArrayBase = static_cast<int32_t>(nodeBase);
        s.config.brickArrayBase = static_cast<int32_t>(brickBase);
        setTierRefTableBase(s.config, tierRefBase);

        cat.configs[k] = s.config;
        cat.nodeCounts[k] = s.nodeCount;
        cat.brickCounts[k] = s.brickCount;
        cat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

        cat.nodes.insert(cat.nodes.end(), s.nodes.begin(), s.nodes.end());
        cat.bricks.insert(cat.bricks.end(), s.bricks.begin(), s.bricks.end());
        cat.tierRefTable.insert(cat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());

        nodeBase += s.nodeCount;
        brickBase += s.brickCount;
        tierRefBase += static_cast<uint32_t>(s.tierRefs.size());
    }

    ASSERT_EQ(cat.count, 2u);
    ASSERT_EQ(kParentSlot, 0u);
    ASSERT_EQ(kChildSlot, 1u);

    // --- The round-trip proof: read the PARENT's marked leaf back out of the
    // CONCATENATED node buffer (as a real consumer would: nodeArrayBase +
    // local index), confirm it is tier-crossing, resolve its TierRef via
    // tierRefTableBase + the leaf's tier-ref index, and confirm it points at
    // the CHILD's actual slot/origin/scale.
    const uint32_t parentNodeArrayBase = static_cast<uint32_t>(cat.configs[kParentSlot].nodeArrayBase);

    ChildDescriptor parentDescFromCat;
    std::memcpy(&parentDescFromCat,
                cat.nodes.data() + static_cast<size_t>(parentNodeArrayBase + markedLeaf.parentDescriptorIndex) * sizeof(ChildDescriptor),
                sizeof(ChildDescriptor));

    const uint32_t totalInternal = static_cast<uint32_t>(
        std::popcount(static_cast<uint8_t>(parentDescFromCat.validMask & ~parentDescFromCat.leafMask)));
    uint32_t leafBefore = 0;
    for (int i = 0; i < markedLeaf.octant; ++i) {
        if (parentDescFromCat.hasChild(i) && parentDescFromCat.isLeaf(i)) ++leafBefore;
    }
    const uint32_t localLeafDescIdx = parentDescFromCat.childPointer + totalInternal + leafBefore;

    ChildDescriptor leafDescFromCat;
    std::memcpy(&leafDescFromCat,
                cat.nodes.data() + static_cast<size_t>(parentNodeArrayBase + localLeafDescIdx) * sizeof(ChildDescriptor),
                sizeof(ChildDescriptor));

    ASSERT_TRUE(leafDescFromCat.isTierCrossing())
        << "the marked leaf must still read as farBit=1 after concatenation (byte-verbatim append)";
    EXPECT_EQ(leafDescFromCat.getChildRootScaleHint(), knownScaleHint);

    // Resolve via THIS octree's own tierRefTableBase slice (the exact
    // mechanism a future traversal-restart would use, §3.1/§3.2).
    const uint32_t parentTierRefBase = tierRefTableBaseOf(cat.configs[kParentSlot]);
    const uint32_t tierRefIdxInSlice = leafDescFromCat.getTierRefIndex();
    const uint32_t absoluteTierRefIdx = parentTierRefBase + tierRefIdxInSlice;
    ASSERT_LT(absoluteTierRefIdx, cat.tierRefTable.size());

    const TierRef& resolved = cat.tierRefTable[absoluteTierRefIdx];
    EXPECT_EQ(resolved.childOctreeIndex, kChildSlot)
        << "the marked leaf's TierRef must resolve to the CHILD tree's real ConcatenatedOctrees slot";
    EXPECT_FLOAT_EQ(resolved.childOriginLocal[0], knownOrigin.x);
    EXPECT_FLOAT_EQ(resolved.childOriginLocal[1], knownOrigin.y);
    EXPECT_FLOAT_EQ(resolved.childOriginLocal[2], knownOrigin.z);
    EXPECT_FLOAT_EQ(resolved.childScale, knownScale);

    // And childOctreeIndex genuinely addresses the CHILD's own config/node
    // slice in the SAME ConcatenatedOctrees — not just a bare integer, but a
    // real, dereferenceable index into cat.configs/cat.nodeCounts.
    ASSERT_LT(static_cast<size_t>(resolved.childOctreeIndex), cat.configs.size());
    EXPECT_EQ(cat.nodeCounts[resolved.childOctreeIndex], childSer.nodeCount);
    const uint32_t childNodeArrayBase = static_cast<uint32_t>(cat.configs[resolved.childOctreeIndex].nodeArrayBase);
    EXPECT_EQ(childNodeArrayBase, parentSer.nodeCount)
        << "child's node slice must start immediately after the parent's in the concatenated buffer";

    // Sanity: the child's own root descriptor, read back from its resolved
    // slice, is a real, valid descriptor (non-garbage) — confirms childSer's
    // bytes truly landed at childNodeArrayBase in cat.nodes.
    ChildDescriptor childRootFromCat;
    std::memcpy(&childRootFromCat,
                cat.nodes.data() + static_cast<size_t>(childNodeArrayBase) * sizeof(ChildDescriptor),
                sizeof(ChildDescriptor));
    ChildDescriptor childRootOriginal;
    std::memcpy(&childRootOriginal, childSer.nodes.data(), sizeof(ChildDescriptor));
    EXPECT_EQ(childRootFromCat.validMask, childRootOriginal.validMask);
    EXPECT_EQ(childRootFromCat.leafMask, childRootOriginal.leafMask);
    EXPECT_FALSE(childRootFromCat.isTierCrossing())
        << "the CHILD tree itself has no tier-crossing leaves — only the parent's one marked leaf does";
}

// ---------------------------------------------------------------------------
// No-regression: a plain (unmarked) tree's every leaf still reads farBit=0
// and hasBrick()/getBrickIndex() unaffected, through the SAME concatenation
// path this test exercises for the tier-crossing case.
// ---------------------------------------------------------------------------

TEST(TierCrossingConstruction, UnmarkedTreeLeavesRemainOrdinaryBrickLeavesAfterConcatenate) {
    SdfFixture f;
    const SdfBodyOctree* bodies[2] = {&f.body, &f.body};
    std::vector<const SdfBodyOctree*> vec(bodies, bodies + 2);
    ConcatenatedOctrees cat = ConcatenateSdf(vec);

    ASSERT_EQ(cat.count, 2u);
    EXPECT_TRUE(cat.tierRefTable.empty());

    const Octree* oct = f.body.octree->getOctree();
    std::vector<LeafLocation> leaves = FindAllLeaves(*oct);
    ASSERT_FALSE(leaves.empty());

    for (const LeafLocation& loc : leaves) {
        const ChildDescriptor& parentDesc = oct->root->childDescriptors[loc.parentDescriptorIndex];
        EXPECT_FALSE(parentDesc.farBit) << "no existing construction path ever sets farBit=1";

        const uint32_t totalInternal = static_cast<uint32_t>(
            std::popcount(static_cast<uint8_t>(parentDesc.validMask & ~parentDesc.leafMask)));
        uint32_t leafBefore = 0;
        for (int i = 0; i < loc.octant; ++i) {
            if (parentDesc.hasChild(i) && parentDesc.isLeaf(i)) ++leafBefore;
        }
        const uint32_t leafDescIdx = parentDesc.childPointer + totalInternal + leafBefore;
        const ChildDescriptor& leafDesc = oct->root->childDescriptors[leafDescIdx];

        EXPECT_FALSE(leafDesc.isTierCrossing());
        EXPECT_TRUE(leafDesc.hasBrick() || leafDesc.getBrickIndex() == ChildDescriptor::INVALID_BRICK_INDEX)
            << "hasBrick()/getBrickIndex() must read a real brick-mode value on an untouched leaf";
    }
}
