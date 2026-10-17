#include <gtest/gtest.h>

#include "Selection/PickVote.h"

using namespace Vixen::RenderGraph;

namespace {
PickVoteSample Sample(uint32_t address, glm::vec2 jitter, uint32_t instance = 0) {
    return {address, instance, jitter};
}
}

TEST(PickVote, WeightFallsOffWithDistanceFromThePixelCentre) {
    EXPECT_FLOAT_EQ(PickVoteWeight({0.0f, 0.0f}), 1.0f);
    EXPECT_NEAR(PickVoteWeight({0.5f, 0.0f}), std::exp(-0.5f), 1e-6f);
    EXPECT_NEAR(PickVoteWeight({0.5f, 0.5f}), std::exp(-1.0f), 1e-6f);
    EXPECT_GT(PickVoteWeight({0.1f, 0.0f}), PickVoteWeight({0.3f, 0.0f}));
    EXPECT_GT(PickVoteWeight({0.5f, 0.5f}), 0.0f);
    EXPECT_EQ(PickVoteWeight({0.3f, -0.2f}), PickVoteWeight({-0.3f, 0.2f}));
}

TEST(PickVote, EmptyHistoryHasNoVote) {
    PickVoteHistory history;
    EXPECT_FALSE(history.Winner().Valid());
    EXPECT_FALSE(history.Winner().Hit());
}

TEST(PickVote, SummedWeightPicksTheWinnerNotTheNewestSample) {
    PickVoteHistory history;
    for (int i = 0; i < 5; ++i) history.Observe(Sample(7, {0.1f, 0.1f}), PickMotion::Still, true);
    history.Observe(Sample(9, {0.0f, 0.0f}), PickMotion::Still, true);
    const PickVoteResult result = history.Winner();
    EXPECT_EQ(result.address, 7u);
    EXPECT_EQ(result.sampleCount, 6u);
    EXPECT_FALSE(result.unanimous);
}

TEST(PickVote, CentredSamplesOutweighCornerSamplesOfEqualCount) {
    PickVoteHistory history;
    history.Observe(Sample(1, {0.5f, 0.5f}), PickMotion::Still, true);
    history.Observe(Sample(1, {-0.5f, -0.5f}), PickMotion::Still, true);
    history.Observe(Sample(2, {0.0f, 0.1f}), PickMotion::Still, true);
    history.Observe(Sample(2, {0.1f, 0.0f}), PickMotion::Still, true);
    EXPECT_EQ(history.Winner().address, 2u);
}

TEST(PickVote, SameAddressOnAnotherInstanceIsAnotherId) {
    PickVoteHistory history;
    history.Observe(Sample(5, {0.0f, 0.0f}, 1), PickMotion::Still, true);
    history.Observe(Sample(5, {0.0f, 0.0f}, 2), PickMotion::Still, true);
    history.Observe(Sample(5, {0.0f, 0.0f}, 2), PickMotion::Still, true);
    EXPECT_EQ(history.Winner().instance, 2u);
}

TEST(PickVote, EqualWeightGoesToTheMoreRecentId) {
    PickVoteHistory history;
    history.Observe(Sample(3, {0.2f, 0.0f}), PickMotion::Still, true);
    history.Observe(Sample(4, {0.2f, 0.0f}), PickMotion::Still, true);
    EXPECT_EQ(history.Winner().address, 4u);
}

TEST(PickVote, MissVotesLikeAnyOtherId) {
    PickVoteHistory history;
    history.Observe(Sample(kPickMissSentinel, {0.0f, 0.0f}, kPickMissSentinel), PickMotion::Unknown, true);
    history.Observe(Sample(kPickMissSentinel, {0.0f, 0.0f}, kPickMissSentinel), PickMotion::Unknown, true);
    history.Observe(Sample(8, {0.4f, 0.4f}), PickMotion::Still, true);
    EXPECT_FALSE(history.Winner().Hit());
    EXPECT_TRUE(history.Winner().Valid());
}

TEST(PickVote, KeepsOnlyOneJitterCycle) {
    PickVoteHistory history;
    for (uint32_t i = 0; i < kReconstructionJitterCycle; ++i) history.Observe(Sample(1, {0.0f, 0.0f}), PickMotion::Still, true);
    for (uint32_t i = 0; i < kReconstructionJitterCycle; ++i) history.Observe(Sample(2, {0.0f, 0.0f}), PickMotion::Still, true);
    const PickVoteResult result = history.Winner();
    EXPECT_EQ(result.sampleCount, kReconstructionJitterCycle);
    EXPECT_EQ(result.address, 2u);
    EXPECT_TRUE(result.unanimous);
}

TEST(PickVote, MotionResetsTheHistoryToTheCurrentSample) {
    PickVoteHistory history;
    for (int i = 0; i < 10; ++i) history.Observe(Sample(1, {0.0f, 0.0f}), PickMotion::Still, true);
    history.Observe(Sample(6, {0.5f, 0.5f}), PickMotion::Moved, true);
    const PickVoteResult result = history.Winner();
    EXPECT_EQ(result.sampleCount, 1u);
    EXPECT_EQ(result.address, 6u);
}

TEST(PickVote, UnknownMotionKeepsTheHistory) {
    PickVoteHistory history;
    history.Observe(Sample(1, {0.0f, 0.0f}), PickMotion::Still, true);
    history.Observe(Sample(kPickMissSentinel, {0.0f, 0.0f}, kPickMissSentinel), PickMotion::Unknown, true);
    EXPECT_EQ(history.Winner().sampleCount, 2u);
}

TEST(PickVote, WithoutJitterTheVoteIsTheSingleCurrentHit) {
    PickVoteHistory history;
    history.Observe(Sample(1, {0.0f, 0.0f}), PickMotion::Still, false);
    history.Observe(Sample(2, {0.0f, 0.0f}), PickMotion::Still, false);
    history.Observe(Sample(3, {0.0f, 0.0f}), PickMotion::Still, false);
    const PickVoteResult result = history.Winner();
    EXPECT_EQ(result.sampleCount, 1u);
    EXPECT_EQ(result.address, 3u);
    EXPECT_TRUE(result.unanimous);
}

TEST(PickVote, ClassifiesMotionFromVectorAndValidity) {
    const glm::vec2 extent(1280.0f, 720.0f);
    EXPECT_EQ(ClassifyPickMotion(false, {0.2f, 0.2f}, 1.0f, extent), PickMotion::Unknown);
    EXPECT_EQ(ClassifyPickMotion(true, {0.0f, 0.0f}, 0.0f, extent), PickMotion::Moved);
    EXPECT_EQ(ClassifyPickMotion(true, {1e-6f, -1e-6f}, 1.0f, extent), PickMotion::Still);
    EXPECT_EQ(ClassifyPickMotion(true, {0.0f, 0.001f}, 1.0f, extent), PickMotion::Moved);
}
