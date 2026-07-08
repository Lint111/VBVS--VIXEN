// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
//
// Concurrency regression tests for TypedCacher::GetOrCreate.
//
// Root cause under test (P0 / AR#51): when two threads request the same
// uncached key, the waiter blocked inside `future.get()` WHILE STILL HOLDING
// the cacher's shared/unique lock (the `return ....get();` lived inside the
// lock scope). The creating thread must re-acquire the same lock to fulfil the
// promise, so the two threads deadlock permanently. These tests force that
// interleaving and assert it completes; a deadlock is detected via a watchdog
// rather than hanging the suite forever.

#include <gtest/gtest.h>
#include <TypedCacher.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace {

struct TestCI { uint64_t id; };
struct TestResource { uint64_t value; };

// Minimal concrete cacher. Create() exposes a hook so tests can control the
// timing window during which the "pending" future is visible to other threads.
class TestCacher : public CashSystem::TypedCacher<TestResource, TestCI> {
public:
    std::atomic<int> createCount{0};
    std::function<void()> onCreate;  // invoked inside Create(), before the resource is produced

protected:
    PtrT Create(const TestCI& ci) override {
        createCount.fetch_add(1, std::memory_order_relaxed);
        if (onCreate) onCreate();
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

// Two threads, same uncached key: one becomes the creator, the other finds the
// pending future and waits. Pre-fix this deadlocks because the waiter holds the
// lock across future.get() and the creator needs that lock to set the promise.
TEST(TypedCacherConcurrency, ConcurrentSameKeyDoesNotDeadlock) {
    TestCacher cacher;
    std::atomic<bool> creatorInCreate{false};
    std::atomic<bool> waiterStarted{false};

    cacher.onCreate = [&] {
        creatorInCreate.store(true, std::memory_order_release);
        // Hold inside Create() until the waiter has had time to enter get().
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
                cacher.GetOrCreate(ci);  // finds pending future, waits on it
            });
            creator.join();
            waiter.join();
        },
        std::chrono::seconds(5));

    EXPECT_TRUE(ok)
        << "TypedCacher::GetOrCreate deadlocked under concurrent same-key access "
           "(lock held across future.get()).";
}

// Many threads, same uncached key: must create the resource exactly once and
// hand every caller the same instance.
TEST(TypedCacherConcurrency, ConcurrentSameKeyCreatesExactlyOnce) {
    TestCacher cacher;
    cacher.onCreate = [&] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); };

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

namespace {

// Mirrors the derived-cacher bug class fixed under audit V-M9: a hand-written override
// (PipelineCacher::Cleanup, ShaderModuleCacher::SerializeToFile, etc.) iterates m_entries
// directly instead of going through a locked base-class accessor. TypedCacher's own Cleanup()
// is the locked default (just calls Clear()), so this subclass overrides it the way the real
// cachers do — walking m_entries under the shared TypedCacher lock — to prove the discipline
// holds when exercised concurrently with GetOrCreate() insertions.
class SelfCleaningCacher : public CashSystem::TypedCacher<TestResource, TestCI> {
public:
    void Cleanup() override {
        std::shared_lock rlock(m_lock);
        // Read-only walk — same shape as SerializeToFile's entry collection loop.
        for (const auto& [key, entry] : m_entries) {
            (void)key;
            (void)entry;
        }
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

// Concurrent Cleanup() (a locked read-walk of m_entries) racing GetOrCreate() insertions of
// distinct keys (audit V-M9): under TSAN/ASAN this catches an unlocked walk racing a live
// unordered_map insert; under a plain build it at least proves no deadlock/crash.
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
}
