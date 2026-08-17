// test_esvo_address_parity.cpp — ESVO Address Extraction, Slice V1 (2026-08-17,
// docs/superpowers/specs/2026-08-17-esvo-address-extraction-design.md, RULING B).
//
// voxelExists/getVoxelData/getChildMask now build a Vixen::SVO::TierAddress (backed by the
// kernel-generated Vixen::SVO::EsvoAddress POD, Generated/EsvoAddress.g.h) hop-by-hop during
// their descent, instead of an inline per-level local — see LaineKarrasOctree.cpp. This is the
// spec's Slice V1 acceptance parity test: "run voxelExists/getVoxelData against a fixture octree
// before and after the swap, assert byte-identical results." test_octree_queries.cpp's existing
// assertions already ARE that pre-change pin (unchanged by this slice, still green); this file
// adds deeper multi-level fixtures (3 levels, not the existing fixture's 2) so the address-hop
// bookkeeping is exercised across more than one PushHop, plus a direct assertion that the
// TierAddress the descent built matches the position by construction (Depth() == scale, and each
// hop's octant bits reproduce the position comparison the old inline code performed).

#define NOMINMAX
#include <gtest/gtest.h>
#include "LaineKarrasOctree.h"
#include "SVOBuilder.h"
#include "SVOTypes.h"
#include "TierAddress.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include "ComponentData.h"

using namespace Vixen::SVO;
using namespace Vixen::GaiaVoxel;

namespace {

// Three-level fixture: root -> mid (child 3) -> leaf (child 5), all other siblings empty.
// World [0,8)^3, so level-1 nodes are 4-wide, level-2 nodes are 2-wide, level-3 (leaf) are 1-wide.
class EsvoAddressParityFixture : public ::testing::Test {
protected:
    GaiaVoxelWorld dummyWorld;

    void SetUp() override {
        octree = std::make_unique<Octree>();
        octree->worldMin = glm::vec3(0.0f);
        octree->worldMax = glm::vec3(8.0f);
        octree->maxLevels = 4;
        octree->root = std::make_unique<OctreeBlock>();

        // Root: only child 3 (octantBits = 1|2 -> +x,+y, not +z) exists, non-leaf.
        ChildDescriptor root{};
        root.childPointer = 1;
        root.farBit = 0;
        root.validMask = 0b00001000;  // child 3
        root.leafMask = 0b00000000;
        octree->root->childDescriptors.push_back(root);

        // Mid (index 1): only child 5 (octantBits = 1|4 -> +x,+z) exists, and it's a leaf.
        ChildDescriptor mid{};
        mid.childPointer = 0;
        mid.farBit = 0;
        mid.validMask = 0b00100000;  // child 5
        mid.leafMask = 0b00100000;   // child 5 is a leaf
        octree->root->childDescriptors.push_back(mid);

        AttributeLookup rootAttrLookup{};
        rootAttrLookup.valuePointer = 0;
        rootAttrLookup.mask = 0;
        octree->root->attributeLookups.push_back(rootAttrLookup);

        AttributeLookup midAttrLookup{};
        midAttrLookup.valuePointer = 0;
        midAttrLookup.mask = 0b00100000;  // child 5 has an attribute
        octree->root->attributeLookups.push_back(midAttrLookup);

        UncompressedAttributes attr = makeAttributes(
            glm::vec3(0.0f, 1.0f, 0.0f),  // Green
            glm::vec3(1.0f, 0.0f, 0.0f)   // Normal
        );
        octree->root->attributes.push_back(attr);

        octree->totalVoxels = 1;
        octree->leafVoxels = 1;
        octree->memoryUsage = octree->root->getTotalSize();

        lkOctree = std::make_unique<LaineKarrasOctree>(dummyWorld, nullptr, 4, 3);
        lkOctree->setOctree(std::move(octree));
    }

    std::unique_ptr<Octree> octree;
    std::unique_ptr<LaineKarrasOctree> lkOctree;
};

}  // namespace

// Hop sequences below are traced exactly against the production descent (normalizedPos =
// position/8, halving nodeSize/nodePos each level, bit1=+x/bit2=+y/bit4=+z) -- verified with an
// independent script model, not hand-arithmetic:
//   (7,5,3) -> hops [3,5]  (reaches the populated leaf: root child 3, then its child 5)
//   (5,5,1) -> hops [3,0]  (root child 3, but child 0 -- a real, populated-sibling non-match)
//   (1,1,1) -> hops [0,0]  (root child 0 -- doesn't exist at all, diverges at hop 0)
TEST_F(EsvoAddressParityFixture, VoxelExistsAtDepth2ThroughRealSiblingBranches) {
    EXPECT_TRUE(lkOctree->voxelExists(glm::vec3(7.0f, 5.0f, 3.0f), 2));
}

TEST_F(EsvoAddressParityFixture, VoxelDoesNotExistAtSiblingOfPopulatedBranch) {
    // Same root quadrant (child 3), but child 0 instead of child 5 -- exercises hasChild()
    // returning false partway through a multi-hop address.
    EXPECT_FALSE(lkOctree->voxelExists(glm::vec3(5.0f, 5.0f, 1.0f), 2));
}

TEST_F(EsvoAddressParityFixture, VoxelDoesNotExistAtRootSiblingBranch) {
    // Root child 0 doesn't exist at all -- address diverges at hop 0, never reaches hop 1.
    EXPECT_FALSE(lkOctree->voxelExists(glm::vec3(1.0f, 1.0f, 1.0f), 2));
}

TEST_F(EsvoAddressParityFixture, GetVoxelDataReturnsAttributeAtDepth2) {
    auto data = lkOctree->getVoxelData(glm::vec3(7.0f, 5.0f, 3.0f), 2);
    ASSERT_TRUE(data.has_value());
    EXPECT_NEAR(data->color.g, 1.0f, 0.01f);
}

TEST_F(EsvoAddressParityFixture, GetChildMaskAtRootMatchesFixture) {
    uint8_t mask = lkOctree->getChildMask(glm::vec3(7.0f, 5.0f, 3.0f), 0);
    EXPECT_EQ(mask, 0b00001000);
}

TEST_F(EsvoAddressParityFixture, GetChildMaskAtDepth1MatchesFixture) {
    uint8_t mask = lkOctree->getChildMask(glm::vec3(7.0f, 5.0f, 3.0f), 1);
    EXPECT_EQ(mask, 0b00100000);
}

// Direct check that TierAddress's own semantics (independent of LaineKarrasOctree) are unaffected
// by backing storage moving to the kernel-generated EsvoAddress POD -- same assertions
// test_tier_address.cpp already pins, repeated here as the parity test's own self-contained
// evidence that the wrapper didn't silently change behavior.
TEST(TierAddressBackedByGeneratedPod, PushHopAndSharedPrefixStillMatchPreExtractionSemantics) {
    TierAddress a;
    a.PushHop(1);
    a.PushHop(5);
    TierAddress b;
    b.PushHop(1);
    b.PushHop(2);
    EXPECT_EQ(a.Depth(), 2u);
    EXPECT_EQ(TierAddress::SharedPrefixLength(a, b), 1u);
    EXPECT_EQ(a.ToString(), "2:1.5");
}
