// test_instance_sort.cpp — Sparse-Mip ESVO LOD Inc1, M4b (Task 10 part 2, occlusion piece).
//
// Pure math test for SortInstancesFrontToBack — no octree/GPU needed. A minimal
// stand-in struct with a `float worldPos[3]` member exercises the template without
// pulling in the full BodyInstanceGpu (ShellOctreeGpu.h) dependency chain.

#include <gtest/gtest.h>

#include "InstanceSort.h"

using namespace Vixen::SVO;

namespace {
struct FakeInstance {
    float worldPos[3];
    int id;  // identity marker so post-sort order is checkable
};

FakeInstance MakeInst(int id, float x, float y, float z) {
    FakeInstance i{};
    i.id = id;
    i.worldPos[0] = x; i.worldPos[1] = y; i.worldPos[2] = z;
    return i;
}
}  // namespace

TEST(InstanceSort, OrdersNearestFirst) {
    std::vector<FakeInstance> instances = {
        MakeInst(/*far*/    2, 0.0f, 0.0f, 1000.0f),
        MakeInst(/*near*/   0, 0.0f, 0.0f, 10.0f),
        MakeInst(/*middle*/ 1, 0.0f, 0.0f, 100.0f),
    };
    SortInstancesFrontToBack(instances, glm::vec3(0.0f, 0.0f, 0.0f));

    ASSERT_EQ(instances.size(), 3u);
    EXPECT_EQ(instances[0].id, 0);  // nearest
    EXPECT_EQ(instances[1].id, 1);  // middle
    EXPECT_EQ(instances[2].id, 2);  // farthest
}

TEST(InstanceSort, CameraNotAtOriginStillOrdersByActualDistance) {
    const glm::vec3 cam(100.0f, 0.0f, 0.0f);
    std::vector<FakeInstance> instances = {
        MakeInst(/*far from cam*/  0, 0.0f, 0.0f, 0.0f),     // distance 100 from cam
        MakeInst(/*near cam*/      1, 105.0f, 0.0f, 0.0f),   // distance 5 from cam
    };
    SortInstancesFrontToBack(instances, cam);

    ASSERT_EQ(instances.size(), 2u);
    EXPECT_EQ(instances[0].id, 1);  // nearest to camera, not nearest to origin
    EXPECT_EQ(instances[1].id, 0);
}

TEST(InstanceSort, EmptyListIsNoOp) {
    std::vector<FakeInstance> instances;
    EXPECT_NO_THROW(SortInstancesFrontToBack(instances, glm::vec3(0.0f)));
    EXPECT_TRUE(instances.empty());
}

TEST(InstanceSort, SingleInstanceIsNoOp) {
    std::vector<FakeInstance> instances = { MakeInst(7, 1.0f, 2.0f, 3.0f) };
    SortInstancesFrontToBack(instances, glm::vec3(0.0f));
    ASSERT_EQ(instances.size(), 1u);
    EXPECT_EQ(instances[0].id, 7);
}

TEST(InstanceSort, StableForEqualDistances) {
    // Two instances equidistant from the camera should keep their relative order
    // (stable_sort) rather than an unspecified swap — deterministic frame-to-frame
    // ordering avoids gratuitous instance-array churn when distances tie.
    std::vector<FakeInstance> instances = {
        MakeInst(0, 10.0f, 0.0f, 0.0f),
        MakeInst(1, -10.0f, 0.0f, 0.0f),
    };
    SortInstancesFrontToBack(instances, glm::vec3(0.0f));
    ASSERT_EQ(instances.size(), 2u);
    EXPECT_EQ(instances[0].id, 0);
    EXPECT_EQ(instances[1].id, 1);
}
