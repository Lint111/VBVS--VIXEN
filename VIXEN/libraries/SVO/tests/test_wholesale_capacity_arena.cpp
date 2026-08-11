#include <gtest/gtest.h>
#include "WholesaleCapacityArena.h"

using Vixen::SVO::WholesaleCapacityArena;

TEST(WholesaleCapacityArena, RetiresOnlyAfterSafeFrameAndReusesRange) {
    WholesaleCapacityArena arena;
    const auto first = arena.Acquire(128);
    const auto second = arena.Acquire(64);
    EXPECT_EQ(first.offset, 0u);
    EXPECT_EQ(second.offset, 128u);
    arena.Retire(first, 2);
    arena.Reclaim(1);
    EXPECT_EQ(arena.ReusableBytes(), 0u);
    const auto third = arena.Acquire(32);
    EXPECT_EQ(third.offset, 192u);
    arena.Reclaim(2);
    EXPECT_EQ(arena.ReusableBytes(), 128u);
    const auto reused = arena.Acquire(96);
    EXPECT_EQ(reused.offset, 0u);
    EXPECT_EQ(arena.AllocatedCapacityBytes(), 224u);
}

TEST(WholesaleCapacityArena, MergesAdjacentRetiredRanges) {
    WholesaleCapacityArena arena;
    const auto a = arena.Acquire(32);
    const auto b = arena.Acquire(32);
    arena.Retire(b, 4);
    arena.Retire(a, 4);
    arena.Reclaim(4);
    EXPECT_EQ(arena.ReusableBytes(), 64u);
    const auto whole = arena.Acquire(64);
    EXPECT_EQ(whole.offset, 0u);
    EXPECT_EQ(arena.AllocatedCapacityBytes(), 64u);
}

TEST(WholesaleCapacityArena, ZeroSizeDoesNotConsumeCapacity) {
    WholesaleCapacityArena arena;
    EXPECT_EQ(arena.Acquire(0).size, 0u);
    EXPECT_EQ(arena.AllocatedCapacityBytes(), 0u);
}
