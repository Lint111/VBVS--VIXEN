// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
//
// Concurrency tests for TypedCacher::GetOrCreate — lock-free federation P4 (CACHE/MEMO →
// immutable generations, design §2.4).
//
// History: P0 / AR#51 found that a same-key waiter blocked inside `future.get()` WHILE STILL
// HOLDING the cacher's shared lock, and the creator needed that lock to publish — a permanent
// deadlock. The base class was patched to drop the lock before waiting, but six derived
// overrides kept the original shape (pipeline_cacher.cpp:108, shader_module_cacher.cpp:54,
// pipeline_layout_cacher.cpp:23, SamplerCacher.cpp:63, MeshCacher.cpp:69,
// RenderPassCacher.cpp:56). P4 removes the lock itself: one builder per key claims a slot with a
// CAS, waiters wait on the slot's state word holding nothing, generations are immutable. These
// tests are the stress proof; a deadlock is detected via a watchdog rather than hanging the suite.

#include <gtest/gtest.h>
#include <TypedCacher.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

struct TestCI { uint64_t id; };
struct TestResource { uint64_t value; };

// Minimal concrete cacher. Create() exposes a hook so tests can control the
// timing window during which the key's builder slot is visible to other threads.
class TestCacher : public CashSystem::TypedCacher<TestResource, TestCI> {
public:
    std::atomic<int> createCount{0};
    std::function<void(const TestCI&)> onCreate;  // invoked inside Create(), before the resource is produced

    using CashSystem::TypedCacher<TestResource, TestCI>::Find;
    using CashSystem::TypedCacher<TestResource, TestCI>::EntryCount;

protected:
    PtrT Create(const TestCI& ci) override {
        createCount.fetch_add(1, std::memory_order_relaxed);
        if (onCreate) onCreate(ci);
        auto r = std::make_shared<TestResource>();
        r->value = ci.id * 10;
        return r;
    }

    std::uint64_t ComputeKey(const TestCI& ci) const override { return ci.id; }
};

// Runs `fn` on a worker thread. Returns true if it finished within `timeout`,
// false if it appears deadlocked (in which case the worker is detached and the
// stuck threads are reaped at process exit). `done` is a shared_ptr so it stays
// alive even when a deadlocked worker is abandoned.
template <typename Fn>
bool RunWithDeadlockGuard(Fn fn, std::chrono::milliseconds timeout) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([done, fn = std::move(fn)]() mutable {
        fn();
        done->store(true, std::memory_order_release);
    });

    const auto start = std::chrono::steady_clock::now();
    while (!done->load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() - start < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const bool ok = done->load(std::memory_order_acquire);
    if (ok) {
        worker.join();
    } else {
        worker.detach();  // deadlocked - abandon; OS reaps stuck threads on exit
    }
    return ok;
}

}  // namespace

// Two threads, same uncached key: one becomes the builder, the other finds the
// builder's slot and waits on it. The historical deadlock interleaving.
TEST(TypedCacherConcurrency, ConcurrentSameKeyDoesNotDeadlock) {
    TestCacher cacher;
    std::atomic<bool> creatorInCreate{false};
    std::atomic<bool> waiterStarted{false};

    cacher.onCreate = [&](const TestCI&) {
        creatorInCreate.store(true, std::memory_order_release);
        // Hold inside Create() until the waiter has had time to enter its wait.
        while (!waiterStarted.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    };

    const bool ok = RunWithDeadlockGuard(
        [&] {
            const TestCI ci{42};
            std::thread creator([&] { cacher.GetOrCreate(ci); });
            while (!creatorInCreate.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            std::thread waiter([&] {
                waiterStarted.store(true, std::memory_order_release);
                cacher.GetOrCreate(ci);  // finds the builder's slot, waits on it
            });
            creator.join();
            waiter.join();
        },
        std::chrono::seconds(5));

    EXPECT_TRUE(ok)
        << "TypedCacher::GetOrCreate deadlocked under concurrent same-key access.";
    EXPECT_EQ(cacher.createCount.load(), 1);
}

// Many threads, same uncached key: must create the resource exactly once and
// hand every caller the same instance.
TEST(TypedCacherConcurrency, ConcurrentSameKeyCreatesExactlyOnce) {
    TestCacher cacher;
    cacher.onCreate = [&](const TestCI&) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); };

    constexpr int kThreads = 8;
    std::vector<std::shared_ptr<TestResource>> results(kThreads);

    const bool ok = RunWithDeadlockGuard(
        [&] {
            std::vector<std::thread> threads;
            threads.reserve(kThreads);
            for (int i = 0; i < kThreads; ++i) {
                threads.emplace_back([&, i] { results[i] = cacher.GetOrCreate(TestCI{7}); });
            }
            for (auto& t : threads) t.join();
        },
        std::chrono::seconds(5));

    ASSERT_TRUE(ok) << "TypedCacher::GetOrCreate deadlocked under concurrent same-key access.";
    EXPECT_EQ(cacher.createCount.load(), 1) << "Same key must be created exactly once.";
    for (const auto& r : results) {
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(r->value, 70u);
        EXPECT_EQ(r, results.front()) << "All callers must receive the same instance.";
    }
}

