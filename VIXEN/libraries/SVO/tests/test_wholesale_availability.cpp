#include <gtest/gtest.h>

#include "WholesaleAvailability.h"

using namespace Vixen::SVO;

TEST(WholesaleAvailability, PromotesAfterTwoSurfaceFramesAndPublishesOnlyAfterCopy) {
    WholesaleAvailability state;
    EXPECT_FALSE(AdvanceWholesaleAvailability(state, FootprintRegime::Surface, 3u));
    EXPECT_EQ(state.readyMask, 0u);
    EXPECT_TRUE(AdvanceWholesaleAvailability(state, FootprintRegime::Surface, 3u));
    EXPECT_EQ(state.pendingMask, 3u);
    EXPECT_EQ(state.readyMask, 0u);
    PublishWholesaleReady(state);
    EXPECT_EQ(state.pendingMask, 0u);
    EXPECT_EQ(state.readyMask, 3u);
}

TEST(WholesaleAvailability, DemotesAfterFourNonSurfaceFramesAndClearsReadyFirst) {
    WholesaleAvailability state;
    AdvanceWholesaleAvailability(state, FootprintRegime::Surface, 3u);
    AdvanceWholesaleAvailability(state, FootprintRegime::Surface, 3u);
    PublishWholesaleReady(state);
    const uint32_t generation = state.generation;
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(AdvanceWholesaleAvailability(state, FootprintRegime::MipHit, 3u));
        EXPECT_EQ(state.readyMask, 3u);
    }
    EXPECT_TRUE(AdvanceWholesaleAvailability(state, FootprintRegime::MipHit, 3u));
    EXPECT_EQ(state.readyMask, 0u);
    EXPECT_EQ(state.generation, generation + 1u);
}
