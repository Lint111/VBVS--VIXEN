// test_shell_octree_gpu.cpp — SP2 Task 5a
//
// Pins the CPU byte format that ShellOctreeGpu::Serialize() produces, so the
// later Vulkan upload node (Task 5b) and the ray-march shader can rely on it
// WITHOUT a GPU in the loop. Headless / gtest-only; no Vulkan.
//
// The format being pinned is exactly the one VoxelSceneCacher::BuildOctree()
// (libraries/CashSystem/src/VoxelSceneCacher.cpp ~L556-636) uploads and the
// shader VoxelRayMarch.comp (~L46-62, L193-194) consumes:
//   - nodes   : raw ChildDescriptor array, stride sizeof(ChildDescriptor) == 8.
//   - bricks  : 512 * uint32_t per brick (2048 B), voxel order z*64+y*8+x.
//   - material: GPUMaterial palette, stride 32 B.
//   - config  : OctreeConfig std140 UBO element, stride 432 B (the shader's configs[3]
//               ArrayStride; its trailing float[14] is std140-padded to 14*16).

#include "ShellOctree.h"
#include "ShellOctreeGpu.h"
#include "SVOTypes.h"        // ChildDescriptor
#include "SVOBuilder.h"      // Octree / OctreeBlock

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace Vixen::SVO;