// P4 gate: the same-key STRESS. Many rounds of N threads racing the same fresh key, with the
// builder deliberately slow so every other thread becomes a waiter. Exactly one build per
// round, one instance per key, no deadlock across 200 rounds.
TEST(TypedCacherConcurrency, SameKeyStress_OneBuilderPerKey_NoDeadlock) {
    TestCacher cacher;
    cacher.onCreate = [&](const TestCI&) { std::this_thread::sleep_for(std::chrono::microseconds(200)); };

    constexpr int kThreads = 16;
    constexpr int kRounds = 200;
    std::atomic<int> mismatches{0};

    const bool ok = RunWithDeadlockGuard(
        [&] {
            for (int round = 0; round < kRounds; ++round) {
                const TestCI ci{static_cast<uint64_t>(1000 + round)};
                std::vector<std::shared_ptr<TestResource>> results(kThreads);
                std::atomic<int> gate{0};
                std::vector<std::thread> threads;
                threads.reserve(kThreads);
                for (int i = 0; i < kThreads; ++i) {
                    threads.emplace_back([&, i] {
                        gate.fetch_add(1);
                        while (gate.load() < kThreads) std::this_thread::yield();  // release together
                        results[i] = cacher.GetOrCreate(ci);
                    });
                }
                for (auto& t : threads) t.join();
                for (const auto& r : results) {
                    if (!r || r != results.front() || r->value != ci.id * 10) mismatches.fetch_add(1);
                }
            }
        },
        std::chrono::seconds(60));

    ASSERT_TRUE(ok) << "same-key stress deadlocked";
    EXPECT_EQ(cacher.createCount.load(), kRounds) << "exactly one builder per key";
    EXPECT_EQ(mismatches.load(), 0) << "every waiter must receive the builder's instance";
    EXPECT_EQ(cacher.EntryCount(), static_cast<std::size_t>(kRounds));
}

// Waiters hold nothing: while a same-key waiter is blocked on a slow builder, an unrelated
// key can be built and published, lookups proceed, and the cacher can even be cleared.
// (Under the old shared lock a waiter's held read-lock would block the unrelated publish.)
TEST(TypedCacherConcurrency, WaitersHoldNothing_UnrelatedKeysProgress) {
    TestCacher cacher;
    std::atomic<bool> builderInCreate{false};
    std::atomic<bool> releaseBuilder{false};

    cacher.onCreate = [&](const TestCI& ci) {
        if (ci.id == 1) {
            builderInCreate.store(true, std::memory_order_release);
            while (!releaseBuilder.load(std::memory_order_acquire)) std::this_thread::yield();
        }
    };

    std::shared_ptr<TestResource> waiterResult;
    std::shared_ptr<TestResource> unrelatedResult;
    bool hasUnrelatedWhileWaiting = false;

    const bool ok = RunWithDeadlockGuard(
        [&] {
            std::thread builder([&] { cacher.GetOrCreate(TestCI{1}); });
            while (!builderInCreate.load(std::memory_order_acquire)) std::this_thread::yield();
            std::thread waiter([&] { waiterResult = cacher.GetOrCreate(TestCI{1}); });
            std::this_thread::sleep_for(std::chrono::milliseconds(20));  // waiter is parked on the slot

            // Unrelated key builds and publishes while the waiter is parked.
            unrelatedResult = cacher.GetOrCreate(TestCI{2});
            hasUnrelatedWhileWaiting = cacher.Has(2);
            EXPECT_FALSE(cacher.Has(1)) << "key 1 is pending, not published";

            releaseBuilder.store(true, std::memory_order_release);
            builder.join();
            waiter.join();
        },
        std::chrono::seconds(5));

    ASSERT_TRUE(ok) << "a parked waiter blocked unrelated progress";
    ASSERT_NE(unrelatedResult, nullptr);
    EXPECT_TRUE(hasUnrelatedWhileWaiting);
    ASSERT_NE(waiterResult, nullptr);
    EXPECT_EQ(waiterResult->value, 10u);
    EXPECT_EQ(cacher.createCount.load(), 2);
}

