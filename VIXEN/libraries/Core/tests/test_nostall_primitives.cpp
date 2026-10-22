// Tests for the no-stall primitives: OverwriteRing, RcuSnapshot, PerThreadLog, FixedKeyValueTable.
// Each concurrent test checks a data invariant (nothing lost, nothing torn), never timing.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "FixedKeyValueTable.h"
#include "OverwriteRing.h"
#include "PerThreadLog.h"
#include "RcuSnapshot.h"

namespace {

struct Pair {
    std::uint64_t a;
    std::uint64_t b;
};

TEST(OverwriteRing, DeliversInOrderThenEmpty) {
    Vixen::OverwriteRing<std::uint64_t, 8> ring;
    for (std::uint64_t i = 0; i < 5; ++i) ring.Push(i);
    EXPECT_EQ(ring.PendingCount(), 5u);
    std::vector<std::uint64_t> seen;
    EXPECT_EQ(ring.Drain([&](std::uint64_t v) { seen.push_back(v); }), 5u);
    EXPECT_EQ(seen, (std::vector<std::uint64_t>{0, 1, 2, 3, 4}));
    EXPECT_EQ(ring.PendingCount(), 0u);
    EXPECT_EQ(ring.Drain([](std::uint64_t) {}), 0u);
}

TEST(OverwriteRing, OverflowKeepsNewestAndCountsDropped) {
    Vixen::OverwriteRing<std::uint64_t, 8> ring;
    for (std::uint64_t i = 0; i < 20; ++i) ring.Push(i);
    std::vector<std::uint64_t> seen;
    ring.Drain([&](std::uint64_t v) { seen.push_back(v); });
    ASSERT_EQ(seen.size(), 8u);
    EXPECT_EQ(seen.front(), 12u);
    EXPECT_EQ(seen.back(), 19u);
    EXPECT_EQ(ring.DroppedCount(), 12u);
}

TEST(OverwriteRing, DiscardPendingDropsQueued) {
    Vixen::OverwriteRing<std::uint64_t, 8> ring;
    ring.Push(1);
    ring.Push(2);
    ring.DiscardPending();
    EXPECT_FALSE(ring.HasPending());
    ring.Push(3);
    std::vector<std::uint64_t> seen;
    ring.Drain([&](std::uint64_t v) { seen.push_back(v); });
    EXPECT_EQ(seen, (std::vector<std::uint64_t>{3}));
}

TEST(OverwriteRing, ConcurrentProducersLoseNothingWhenCapacitySuffices) {
    constexpr int kThreads = 4;
    constexpr int kPer = 2000;
    Vixen::OverwriteRing<Pair, 16384> ring;  // > kThreads * kPer, so nothing can lap
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kPer; ++i) ring.Push(Pair{static_cast<std::uint64_t>(t), static_cast<std::uint64_t>(i)});
        });
    }
    for (auto& th : threads) th.join();
    std::vector<int> next(kThreads, 0);
    std::size_t total = 0;
    ring.Drain([&](const Pair& p) {
        ASSERT_LT(p.a, static_cast<std::uint64_t>(kThreads));
        EXPECT_EQ(p.b, static_cast<std::uint64_t>(next[p.a]++));  // per-producer order preserved
        ++total;
    });
    EXPECT_EQ(total, static_cast<std::size_t>(kThreads * kPer));
    EXPECT_EQ(ring.DroppedCount(), 0u);
}

TEST(OverwriteRing, ConcurrentDrainWhileProducingNeverTears) {
    Vixen::OverwriteRing<Pair, 64> ring;  // small: producers lap the drainer
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> torn{0};
    std::thread producer([&] {
        for (std::uint64_t i = 1; i <= 200000; ++i) ring.Push(Pair{i, ~i});
        stop.store(true);
    });
    std::uint64_t delivered = 0;
    while (!stop.load()) {
        ring.Drain([&](const Pair& p) {
            if (p.b != ~p.a) torn.fetch_add(1);
            ++delivered;
        });
    }
    producer.join();
    ring.Drain([&](const Pair& p) {
        if (p.b != ~p.a) torn.fetch_add(1);
        ++delivered;
    });
    EXPECT_EQ(torn.load(), 0u);
    EXPECT_EQ(delivered + ring.DroppedCount(), 200000u);
}

TEST(RcuSnapshot, PublishAndRead) {
    Vixen::RcuSnapshot<int> snap;
    EXPECT_EQ(*snap.Read(), 0);
    snap.Publish(7);
    EXPECT_EQ(*snap.Read(), 7);
}

