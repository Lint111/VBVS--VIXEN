#include <gtest/gtest.h>

#include "WholesaleAvailability.h"

using namespace Vixen::SVO;

TEST(WholesaleAvailability, PromotesAfterTwoSurfaceFramesAndPublishesOnlyAfterCopy) {
    ResidencyState state;
    EXPECT_FALSE(AdvanceWholesaleAvailability(state, CellFootprintRegime::Surface, 3u));
    EXPECT_EQ(state.readyMask, 0u);
    EXPECT_TRUE(AdvanceWholesaleAvailability(state, CellFootprintRegime::Surface, 3u));
    EXPECT_EQ(state.pendingMask, 3u);
    EXPECT_EQ(state.readyMask, 0u);
    PublishWholesaleReady(state);
    EXPECT_EQ(state.pendingMask, 0u);
    EXPECT_EQ(state.readyMask, 3u);
}

TEST(WholesaleAvailability, DemotesAfterFourNonSurfaceFramesAndClearsReadyFirst) {
    ResidencyState state;
    AdvanceWholesaleAvailability(state, CellFootprintRegime::Surface, 3u);
    AdvanceWholesaleAvailability(state, CellFootprintRegime::Surface, 3u);
    PublishWholesaleReady(state);
    const uint32_t generation = state.generation;
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(AdvanceWholesaleAvailability(state, CellFootprintRegime::MipHit, 3u));
        EXPECT_EQ(state.readyMask, 3u);
    }
    EXPECT_TRUE(AdvanceWholesaleAvailability(state, CellFootprintRegime::MipHit, 3u));
    EXPECT_EQ(state.readyMask, 0u);
    EXPECT_EQ(state.generation, generation + 1u);
}

TEST(WholesaleAvailability, PairIsAtomicAndReAdmissionReusesRetainedBytes) {
    ResidencyState state;
    const uint32_t pair = WholesalePayloadMask();
    AdvanceWholesaleAvailability(state, CellFootprintRegime::Surface, pair);
    AdvanceWholesaleAvailability(state, CellFootprintRegime::Surface, pair);
    RetainWholesalePayload(state, pair, 120u, 24u, 0x11u, 0x22u);
    PublishWholesaleReady(state);
    ASSERT_EQ(state.readyMask, pair);

    for (int i = 0; i < 4; ++i) AdvanceWholesaleAvailability(state, CellFootprintRegime::MipHit, pair);
    EXPECT_EQ(state.readyMask, 0u);
    EXPECT_EQ(state.retainedMask, pair);
    EXPECT_EQ(state.reusablePopulatedBytes, 0u);

    AdvanceWholesaleAvailability(state, CellFootprintRegime::Surface, pair);
    EXPECT_TRUE(AdvanceWholesaleAvailability(state, CellFootprintRegime::Surface, pair));
    EXPECT_EQ(state.pendingMask, 0u);
    EXPECT_EQ(state.readyMask, 0u);
    EXPECT_EQ(state.reusablePopulatedBytes, 144u);
    PublishWholesaleReady(state);
    EXPECT_EQ(state.readyMask, pair);
}

TEST(WholesaleAvailability, SignatureIsDeterministicForIdenticalState) {
    ResidencyState a, b;
    const uint32_t pair = WholesalePayloadMask();
    for (auto* state : {&a, &b}) {
        AdvanceWholesaleAvailability(*state, CellFootprintRegime::Surface, pair);
        AdvanceWholesaleAvailability(*state, CellFootprintRegime::Surface, pair);
        RetainWholesalePayload(*state, pair, 120u, 24u, 0x11u, 0x22u);
        PublishWholesaleReady(*state);
    }
    EXPECT_EQ(WholesaleResidentSignatureFNV64(a, 7u), WholesaleResidentSignatureFNV64(b, 7u));
}

TEST(WholesaleAvailability, MipOnlyTransitionKeepsFinePairUnreadable) {
    ResidencyState state;
    const uint32_t pair = WholesalePayloadMask();

    EXPECT_FALSE(AdvanceWholesaleAvailability(state, CellFootprintRegime::MipHit, pair));
    EXPECT_EQ(state.committedRegime, CellFootprintRegime::MipHit);
    EXPECT_EQ(state.readyMask, 0u);
    EXPECT_EQ(state.pendingMask, 0u);

    // A mip-only leg must not promote the fine pair merely because the pair is
    // available in the retained ledger; only Surface demand may do that.
    RetainWholesalePayload(state, pair, 120u, 24u, 0x11u, 0x22u);
    for (int i = 0; i < 8; ++i) {
        EXPECT_FALSE(AdvanceWholesaleAvailability(state, CellFootprintRegime::MipHit, pair));
        EXPECT_EQ(state.committedRegime, CellFootprintRegime::MipHit);
        EXPECT_EQ(state.readyMask, 0u);
        EXPECT_EQ(state.pendingMask, 0u);
    }
}

