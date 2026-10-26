// The TAA history feedback's C++ face (generated from codegen/aa-callables, the same source the
// GPU-system emitter inlines into TemporalAntiAliasingSystem.comp; R619.1).
#include "Generated/TemporalResolve.g.hpp"

#include <gtest/gtest.h>

namespace {

using Vixen::AntiAliasing::HistoryFeedback;
using Vixen::AntiAliasing::ResampleBlur;

TEST(TemporalResolveTest, IntegerReprojectionKeepsTheRampAndTheCeiling) {
    // A still camera (and any whole-texel move) resamples exactly: the 1/N ramp up to the ceiling.
    EXPECT_FLOAT_EQ(HistoryFeedback(0.9f, 1.0f, 0.0f, 0.0f), 0.5f);
    EXPECT_FLOAT_EQ(HistoryFeedback(0.9f, 3.0f, 0.0f, 0.0f), 0.75f);
    EXPECT_FLOAT_EQ(HistoryFeedback(0.9f, 64.0f, 0.0f, 0.0f), 0.9f);
}

TEST(TemporalResolveTest, HalfTexelReprojectionGivesUpThreeTenthsOfTheWeight) {
    EXPECT_FLOAT_EQ(ResampleBlur(0.5f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(ResampleBlur(0.0f, 0.5f), 1.0f);
    EXPECT_FLOAT_EQ(HistoryFeedback(0.9f, 64.0f, 0.5f, 0.0f), 0.9f * 0.7f);
    EXPECT_FLOAT_EQ(HistoryFeedback(0.9f, 64.0f, 0.5f, 0.5f), 0.9f * 0.7f);
}

TEST(TemporalResolveTest, FeedbackFallsMonotonicallyTowardsTheTexelMidpointAndIsSymmetric) {
    float previous = HistoryFeedback(0.9f, 64.0f, 0.0f, 0.0f);
    for (int step = 1; step <= 8; ++step) {
        const float offset = 0.0625f * static_cast<float>(step);
        const float weight = HistoryFeedback(0.9f, 64.0f, offset, 0.0f);
        EXPECT_LT(weight, previous) << "offset " << offset;
        EXPECT_FLOAT_EQ(weight, HistoryFeedback(0.9f, 64.0f, 1.0f - offset, 0.0f)) << "offset " << offset;
        previous = weight;
    }
}

TEST(TemporalResolveTest, NoHistoryWeightMeansNoHistory) {
    EXPECT_FLOAT_EQ(HistoryFeedback(0.0f, 64.0f, 0.25f, 0.75f), 0.0f);
}

} // namespace
