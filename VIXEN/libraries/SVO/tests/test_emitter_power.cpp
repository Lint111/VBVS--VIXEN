// test_emitter_power.cpp - R502.2 sparse emitter power lane (EmitterPower.h, S0).
//
// Gate: sum of leaf power == root power for every octree; the lane matches an independent
// brute-force emission*area sum; non-emissive trees bake nothing; the lane rides the
// concatenated mip stream and survives the shell/streaming derive unchanged; far-field mip
// bytes are not altered by the lane.

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#undef far
#undef near
#undef min
#undef max

#include "MipBake.h"
#include "SdfBake.h"
#include "SdfRecipes.h"
#include "ShellDerive.h"
#include "ShellOctreeGpu.h"

using namespace Vixen::SVO;

namespace {

SdfBodyOctree MakeBody(int n, float r, const std::function<float(const glm::vec3&)>& emission) {
    RecipeParams rp{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec3 c(n * 0.5f);
    SdfBakeResult baked = BakeRecipeToSdfWorldWithEmission(RECIPE_SPHERE, c, rp, n, 2.0f, emission);
    return BuildSdfBodyOctree(baked, 3);
}

double BruteForcePower(const SerializedOctree& s) {
    const double edge = 1.0 / (static_cast<double>(s.config.bricksPerAxis) * 8.0);
    double total = 0.0;
    for (uint32_t bi = 0; bi < s.brickCount; ++bi) {
        const uint32_t* mats = reinterpret_cast<const uint32_t*>(s.bricks.data()) +
                               static_cast<size_t>(bi) * SerializedOctree::kVoxelsPerBrick;
        for (uint32_t v = 0; v < SerializedOctree::kVoxelsPerBrick; ++v) {
            if (mats[v] == 0u) continue;
            const float e = s.readPoolVoxel(SEM_EMISSION, bi, v, 0);
            if (e > 0.0f) total += static_cast<double>(e) * edge * edge;
        }
    }
    return total;
}

float HalfEmitter(const glm::vec3& p) { return p.x > p.y ? 1.5f + 0.01f * p.z : 0.0f; }

}  // namespace

class EmitterPowerSizes : public ::testing::TestWithParam<int> {};

TEST_P(EmitterPowerSizes, SigmaLeafPowerEqualsRootPower) {
    SdfBodyOctree body = MakeBody(GetParam(), GetParam() * 0.375f, HalfEmitter);
    SerializedOctree s = SerializeSdf(body);
    const Octree* oct = body.octree->getOctree();
    ASSERT_NE(oct, nullptr);
    s.emitterPowerPool = BakeEmitterPowerLane(*oct, s);
    ASSERT_FALSE(s.emitterPowerPool.empty());
    ASSERT_EQ(s.emitterPowerPool.size() % sizeof(MipSample), 0u);

    const EmitterPowerLaneView lane = ViewEmitterPowerLane(s.emitterPowerPool);
    ASSERT_GT(lane.LeafCount(), 0u);
    EXPECT_EQ(lane.nodeCount, s.nodeCount) << "lane header carries the node count";
    double sumLeaves = 0.0;
    uint32_t prev = 0u;
    for (uint32_t i = 0; i < lane.LeafCount(); ++i) {
        const uint32_t node = lane.LeafNode(i);
        if (i > 0) EXPECT_GT(node, prev) << "leaf ordinals must be strictly ascending";
        prev = node;
        sumLeaves += lane.LeafPower(i);
        EXPECT_NEAR(lane.LeafPower(i), lane.NodePower(node), 1e-6 * lane.LeafPower(i));
        EXPECT_EQ(lane.FindLeaf(node), i);
    }
    EXPECT_NEAR(sumLeaves, lane.RootPower(), 1e-5 * lane.RootPower());
    EXPECT_NEAR(lane.RootPower(), BruteForcePower(s), 1e-5 * lane.RootPower());
}

INSTANTIATE_TEST_SUITE_P(Trees, EmitterPowerSizes, ::testing::Values(16, 32, 64));

TEST(EmitterPower, InteriorNodePowerIsChildSum) {
    SdfBodyOctree body = MakeBody(64, 24.0f, HalfEmitter);
    SerializedOctree s = SerializeSdf(body);
    const Octree* oct = body.octree->getOctree();
    s.emitterPowerPool = BakeEmitterPowerLane(*oct, s);
    const EmitterPowerLaneView lane = ViewEmitterPowerLane(s.emitterPowerPool);
    const auto& nodes = oct->root->childDescriptors;
    uint32_t checkedInterior = 0u;
    // Walk only real interior nodes: a brick-leaf slot's own descriptor is not a parent.
    std::vector<uint32_t> stack = {0u};
    while (!stack.empty()) {
        const uint32_t n = stack.back();
        stack.pop_back();
        const ChildDescriptor& d = nodes[n];
        const uint32_t totalNonLeaf = static_cast<uint32_t>(std::popcount(static_cast<uint8_t>(d.validMask & ~d.leafMask)));
        double sum = 0.0;
        uint32_t nl = 0u, lf = 0u;
        for (int o = 0; o < 8; ++o) {
            if (!d.hasChild(o)) continue;
            const bool leaf = d.isLeaf(o);
            const uint32_t idx = leaf ? d.childPointer + totalNonLeaf + lf++ : d.childPointer + nl++;
            sum += lane.NodePower(idx);
            if (!leaf) stack.push_back(idx);
        }
        EXPECT_NEAR(lane.NodePower(n), sum, 1e-5 * (sum + 1e-12));
        ++checkedInterior;
    }
    EXPECT_GT(checkedInterior, 1u) << "fixture must have more than one interior level";
}

TEST(EmitterPower, NonEmissiveTreeBakesEmptyLane) {
    SdfBodyOctree body = MakeBody(32, 12.0f, [](const glm::vec3&) { return 0.0f; });
    SerializedOctree s = SerializeSdfWithMips(body);
    EXPECT_TRUE(s.emitterPowerPool.empty());
}

TEST(EmitterPower, OctantSumsPartitionTheBrick) {
    SdfBodyOctree body = MakeBody(32, 12.0f, HalfEmitter);
    SerializedOctree s = SerializeSdf(body);
    s.emitterPowerPool = BakeEmitterPowerLane(*body.octree->getOctree(), s);
    const EmitterPowerLaneView lane = ViewEmitterPowerLane(s.emitterPowerPool);
    for (uint32_t i = 0; i < lane.LeafCount(); ++i) {
        float positive = 0.0f;
        for (uint32_t o = 0; o < 8; ++o) {
            EXPECT_GE(lane.LeafOctantPower(i, o), 0.0f);
            positive += lane.LeafOctantPower(i, o);
        }
        EXPECT_GT(positive, 0.0f);
    }
}

TEST(EmitterPower, ConcatenatedStreamCarriesLaneAndFarFieldBytesUnchanged) {
    SdfBodyOctree a = MakeBody(32, 12.0f, HalfEmitter);
    SdfBodyOctree dark = MakeBody(32, 10.0f, [](const glm::vec3&) { return 0.0f; });
    SdfBodyOctree b = MakeBody(16, 6.0f, [](const glm::vec3& p) { return p.z > 8.0f ? 3.0f : 0.0f; });
    const std::vector<const SdfBodyOctree*> bodies = {&a, &dark, &b};
    ConcatenatedOctrees cat = ConcatenateSdfWithMips(bodies);
    ASSERT_EQ(cat.count, 3u);

    EXPECT_TRUE(emitterPowerPoolBakedOf(cat.configs[0]));
    EXPECT_FALSE(emitterPowerPoolBakedOf(cat.configs[1]));
    EXPECT_TRUE(emitterPowerPoolBakedOf(cat.configs[2]));

    for (uint32_t k : {0u, 2u}) {
        SerializedOctree ref = SerializeSdf(*bodies[k]);
        const Octree* oct = bodies[k]->octree->getOctree();
        const MipPool pool = BakeMipPool(*oct, ref);
        const std::vector<uint8_t> farField = SerializeMipPool(pool);
        const uint8_t* mip = cat.mipPool.data();
        const uint32_t mipBase = mipPoolBaseOf(cat.configs[k]);
        ASSERT_LE(static_cast<size_t>(mipBase) * sizeof(MipSample) + farField.size(), cat.mipPool.size());
        EXPECT_EQ(std::memcmp(mip + static_cast<size_t>(mipBase) * sizeof(MipSample), farField.data(),
                              farField.size()), 0)
            << "far-field mip bytes must be untouched by the power lane";

        const size_t laneByte = static_cast<size_t>(emitterPowerPoolBaseOf(cat.configs[k])) * sizeof(MipSample);
        ASSERT_LT(laneByte, cat.mipPool.size());
        EXPECT_GE(laneByte, static_cast<size_t>(mipBase) * sizeof(MipSample) + farField.size());
        const std::vector<uint8_t> expected = pool.emitterPower;
        ASSERT_FALSE(expected.empty());
        ASSERT_LE(laneByte + expected.size(), cat.mipPool.size());
        EXPECT_EQ(std::memcmp(mip + laneByte, expected.data(), expected.size()), 0);
    }
}

TEST(EmitterPower, ShellDerivePassesLaneThroughUnchanged) {
    SdfBodyOctree a = MakeBody(32, 12.0f, HalfEmitter);
    const std::vector<const SdfBodyOctree*> bodies = {&a};
    ConcatenatedOctrees cat = ConcatenateSdfWithMips(bodies);
    ShellPool shell = DeriveShellPool(cat);
    EXPECT_EQ(shell.compact.mipPool, cat.mipPool);
    EXPECT_EQ(emitterPowerPoolBaseOf(shell.compact.configs[0]), emitterPowerPoolBaseOf(cat.configs[0]));
    EXPECT_EQ(emitterPowerPoolBakedOf(shell.compact.configs[0]), emitterPowerPoolBakedOf(cat.configs[0]));
}