TEST(RcuSnapshot, PinnedReaderKeepsOldValueAndRetirementWaits) {
    Vixen::RcuSnapshot<int> snap;
    snap.Publish(1);
    {
        auto reader = snap.Read();
        snap.Publish(2);
        snap.Publish(3);
        EXPECT_EQ(*reader, 1);                  // pinned snapshot is intact
        EXPECT_GE(snap.RetiredCountApprox(), 1u);  // not freed while pinned
    }
    snap.Reclaim();
    EXPECT_EQ(snap.RetiredCountApprox(), 0u);
    EXPECT_EQ(*snap.Read(), 3);
}

TEST(RcuSnapshot, UpdateAppliesAndCanDecline) {
    Vixen::RcuSnapshot<std::vector<int>> snap;
    EXPECT_TRUE(snap.Update([](const std::vector<int>& cur) {
        auto next = std::make_unique<std::vector<int>>(cur);
        next->push_back(5);
        return next;
    }));
    EXPECT_FALSE(snap.Update([](const std::vector<int>&) { return std::unique_ptr<std::vector<int>>(); }));
    EXPECT_EQ(snap.Read()->size(), 1u);
}

TEST(RcuSnapshot, ConcurrentUpdatesLoseNoIncrement) {
    Vixen::RcuSnapshot<std::uint64_t> snap;
    constexpr int kThreads = 4;
    constexpr int kPer = 5000;
    std::atomic<bool> readerStop{false};
    std::thread reader([&] {
        std::uint64_t last = 0;
        while (!readerStop.load()) {
            const std::uint64_t v = *snap.Read();
            EXPECT_GE(v, last);  // monotonic: a reader never sees an older snapshot after a newer one
            last = v;
        }
    });
    std::vector<std::thread> writers;
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&] {
            for (int i = 0; i < kPer; ++i) {
                snap.Update([](const std::uint64_t& cur) { return std::make_unique<std::uint64_t>(cur + 1); });
            }
        });
    }
    for (auto& w : writers) w.join();
    readerStop.store(true);
    reader.join();
    EXPECT_EQ(*snap.Read(), static_cast<std::uint64_t>(kThreads) * kPer);
}

TEST(PerThreadLog, MergesAllThreadsAndClears) {
    Vixen::PerThreadLog<int> log;
    constexpr int kThreads = 4;
    constexpr int kPer = 1000;  // crosses chunk boundaries
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kPer; ++i) log.Append(t * 100000 + i);
        });
    }
    for (auto& th : threads) th.join();
    std::vector<int> perThread(kThreads, 0);
    log.ForEach([&](int v) { ++perThread[v / 100000]; });
    for (int t = 0; t < kThreads; ++t) EXPECT_EQ(perThread[t], kPer);
    EXPECT_EQ(log.Size(), static_cast<std::size_t>(kThreads * kPer));
    log.Clear();
    EXPECT_EQ(log.Size(), 0u);
    log.Append(1);
    EXPECT_EQ(log.Size(), 1u);
}

TEST(PerThreadLog, ReadWhileAppendingSeesAPrefix) {
    Vixen::PerThreadLog<std::uint64_t> log;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (std::uint64_t i = 0; i < 100000; ++i) log.Append(i);
        stop.store(true);
    });
    while (!stop.load()) {
        std::uint64_t expect = 0;
        log.ForEach([&](std::uint64_t v) { EXPECT_EQ(v, expect++); });
    }
    writer.join();
    EXPECT_EQ(log.Size(), 100000u);
}

TEST(FixedKeyValueTable, InsertMatchDiffersFull) {
    Vixen::FixedKeyValueTable<4> table;
    std::uint64_t existing = 0;
    using R = Vixen::FixedKeyValueTable<4>::Result;
    EXPECT_EQ(table.Observe(10, 100, existing), R::Inserted);
    EXPECT_EQ(table.Observe(10, 100, existing), R::Match);
    EXPECT_EQ(table.Observe(10, 101, existing), R::Differs);
    EXPECT_EQ(existing, 100u);
    table.Observe(0, 1, existing);  // key 0 is a valid key
    table.Observe(11, 1, existing);
    table.Observe(12, 1, existing);
    EXPECT_EQ(table.Observe(13, 1, existing), R::Full);
    EXPECT_EQ(table.Observe(11, 1, existing), R::Match);  // recorded keys still match when full
}

}  // namespace
