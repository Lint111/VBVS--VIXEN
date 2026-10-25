#pragma once

// ============================================================================
// OverwriteRing<T, N> — bounded multi-producer / single-drainer ring that never blocks.
//
// The no-stall replacement for "mutex + std::vector<T> pending queue" (lock-free pattern 3 of the
// VIXEN no-stall program, R213 overflow policy = ring):
//   * Push() is wait-free for any number of producer threads: one fetch_add claims a slot, the
//     payload is written, one release store publishes it. No lock, no CAS retry loop, no sleep.
//   * Drain() consumes every published element in claim order. It claims the whole range with one
//     CAS; a drainer that loses the CAS returns 0 (another drainer owns that range) — it never
//     waits. An element whose producer has claimed but not yet published stops the drain at that
//     index; it is delivered by the next Drain().
//   * Overflow is the ring policy: when producers lap the drainer by more than N, the OLDEST
//     elements are overwritten. DroppedCount() reports how many were lost that way.
//
// Payload words are stored as relaxed atomics under a per-slot seqlock, so a lapped slot can be
// read concurrently with its overwrite without a data race; a torn read is detected and counted
// as dropped instead of delivered. T must be trivially copyable.
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Vixen {

template <class T, std::size_t N>
class OverwriteRing {
    static_assert(std::is_trivially_copyable_v<T>, "OverwriteRing payload must be trivially copyable");
    static_assert(N >= 2 && (N & (N - 1)) == 0, "OverwriteRing capacity must be a power of two");

    static constexpr std::uint64_t kBusy = std::uint64_t{1} << 63;
    static constexpr std::size_t kWords = (sizeof(T) + 7) / 8;

    struct Slot {
        std::atomic<std::uint64_t> seq{0};  // 0 = never written, kBusy bit = a producer owns it, else index + 1
        std::atomic<std::uint64_t> skip{0}; // highest index + 1 whose producer lost the slot (never published)
        std::atomic<std::uint64_t> words[kWords];
        Slot() {
            for (auto& w : words) w.store(0, std::memory_order_relaxed);
        }
    };

public:
    OverwriteRing() = default;
    OverwriteRing(const OverwriteRing&) = delete;
    OverwriteRing& operator=(const OverwriteRing&) = delete;

    static constexpr std::size_t Capacity() noexcept { return N; }

    /// Wait-free publish from any thread.
    void Push(const T& value) noexcept {
        const std::uint64_t index = write_.fetch_add(1, std::memory_order_relaxed);
        Slot& slot = slots_[index & (N - 1)];
        // One producer owns a slot at a time. Two producers that lapped the same slot would otherwise
        // interleave payload words under one valid-looking seq (a torn element delivered as good).
        // The loser does not wait: its element is dropped and the index recorded as skipped.
        std::uint64_t cur = slot.seq.load(std::memory_order_relaxed);
        if ((cur & kBusy) != 0 || cur >= index + 1 ||
            !slot.seq.compare_exchange_strong(cur, cur | kBusy, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            MarkSkipped(slot, index);
            return;
        }
        std::atomic_thread_fence(std::memory_order_release);
        std::uint64_t buffer[kWords] = {};
        std::memcpy(buffer, &value, sizeof(T));
        for (std::size_t i = 0; i < kWords; ++i) slot.words[i].store(buffer[i], std::memory_order_relaxed);
        slot.seq.store(index + 1, std::memory_order_release);
    }

    /// Consume every published element, oldest first. Returns the number delivered to `fn`.
    /// Non-blocking: returns 0 when there is nothing published yet or another drainer owns the range.
    template <class Fn>
    std::size_t Drain(Fn&& fn) {
        std::uint64_t begin = read_.load(std::memory_order_acquire);
        const std::uint64_t end = write_.load(std::memory_order_acquire);
        if (begin == end) return 0;

        const std::uint64_t lo = (end - begin > N) ? end - N : begin;  // older ones were overwritten
        std::uint64_t hi = lo;
        while (hi < end && IsSettled(slots_[hi & (N - 1)], hi)) ++hi;
        if (hi == begin) return 0;  // nothing published at the head yet
        if (!read_.compare_exchange_strong(begin, hi, std::memory_order_acq_rel)) return 0;

        dropped_.fetch_add(lo - begin, std::memory_order_relaxed);
        std::size_t delivered = 0;
        for (std::uint64_t i = lo; i < hi; ++i) {
            T value;
            if (TryRead(i, value)) {
                fn(value);
                ++delivered;
            } else {
                dropped_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return delivered;
    }

    /// Drop everything published so far (the "clear pending" operation).
    void DiscardPending() noexcept {
        read_.store(write_.load(std::memory_order_acquire), std::memory_order_release);
    }

    /// Elements pushed but not yet drained (capped at N: older ones are already overwritten).
    std::size_t PendingCount() const noexcept {
        const std::uint64_t w = write_.load(std::memory_order_acquire);
        const std::uint64_t r = read_.load(std::memory_order_acquire);
        const std::uint64_t n = w >= r ? w - r : 0;
        return static_cast<std::size_t>(n > N ? N : n);
    }

    bool HasPending() const noexcept { return PendingCount() != 0; }

    /// Elements lost to overwrite since construction.
    std::uint64_t DroppedCount() const noexcept { return dropped_.load(std::memory_order_relaxed); }

    /// Total elements ever pushed.
    std::uint64_t PushedCount() const noexcept { return write_.load(std::memory_order_relaxed); }

private:
    static void MarkSkipped(Slot& slot, std::uint64_t index) noexcept {
        std::uint64_t seen = slot.skip.load(std::memory_order_relaxed);
        while (seen < index + 1 &&
               !slot.skip.compare_exchange_weak(seen, index + 1, std::memory_order_release, std::memory_order_relaxed)) {
        }
    }

    /// Index is published, overwritten by a later lap, or skipped by a producer that lost the slot.
    static bool IsSettled(const Slot& slot, std::uint64_t index) noexcept {
        const std::uint64_t s = slot.seq.load(std::memory_order_acquire);
        if ((s & kBusy) == 0 && s >= index + 1) return true;
        return slot.skip.load(std::memory_order_acquire) >= index + 1;
    }

    bool TryRead(std::uint64_t index, T& out) const noexcept {
        const Slot& slot = slots_[index & (N - 1)];
        const std::uint64_t before = slot.seq.load(std::memory_order_acquire);
        if (before != index + 1) return false;
        std::uint64_t buffer[kWords];
        for (std::size_t i = 0; i < kWords; ++i) buffer[i] = slot.words[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (slot.seq.load(std::memory_order_relaxed) != before) return false;
        std::memcpy(&out, buffer, sizeof(T));
        return true;
    }

    alignas(64) std::atomic<std::uint64_t> write_{0};
    alignas(64) std::atomic<std::uint64_t> read_{0};
    std::atomic<std::uint64_t> dropped_{0};
    Slot slots_[N];
};

}  // namespace Vixen
