#include <gtest/gtest.h>
#include "InstanceMotionHistory.h"
#include <string>

using namespace Vixen::SVO;

static Affine3x4Gpu Previous(const BodyInstanceGpu& instance) {
    return ToAffine3x4(instance.motion.previousLocalToWorld);
}

// Generic algorithm fixture keys. A production instantiation uses the source
// kind's generated Ref and its hash; these strings are never render identities.
TEST(InstanceMotionHistory, ReorderingAndRigidMotionRetainTheMatchingPreviousTransform) {
    InstanceMotionHistory<std::string> history;
    std::vector<BodyInstanceSource<std::string>> rows{{"a", 7u, {}}, {"b", 3u, {}}};
    SetInstanceTranslationScale(rows[0].instance, {1, 0, 0}, 1.0f);
    SetInstanceTranslationScale(rows[1].instance, {5, 0, 0}, 2.0f);
    auto first = history.ResolveFrame(rows);
    EXPECT_EQ(first[0].motion.valid, 0u);
    EXPECT_EQ(first[1].motion.valid, 0u);
    std::swap(rows[0], rows[1]);
    SetInstanceTranslationScale(rows[0].instance, {8, 0, 0}, 3.0f);
    SetInstanceTranslationScale(rows[1].instance, {2, 0, 0}, 1.0f);
    auto next = history.ResolveFrame(rows);
    EXPECT_EQ(next[0].motion.valid, 1u);
    EXPECT_EQ(next[1].motion.valid, 1u);
    EXPECT_FLOAT_EQ(TransformPoint(Previous(next[0]), {1, 0, 0}).x, 7.0f);
    EXPECT_FLOAT_EQ(TransformPoint(Previous(next[1]), {0, 0, 0}).x, 1.0f);
}

TEST(InstanceMotionHistory, ReuseDeletionDeformationAndDuplicateRefsInvalidateCorrespondence) {
    InstanceMotionHistory<std::string> history;
    std::vector<BodyInstanceSource<std::string>> rows{{"a", 7u, {}}};
    history.ResolveFrame(rows);
    rows[0].generation += uint64_t{1} << 32u;
    EXPECT_EQ(history.ResolveFrame(rows)[0].motion.valid, 0u);
    EXPECT_EQ(history.ResolveFrame(rows)[0].motion.valid, 1u);
    rows[0].instance.material.recipeParams[2] = 4.0f;
    EXPECT_EQ(history.ResolveFrame(rows)[0].motion.valid, 0u);
    history.ResolveFrame({});
    EXPECT_EQ(history.ResolveFrame(rows)[0].motion.valid, 0u);
    rows.push_back(rows.front());
    auto duplicate = history.ResolveFrame(rows);
    EXPECT_EQ(duplicate[0].motion.valid, 0u);
    EXPECT_EQ(duplicate[1].motion.valid, 0u);
    rows.pop_back();
    EXPECT_EQ(history.ResolveFrame(rows)[0].motion.valid, 0u);
    history.Reset();
    EXPECT_EQ(history.ResolveFrame(rows)[0].motion.valid, 0u);
}
