// test_mip_aniso_pool.cpp — Deep-Field Mip Policy, anisotropic coarse mips
// (MipAnisoPool.h). Bake-time self-check (spec item 3): an axis-aligned slab
// body must show strong axis asymmetry; a solid cube near-isotropic.
//
// Uses a synthetic single-node Octree (root = level 0, 8 leaf-brick children)
// rather than a full recipe bake — the coverage computation only reads
// ChildDescriptor::validMask/leafMask, so this isolates the encoding from
// SdfRecipes/SdfBake plumbing entirely (same "independent of internals"
// spirit as test_mip_sample_bake.cpp's hand-computed expectations).

#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

#include "MipAnisoPool.h"
#include "SVOTypes.h"
#include "SVOBuilder.h"
#include "ShellOctreeGpu.h"

using namespace Vixen::SVO;

namespace {

// Builds a minimal Octree with one root ChildDescriptor whose 8 octant slots
// are all LEAVES (brick level) — validMask bits select which octants are
// occupied. octant bit0=X, bit1=Y, bit2=Z (verified convention, MipAnisoPool.h).
Octree BuildSingleNodeOctree(uint8_t validMask) {
    Octree oct;
    oct.root = std::make_unique<OctreeBlock>();
    ChildDescriptor desc{};
    desc.validMask = validMask;
    desc.leafMask = validMask;  // every valid octant is a leaf (brick-level)
    desc.childPointer = 0;
    oct.root->childDescriptors.push_back(desc);
    return oct;
}

SerializedOctree MakeMinimalSerialized(uint32_t nodeCount, uint32_t channelCount) {
    SerializedOctree s{};
    s.nodeCount = nodeCount;
    s.channelCount = channelCount;
    return s;
}

}  // namespace

// A slab occupying only the -X half (octants 0,2,4,6: bit0=0) must show
// covPosX=0, covNegX=1 — maximal asymmetry on X, and balanced (0.5/0.5) on
// Y and Z since both halves of Y/Z are represented within the -X half.
TEST(MipAnisoPool, SlabAlongMinusXShowsStrongXAsymmetry) {
    const uint8_t slabMask = 0b01010101;  // octants 0,2,4,6 (bit0 clear -> -X half)
    Octree oct = BuildSingleNodeOctree(slabMask);
    SerializedOctree s = MakeMinimalSerialized(/*nodeCount=*/1, /*channelCount=*/1);

    MipAnisoPool pool = BakeMipAnisoPool(oct, s, /*thresholdLevel=*/1);
    ASSERT_EQ(pool.coarseNodeCount, 1u);
    MipAnisoSample root = pool.Get(0, 0);

    EXPECT_FLOAT_EQ(root.covNegX, 1.0f);
    EXPECT_FLOAT_EQ(root.covPosX, 0.0f);
    EXPECT_FLOAT_EQ(root.covPosY, 0.5f);
    EXPECT_FLOAT_EQ(root.covNegY, 0.5f);
    EXPECT_FLOAT_EQ(root.covPosZ, 0.5f);
    EXPECT_FLOAT_EQ(root.covNegZ, 0.5f);

    const auto check = MipAnisoSelfCheckSlabAsymmetry(root);
    EXPECT_TRUE(check.pass) << check.report;
}

// A solid cube (all 8 octants occupied) must be exactly isotropic: every
// axis half is FULLY occupied (4/4 -> 1.0), so +/- coverage is IDENTICAL
// (diff == 0) on every axis — that identity, not the absolute magnitude, is
// what "isotropic" means for this per-axis-asymmetry encoding.
TEST(MipAnisoPool, SolidCubeIsIsotropic) {
    Octree oct = BuildSingleNodeOctree(0xFFu);
    SerializedOctree s = MakeMinimalSerialized(/*nodeCount=*/1, /*channelCount=*/1);

    MipAnisoPool pool = BakeMipAnisoPool(oct, s, /*thresholdLevel=*/1);
    MipAnisoSample root = pool.Get(0, 0);

    EXPECT_FLOAT_EQ(root.covPosX, 1.0f);
    EXPECT_FLOAT_EQ(root.covNegX, 1.0f);
    EXPECT_FLOAT_EQ(root.covPosY, 1.0f);
    EXPECT_FLOAT_EQ(root.covNegY, 1.0f);
    EXPECT_FLOAT_EQ(root.covPosZ, 1.0f);
    EXPECT_FLOAT_EQ(root.covNegZ, 1.0f);

    const auto check = MipAnisoSelfCheckCubeIsotropy(root);
    EXPECT_TRUE(check.pass) << check.report;
}

// A node at/above thresholdLevel gets no sample — stays at the zero default
// (additive pool leaves non-coarse nodes untouched).
TEST(MipAnisoPool, NodeAtOrAboveThresholdLevelIsUntouched) {
    Octree oct = BuildSingleNodeOctree(0xFFu);
    SerializedOctree s = MakeMinimalSerialized(/*nodeCount=*/1, /*channelCount=*/1);

    MipAnisoPool pool = BakeMipAnisoPool(oct, s, /*thresholdLevel=*/0);  // root is level 0, 0 >= 0 -> skipped
    EXPECT_EQ(pool.coarseNodeCount, 0u);
    MipAnisoSample root = pool.Get(0, 0);
    EXPECT_FLOAT_EQ(root.covPosX, 0.0f);
    EXPECT_FLOAT_EQ(root.covNegX, 0.0f);
}

// Serialize round-trip: byte size == coarseNodeCount-independent nodeCount *
// channelCount * sizeof(MipAnisoSample) (every slot serialized, coarse or not
// — mirrors SerializeMipPool's dense layout).
TEST(MipAnisoPool, SerializeRoundTripByteSize) {
    Octree oct = BuildSingleNodeOctree(0xFFu);
    SerializedOctree s = MakeMinimalSerialized(/*nodeCount=*/1, /*channelCount=*/2);

    MipAnisoPool pool = BakeMipAnisoPool(oct, s, /*thresholdLevel=*/1);
    std::vector<uint8_t> bytes = SerializeMipAnisoPool(pool);
    EXPECT_EQ(bytes.size(), static_cast<size_t>(1 * 2) * sizeof(MipAnisoSample));
}