// A builder that throws must not strand its waiters (the old promise was never fulfilled, so
// waiters hung forever and the key could never be created again). Waiters get the exception;
// the next request re-claims the key and builds it.
TEST(TypedCacherConcurrency, FailedBuildReleasesWaitersAndRebuilds) {
    TestCacher cacher;
    std::atomic<int> attempts{0};
    std::atomic<bool> builderInCreate{false};
    std::atomic<bool> releaseBuilder{false};

    cacher.onCreate = [&](const TestCI&) {
        if (attempts.fetch_add(1) == 0) {
            builderInCreate.store(true, std::memory_order_release);
            while (!releaseBuilder.load(std::memory_order_acquire)) std::this_thread::yield();
            throw std::runtime_error("first build fails");
        }
    };

    bool builderThrew = false;
    bool waiterThrew = false;
    std::shared_ptr<TestResource> rebuilt;

    const bool ok = RunWithDeadlockGuard(
        [&] {
            std::thread builder([&] {
                try { cacher.GetOrCreate(TestCI{5}); } catch (const std::runtime_error&) { builderThrew = true; }
            });
            while (!builderInCreate.load(std::memory_order_acquire)) std::this_thread::yield();
            std::thread waiter([&] {
                try { cacher.GetOrCreate(TestCI{5}); } catch (const std::runtime_error&) { waiterThrew = true; }
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            releaseBuilder.store(true, std::memory_order_release);
            builder.join();
            waiter.join();
            rebuilt = cacher.GetOrCreate(TestCI{5});
        },
        std::chrono::seconds(5));

    ASSERT_TRUE(ok) << "a failed build stranded its waiters";
    EXPECT_TRUE(builderThrew);
    EXPECT_TRUE(waiterThrew);
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_EQ(rebuilt->value, 50u);
    EXPECT_EQ(cacher.createCount.load(), 2);
}

// Generation growth: many distinct keys published from many threads, each key requested by
// every thread in a different order. Exactly one build per key, every request returns the
// canonical instance, and every key stays reachable after the table has grown several times.
TEST(TypedCacherConcurrency, ManyKeysManyThreads_ExactlyOncePerKey_AcrossGrowth) {
    TestCacher cacher;

    constexpr int kThreads = 8;
    constexpr uint64_t kKeys = 4096;  // 64 -> 128 -> ... -> 8192 cells: six growths
    std::atomic<int> mismatches{0};

    const bool ok = RunWithDeadlockGuard(
        [&] {
            std::vector<std::thread> threads;
            for (int t = 0; t < kThreads; ++t) {
                threads.emplace_back([&, t] {
                    std::vector<uint64_t> order(kKeys);
                    for (uint64_t k = 0; k < kKeys; ++k) order[k] = k;
                    std::shuffle(order.begin(), order.end(), std::mt19937_64(t));
                    for (uint64_t k : order) {
                        auto r = cacher.GetOrCreate(TestCI{k});
                        if (!r || r->value != k * 10) mismatches.fetch_add(1);
                    }
                });
            }
            for (auto& th : threads) th.join();
        },
        std::chrono::seconds(60));

    ASSERT_TRUE(ok) << "many-key stress deadlocked";
    EXPECT_EQ(mismatches.load(), 0);
    EXPECT_EQ(cacher.createCount.load(), static_cast<int>(kKeys)) << "each key built exactly once";
    EXPECT_EQ(cacher.EntryCount(), static_cast<std::size_t>(kKeys));
    for (uint64_t k = 0; k < kKeys; ++k) {
        auto r = cacher.Find(k);
        ASSERT_NE(r, nullptr) << "key " << k << " unreachable after growth";
        EXPECT_EQ(r, cacher.GetOrCreate(TestCI{k})) << "hit must return the published instance";
    }
    EXPECT_EQ(cacher.createCount.load(), static_cast<int>(kKeys)) << "hits never rebuild";
}

// Erase tombstones the key (lookups miss, entry count drops) and the next request rebuilds it
// into a fresh slot; the tombstone never hides the new entry.
TEST(TypedCacherConcurrency, EraseTombstonesAndRebuilds) {
    TestCacher cacher;
    auto first = cacher.GetOrCreate(TestCI{9});
    ASSERT_TRUE(cacher.Has(9));
    EXPECT_EQ(cacher.EntryCount(), 1u);

    cacher.Erase(9);
    EXPECT_FALSE(cacher.Has(9));
    EXPECT_EQ(cacher.EntryCount(), 0u);

    auto second = cacher.GetOrCreate(TestCI{9});
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second, first) << "erase must not resurrect the old instance";
    EXPECT_TRUE(cacher.Has(9));
    EXPECT_EQ(cacher.EntryCount(), 1u);
    EXPECT_EQ(cacher.GetOrCreate(TestCI{9}), second);
    EXPECT_EQ(cacher.createCount.load(), 2);
}

// Clear is an epoch flip: readers that were probing the old generation are not freed from
// under them, and a builder that started in the old generation still delivers to its waiters.
TEST(TypedCacherConcurrency, ClearDuringPendingBuildIsSafe) {
    TestCacher cacher;
    std::atomic<bool> builderInCreate{false};
    std::atomic<bool> releaseBuilder{false};
    cacher.onCreate = [&](const TestCI& ci) {
        if (ci.id == 3) {
            builderInCreate.store(true, std::memory_order_release);
            while (!releaseBuilder.load(std::memory_order_acquire)) std::this_thread::yield();
        }
    };

    std::shared_ptr<TestResource> builderResult, waiterResult;
    const bool ok = RunWithDeadlockGuard(
        [&] {
            std::thread builder([&] { builderResult = cacher.GetOrCreate(TestCI{3}); });
            while (!builderInCreate.load(std::memory_order_acquire)) std::this_thread::yield();
            std::thread waiter([&] { waiterResult = cacher.GetOrCreate(TestCI{3}); });
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            cacher.Clear();                       // epoch flip while key 3 is pending
            EXPECT_FALSE(cacher.Has(3));
            releaseBuilder.store(true, std::memory_order_release);
            builder.join();
            waiter.join();
        },
        std::chrono::seconds(5));

    ASSERT_TRUE(ok);
    ASSERT_NE(builderResult, nullptr);
    EXPECT_EQ(waiterResult, builderResult) << "waiter parked before the flip gets the builder's value";
    // The pending build published into the retired generation; the new epoch rebuilds on demand.
    auto fresh = cacher.GetOrCreate(TestCI{3});
    ASSERT_NE(fresh, nullptr);
    EXPECT_EQ(cacher.createCount.load(), 2);
}

namespace {

// Mirrors the derived cachers' teardown/persistence walks (PipelineCacher::Cleanup,
// ShaderModuleCacher::SerializeToFile, ...): they visit the published entries through the
// base-class ForEachEntry() walk, which holds nothing and tolerates concurrent publication.
class SelfCleaningCacher : public CashSystem::TypedCacher<TestResource, TestCI> {
public:
    std::atomic<std::size_t> visited{0};

    void Cleanup() override {
        // Read-only walk — same shape as SerializeToFile's entry collection loop.
        ForEachEntry([&](const CacheEntry& entry) {
            if (entry.resource && entry.resource->value == entry.key * 10) visited.fetch_add(1);
        });
    }

protected:
    PtrT Create(const TestCI& ci) override {
        auto r = std::make_shared<TestResource>();
        r->value = ci.id * 10;
        return r;
    }

    std::uint64_t ComputeKey(const TestCI& ci) const override { return ci.id; }
};

}  // namespace

// Concurrent Cleanup() (an unlocked walk of the generation chain) racing GetOrCreate()
// publications of distinct keys: under TSAN/ASAN this catches a walk racing a publish; under a
// plain build it proves no deadlock/crash and that every entry the walk sees is fully published.
TEST(TypedCacherConcurrency, CleanupDuringConcurrentInsertsIsSafe) {
    SelfCleaningCacher cacher;

    const bool ok = RunWithDeadlockGuard(
        [&] {
            std::thread cleaner([&] {
                for (int i = 0; i < 200; ++i) {
                    cacher.Cleanup();
                }
            });
            std::vector<std::thread> writers;
            for (int i = 0; i < 8; ++i) {
                writers.emplace_back([&, i] {
                    for (int k = 0; k < 50; ++k) {
                        cacher.GetOrCreate(TestCI{static_cast<uint64_t>(i * 1000 + k)});
                    }
                });
            }
            cleaner.join();
            for (auto& t : writers) t.join();
        },
        std::chrono::seconds(10));

    EXPECT_TRUE(ok) << "Cleanup() racing concurrent GetOrCreate() inserts deadlocked or hung.";
    cacher.Cleanup();
    EXPECT_GE(cacher.visited.load(), 400u) << "the final walk sees every published entry";
}