TEST(WholesaleAvailability, S4PayloadBitsAreIndependent) {
    EXPECT_EQ(WholesaleS4PayloadMask(), 0xcu);
    EXPECT_EQ(WholesaleFinePayloadMask(), 0x3u);
    ResidencyState state;
    state.readyMask = static_cast<uint32_t>(WholesalePayload::TierRefTable);
    EXPECT_NE(state.readyMask & static_cast<uint32_t>(WholesalePayload::TierRefTable), 0u);
    EXPECT_EQ(state.readyMask & static_cast<uint32_t>(WholesalePayload::OccupancyGrid), 0u);
}

// ---------------------------------------------------------------------------
// Out-of-core R0: ResidencyState = WholesaleAvailability generalized. The tier fields are
// observers of the regime the protocol already moves; they must track it exactly and must
// not widen the parity surface.
// ---------------------------------------------------------------------------

TEST(ResidencyState, TierTracksRegimeThroughPromoteAndDemote) {
    ResidencyState state;
    const uint32_t pair = WholesalePayloadMask();
    EXPECT_EQ(state.desiredTier, ResidencyTier::Warm);
    EXPECT_EQ(state.committedTier, ResidencyTier::Warm);

    AdvanceWholesaleAvailability(state, CellFootprintRegime::Surface, pair);
    EXPECT_EQ(state.desiredTier, ResidencyTier::Hot);
    EXPECT_EQ(state.committedTier, ResidencyTier::Warm);  // hysteresis: one frame is not a commit
    AdvanceWholesaleAvailability(state, CellFootprintRegime::Surface, pair);
    EXPECT_EQ(state.committedRegime, CellFootprintRegime::Surface);
    EXPECT_EQ(state.committedTier, ResidencyTier::Hot);

    for (int i = 0; i < 3; ++i) {
        AdvanceWholesaleAvailability(state, CellFootprintRegime::Cosmic, pair);
        EXPECT_EQ(state.desiredTier, ResidencyTier::Warm);
        EXPECT_EQ(state.committedTier, ResidencyTier::Hot);
    }
    AdvanceWholesaleAvailability(state, CellFootprintRegime::Cosmic, pair);
    EXPECT_EQ(state.committedRegime, CellFootprintRegime::Cosmic);
    EXPECT_EQ(state.committedTier, ResidencyTier::Warm);
}

TEST(ResidencyState, SetCommittedRegimeKeepsTierInLockstep) {
    // The eager-boot path (CreateOctreeBuffers) commits Surface without Advance*.
    ResidencyState state;
    SetCommittedRegime(state, CellFootprintRegime::Surface);
    EXPECT_EQ(state.committedRegime, CellFootprintRegime::Surface);
    EXPECT_EQ(state.committedTier, ResidencyTier::Hot);
    EXPECT_EQ(state.desiredTier, ResidencyTier::Warm);
    SetDesiredRegime(state, CellFootprintRegime::Surface);
    EXPECT_EQ(state.desiredTier, ResidencyTier::Hot);
}

TEST(ResidencyState, TierFieldsDoNotEnterTheResidentSignature) {
    ResidencyState a, b;
    const uint32_t pair = WholesalePayloadMask();
    for (auto* state : {&a, &b}) {
        AdvanceWholesaleAvailability(*state, CellFootprintRegime::Surface, pair);
        AdvanceWholesaleAvailability(*state, CellFootprintRegime::Surface, pair);
        RetainWholesalePayload(*state, pair, 120u, 24u, 0x11u, 0x22u);
        PublishWholesaleReady(*state);
    }
    b.committedTier = ResidencyTier::Virtual;
    b.desiredTier = ResidencyTier::Cold;
    EXPECT_EQ(WholesaleResidentSignatureFNV64(a, 7u), WholesaleResidentSignatureFNV64(b, 7u));
}
