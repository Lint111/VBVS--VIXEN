#pragma once

// ============================================================================
// FixedKeyValueTable<N> — fixed-capacity, insert-only (key -> 64-bit value) table that never blocks.
//
// For diagnostics that used a mutex-guarded map ("have I seen this key, and with what value?").
// Open addressing over N slots (power of two). A slot is claimed by one CAS on its key (the CAS is
// per slot and each key claims at most one slot, so contention only happens when two threads insert
// the SAME new key at the same moment); the value is then stored and published with a release flag.
// A lookup that finds a claimed-but-unpublished slot reports Pending instead of waiting.
// When the table is full new keys are not recorded (Full) — the cap policy; existing keys still match.
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Vixen {

template <std::size_t N>
class FixedKeyValueTable {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "capacity must be a power of two");

public:
    enum class Result : std::uint8_t {
        Inserted,  // key was new and is now recorded with `value`
        Match,     // key already recorded with an equal value
        Differs,   // key already recorded with a different value (`existing` holds it)
        Pending,   // key claimed by another thread, value not published yet
        Full,      // no slot left for a new key
    };

    /// Record (key, value) if new, otherwise compare with what is recorded.
    Result Observe(std::uint64_t key, std::uint64_t value, std::uint64_t& existing) noexcept {
        const std::uint64_t stored = key + 1;  // 0 marks an empty slot; key == UINT64_MAX is mapped to 0 -> never recorded
        if (stored == 0) return Result::Full;
        std::size_t index = Mix(key) & (N - 1);
        for (std::size_t probe = 0; probe < N; ++probe, index = (index + 1) & (N - 1)) {
            Slot& slot = slots_[index];
            std::uint64_t current = slot.key.load(std::memory_order_acquire);
            if (current == 0) {
                if (slot.key.compare_exchange_strong(current, stored, std::memory_order_acq_rel)) {
                    slot.value.store(value, std::memory_order_relaxed);
                    slot.ready.store(true, std::memory_order_release);
                    return Result::Inserted;
                }
                // lost the claim: `current` now holds the winner's key
            }
            if (current == stored) {
                if (!slot.ready.load(std::memory_order_acquire)) return Result::Pending;
                existing = slot.value.load(std::memory_order_relaxed);
                return existing == value ? Result::Match : Result::Differs;
            }
        }
        return Result::Full;
    }

private:
    static std::size_t Mix(std::uint64_t k) noexcept {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        return static_cast<std::size_t>(k);
    }

    struct Slot {
        std::atomic<std::uint64_t> key{0};
        std::atomic<std::uint64_t> value{0};
        std::atomic<bool> ready{false};
    };

    Slot slots_[N];
};

}  // namespace Vixen
