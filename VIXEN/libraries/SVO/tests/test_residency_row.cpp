#include <gtest/gtest.h>

#include "ResidencyRow.h"

#include <cstring>
#include <set>
#include <string>

using namespace Vixen::SVO;

namespace {

constexpr ResidencyTierMask kHotOnly = ResidencyTierBit(ResidencyTier::Hot);
constexpr ResidencyTierMask kHotWarm =
    ResidencyTierBit(ResidencyTier::Hot) | ResidencyTierBit(ResidencyTier::Warm);

}  // namespace

TEST(ResidencyRow, OneRowPerKindEachWithADistinctName) {
    EXPECT_EQ(kResidencyKindCount, 10u);
    std::set<std::string> names;
    for (size_t i = 0; i < kResidencyKindCount; ++i) {
        ASSERT_NE(kResidencyRows[i].kind, nullptr);
        EXPECT_TRUE(names.insert(kResidencyRows[i].kind).second) << kResidencyRows[i].kind;
    }
}

TEST(ResidencyRow, PinnedKindsAreHotOnlyWithHotFloor) {
    for (ResidencyKind kind : {ResidencyKind::Nodes, ResidencyKind::Materials, ResidencyKind::Config,
                               ResidencyKind::MipPool, ResidencyKind::ShellCache}) {
        const ResidencyRow& row = ResidencyRowOf(kind);
        EXPECT_EQ(row.tiers, kHotOnly) << row.kind;
        EXPECT_EQ(row.floorTier, ResidencyTier::Hot) << row.kind;
    }
}

TEST(ResidencyRow, GatedKindsSpanHotWarmWithWarmFloor) {
    for (ResidencyKind kind : {ResidencyKind::ChannelPool, ResidencyKind::BrickLookup,
                               ResidencyKind::TierRefTable, ResidencyKind::OccupancyGrid,
                               ResidencyKind::Bricks}) {
        const ResidencyRow& row = ResidencyRowOf(kind);
        EXPECT_EQ(row.tiers, kHotWarm) << row.kind;
        EXPECT_EQ(row.floorTier, ResidencyTier::Warm) << row.kind;
    }
}

// R0 declares today's ladder only. When R1+ lands a reclaim/reconstruct path this test is
// updated together with the row it widens — never loosened on its own.
TEST(ResidencyRow, NoKindDeclaresAColdOrVirtualTierYet) {
    for (size_t i = 0; i < kResidencyKindCount; ++i) {
        const ResidencyRow& row = kResidencyRows[i];
        EXPECT_EQ(row.tiers & ~kHotWarm, 0u) << row.kind;
        EXPECT_EQ(row.cold, ResidencyColdFormat::None) << row.kind;
        EXPECT_EQ(row.prefetchHorizon, 0u) << row.kind;
        EXPECT_EQ(row.budget, ResidencyBudgetClass::Device) << row.kind;
        EXPECT_EQ(row.evict, ResidencyEvictPolicy::DistanceFromActive) << row.kind;
    }
}

TEST(ResidencyRow, EveryWholesalePayloadBitMapsToAGatedRow) {
    for (WholesalePayload payload : {WholesalePayload::ChannelPool, WholesalePayload::BrickLookup,
                                     WholesalePayload::TierRefTable, WholesalePayload::OccupancyGrid}) {
        const ResidencyKind kind = ResidencyKindOfPayload(payload);
        ASSERT_NE(kind, ResidencyKind::Count);
        EXPECT_EQ(ResidencyRowOf(kind).tiers, kHotWarm);
    }
    EXPECT_STREQ(ResidencyRowOf(ResidencyKindOfPayload(WholesalePayload::ChannelPool)).kind, "channelPool");
    EXPECT_STREQ(ResidencyRowOf(ResidencyKindOfPayload(WholesalePayload::OccupancyGrid)).kind, "occupancyGrid");
}

TEST(ResidencyTier, RegimeMapsOntoTodaysTwoStepLadder) {
    EXPECT_EQ(ResidencyTierOfRegime(CellFootprintRegime::Surface), ResidencyTier::Hot);
    EXPECT_EQ(ResidencyTierOfRegime(CellFootprintRegime::MipHit), ResidencyTier::Warm);
    EXPECT_EQ(ResidencyTierOfRegime(CellFootprintRegime::Cosmic), ResidencyTier::Warm);
    EXPECT_EQ(ResidencyTierBit(ResidencyTier::Hot), 1u);
    EXPECT_EQ(ResidencyTierBit(ResidencyTier::Virtual), 8u);
}