namespace {

// ---------------------------------------------------------------------------
// Single-octree serialization
// ---------------------------------------------------------------------------

// nodes buffer == childDescriptors.size() * sizeof(ChildDescriptor), non-empty.
TEST(ShellOctreeGpu, NodesBufferMatchesChildDescriptorArray) {
    auto shell = BuildShellOctree(6, /*materialId*/ 2);
    const SerializedOctree s = Serialize(shell);

    const auto* oct = shell.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    ASSERT_NE(oct->root, nullptr);
    const size_t descriptorCount = oct->root->childDescriptors.size();

    EXPECT_GT(descriptorCount, 0u);
    EXPECT_EQ(s.nodeCount, descriptorCount);
    EXPECT_FALSE(s.nodes.empty());
    EXPECT_EQ(s.nodes.size(), descriptorCount * sizeof(ChildDescriptor));
    EXPECT_EQ(sizeof(ChildDescriptor), 8u);  // node stride contract
}

// bricks buffer: non-empty, a whole multiple of the documented brick stride
// (512 * uint32_t == 2048 bytes), one brick per brickView.
TEST(ShellOctreeGpu, BricksBufferIsWholeBrickMultiple) {
    auto shell = BuildShellOctree(6, /*materialId*/ 2);
    const SerializedOctree s = Serialize(shell);

    const auto* oct = shell.octree->getOctree();
    const size_t brickViewCount = oct->root->brickViews.size();
    EXPECT_GT(brickViewCount, 0u);

    EXPECT_FALSE(s.bricks.empty());
    EXPECT_EQ(SerializedOctree::kBrickStrideBytes, 512u * sizeof(uint32_t));  // 2048
    EXPECT_EQ(s.bricks.size() % SerializedOctree::kBrickStrideBytes, 0u);
    EXPECT_EQ(s.brickCount, brickViewCount);
    EXPECT_EQ(s.bricks.size(), brickViewCount * SerializedOctree::kBrickStrideBytes);
}

// At least one brick voxel carries the shell's material id (the shell is solid
// surface), proving the per-voxel GaiaVoxelWorld material query produced data.
TEST(ShellOctreeGpu, BricksContainTheShellMaterial) {
    constexpr uint32_t kMat = 2;
    auto shell = BuildShellOctree(6, kMat);
    const SerializedOctree s = Serialize(shell);

    ASSERT_FALSE(s.bricks.empty());
    const auto* words = reinterpret_cast<const uint32_t*>(s.bricks.data());
    const size_t wordCount = s.bricks.size() / sizeof(uint32_t);

    size_t solid = 0;
    for (size_t i = 0; i < wordCount; ++i) {
        if (words[i] != 0u) {
            ++solid;
            EXPECT_EQ(words[i], kMat) << "filled voxel must carry the shell material id";
        }
    }
    EXPECT_GT(solid, 0u) << "shell surface must populate at least one brick voxel";
}

// materials palette is non-empty, 32-byte stride.
TEST(ShellOctreeGpu, MaterialsBufferNonEmptyWithStride) {
    auto shell = BuildShellOctree(6, /*materialId*/ 2);
    const SerializedOctree s = Serialize(shell);

    EXPECT_EQ(sizeof(GPUMaterial), 32u);  // material stride contract
    EXPECT_FALSE(s.materials.empty());
    EXPECT_EQ(s.materials.size() % sizeof(GPUMaterial), 0u);
}

// OctreeConfig: 256-byte UBO portion, fields sane and matching the build.
TEST(ShellOctreeGpu, ConfigFieldsAreSane) {
    constexpr int depth = 6;
    const float n = static_cast<float>(1 << depth);  // 64
    auto shell = BuildShellOctree(depth, /*materialId*/ 2);
    const SerializedOctree s = Serialize(shell);

    // sizeof(OctreeConfig) == the shader's std140 UBO-array stride (432 B: the trailing
    // float[14] is std140-padded to 14*16, and the compiled SPIR-V decorates configs[3]
    // with ArrayStride 432). The node uploads std::array<OctreeConfig,3> at this stride,
    // so a mismatch misaligns configs[1]/[2] and octreeIndex>0 bodies render nothing.
    // Lock the stride + the offsets of every field the shader actually reads (<200).
    EXPECT_EQ(sizeof(OctreeConfig), 432u);
    EXPECT_EQ(offsetof(OctreeConfig, localToWorld), 64u);
    EXPECT_EQ(offsetof(OctreeConfig, worldToLocal), 128u);
    EXPECT_EQ(offsetof(OctreeConfig, nodeArrayBase), 192u);
    EXPECT_EQ(offsetof(OctreeConfig, brickArrayBase), 196u);

    const OctreeConfig& c = s.config;
    EXPECT_EQ(c.esvoMaxScale, 22);
    EXPECT_EQ(c.brickDepthLevels, 3);          // BuildShellOctree kBrickDepthLevels
    EXPECT_EQ(c.brickSize, 8);                  // 1 << 3
    EXPECT_EQ(c.userMaxLevels, depth + 3);      // maxLevels = depth + brickDepthLevels
    EXPECT_EQ(c.bricksPerAxis, (1 << depth) / 8);

    // Grid bounds come from the octree's [0, n]^3.
    EXPECT_FLOAT_EQ(c.gridMinX, 0.0f);
    EXPECT_FLOAT_EQ(c.gridMinY, 0.0f);
    EXPECT_FLOAT_EQ(c.gridMinZ, 0.0f);
    EXPECT_FLOAT_EQ(c.gridMaxX, n);
    EXPECT_FLOAT_EQ(c.gridMaxY, n);
    EXPECT_FLOAT_EQ(c.gridMaxZ, n);

    // Derived scales (same formulas as the cacher).
    EXPECT_EQ(c.minESVOScale, c.esvoMaxScale - c.userMaxLevels + 1);
}

// ---------------------------------------------------------------------------
// Multi-octree concatenation (<= 3 octrees)
// ---------------------------------------------------------------------------

// octree[1] base == octree[0] count for both node and brick arrays; total
// concatenated size == sum of the parts; configs carry the per-octree base.
TEST(ShellOctreeGpu, ConcatRecordsPerOctreeBaseOffsets) {
    auto a = BuildShellOctree(5, /*materialId*/ 1);
    auto b = BuildShellOctree(6, /*materialId*/ 2);

    const SerializedOctree sa = Serialize(a);
    const SerializedOctree sb = Serialize(b);

    std::vector<const ShellOctree*> octrees = {&a, &b};
    const ConcatenatedOctrees cat = Concatenate(octrees);

    ASSERT_EQ(cat.count, 2u);

    // Per-octree node base / count.
    EXPECT_EQ(cat.configs[0].nodeArrayBase, 0);
    EXPECT_EQ(cat.configs[1].nodeArrayBase, static_cast<int32_t>(sa.nodeCount));
    EXPECT_EQ(cat.nodeCounts[0], sa.nodeCount);
    EXPECT_EQ(cat.nodeCounts[1], sb.nodeCount);

    // Per-octree brick base / count.
    EXPECT_EQ(cat.configs[0].brickArrayBase, 0);
    EXPECT_EQ(cat.configs[1].brickArrayBase, static_cast<int32_t>(sa.brickCount));
    EXPECT_EQ(cat.brickCounts[0], sa.brickCount);
    EXPECT_EQ(cat.brickCounts[1], sb.brickCount);

    // Concatenated buffers are exactly the sum of the parts.
    EXPECT_EQ(cat.nodes.size(), sa.nodes.size() + sb.nodes.size());
    EXPECT_EQ(cat.bricks.size(), sa.bricks.size() + sb.bricks.size());

    // The second octree's bytes are appended verbatim after the first.
    ASSERT_GE(cat.nodes.size(), sa.nodes.size() + sb.nodes.size());
    EXPECT_EQ(0, std::memcmp(cat.nodes.data(), sa.nodes.data(), sa.nodes.size()));
    EXPECT_EQ(0, std::memcmp(cat.nodes.data() + sa.nodes.size(),
                             sb.nodes.data(), sb.nodes.size()));
    EXPECT_EQ(0, std::memcmp(cat.bricks.data() + sa.bricks.size(),
                             sb.bricks.data(), sb.bricks.size()));
}

TEST(ShellOctreeGpu, ConcatRejectsMoreThanThree) {
    auto a = BuildShellOctree(4, 1);
    auto b = BuildShellOctree(4, 2);
    auto c = BuildShellOctree(4, 3);
    auto d = BuildShellOctree(4, 4);
    std::vector<const ShellOctree*> four = {&a, &b, &c, &d};
    EXPECT_THROW(Concatenate(four), std::length_error);
}

// ---------------------------------------------------------------------------
// Per-instance GPU record packing
// ---------------------------------------------------------------------------

TEST(ShellOctreeGpu, InstanceRecordIsSixtyFourBytes) {
    EXPECT_EQ(sizeof(BodyInstanceGpu), 64u);
}

TEST(ShellOctreeGpu, InstancePackingRoundTrips) {
    std::vector<BodyInstanceGpu> in = {
        {{1.0f, 2.0f, 3.0f}, 4.0f, {0.1f, 0.2f, 0.3f}, 0u},
        {{-5.0f, 6.0f, 7.5f}, 8.0f, {0.4f, 0.5f, 0.6f}, 2u},
    };
    const std::vector<uint8_t> bytes = PackInstances(in);
    ASSERT_EQ(bytes.size(), in.size() * sizeof(BodyInstanceGpu));

    const auto* out = reinterpret_cast<const BodyInstanceGpu*>(bytes.data());
    for (size_t i = 0; i < in.size(); ++i) {
        EXPECT_EQ(out[i].octreeIndex, in[i].octreeIndex);
        EXPECT_FLOAT_EQ(out[i].renderScale, in[i].renderScale);
        for (int k = 0; k < 3; ++k) {
            EXPECT_FLOAT_EQ(out[i].worldPos[k], in[i].worldPos[k]);
            EXPECT_FLOAT_EQ(out[i].color[k], in[i].color[k]);
        }
    }
}

}  // namespace
