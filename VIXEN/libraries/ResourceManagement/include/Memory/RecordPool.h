#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace ResourceManagement {

/**
 * @brief Lock-free, type-stable pool of fixed-size records handed out as stable pointers.
 *
 * Replaces "mutex + unordered_map<handle, record>" registries. The record IS the handle: Acquire()
 * returns a pointer that stays valid (memory-wise) for the pool's whole life, Release() puts the
 * slot on a tagged free stack for reuse, and Owns() answers "is this pointer one of our slots"
 * without dereferencing it, so an invalid or already-released handle never touches foreign memory.
 *
 * Slots live in append-only chunks installed by CAS and are never freed before the pool, so a
 * racing reader of a just-released slot reads valid (possibly recycled) memory, never freed memory.
 * Footprint is bounded by the peak live count, not by the number of allocations ever made.
 *
 * Capacity is capped (kMaxChunks * kChunkSize slots): Acquire() returns nullptr past the cap and
 * the caller reports its out-of-memory error; nothing blocks and nothing throws.
 *
 * T must expose `std::atomic<bool> live` (set by the owner after filling the record, cleared by
 * the releasing thread's exchange), `uint32_t poolIndex` and `uint32_t nextFree` (pool-owned).
 */
template <typename T>
class RecordPool {
public:
    static constexpr uint32_t kChunkSize = 256;
    static constexpr uint32_t kMaxChunks = 1024;  // 262144 slots

    RecordPool() = default;
    RecordPool(const RecordPool&) = delete;
    RecordPool& operator=(const RecordPool&) = delete;

    ~RecordPool() {
        for (auto& slot : chunks_) {
            delete[] slot.load(std::memory_order_relaxed);
        }
    }

    /// A slot ready to fill (its `live` flag is false until the caller publishes it), or nullptr
    /// when the pool is at its cap.
    T* Acquire() {
        if (T* recycled = PopFree()) return recycled;

        const uint32_t index = fresh_.fetch_add(1, std::memory_order_relaxed);
        if (index >= kChunkSize * kMaxChunks) {
            fresh_.fetch_sub(1, std::memory_order_relaxed);
            return nullptr;
        }
        T* slot = &EnsureChunk(index / kChunkSize)[index % kChunkSize];
        slot->poolIndex = index;
        return slot;
    }

    /// Return a slot for reuse. The caller has already cleared `live` and finished reading it.
    void Release(T* slot) {
        uint64_t head = freeHead_.load(std::memory_order_acquire);
        for (;;) {
            slot->nextFree = static_cast<uint32_t>(head & 0xFFFFFFFFu);
            const uint64_t desired = (((head >> 32) + 1) << 32) | (slot->poolIndex + 1);
            if (freeHead_.compare_exchange_weak(head, desired,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                return;
            }
        }
    }

    /// True when `pointer` addresses a slot of this pool (never dereferences it).
    [[nodiscard]] bool Owns(const void* pointer) const noexcept {
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const uint32_t used = fresh_.load(std::memory_order_acquire);
        const uint32_t chunkCount = (used + kChunkSize - 1) / kChunkSize;
        for (uint32_t c = 0; c < chunkCount && c < kMaxChunks; ++c) {
            const T* chunk = chunks_[c].load(std::memory_order_acquire);
            if (!chunk) continue;
            const auto begin = reinterpret_cast<std::uintptr_t>(chunk);
            const auto end = begin + sizeof(T) * kChunkSize;
            if (address >= begin && address < end) {
                return (address - begin) % sizeof(T) == 0;
            }
        }
        return false;
    }

    /// Visit every slot ever carved (live or not). Teardown only: not safe against concurrent use.
    template <typename Fn>
    void ForEachSlot(Fn&& fn) {
        const uint32_t used = fresh_.load(std::memory_order_acquire);
        for (uint32_t index = 0; index < used; ++index) {
            fn(chunks_[index / kChunkSize].load(std::memory_order_acquire)[index % kChunkSize]);
        }
    }

private:
    T* EnsureChunk(uint32_t chunkIndex) {
        T* chunk = chunks_[chunkIndex].load(std::memory_order_acquire);
        if (chunk) return chunk;
        T* fresh = new T[kChunkSize];
        if (chunks_[chunkIndex].compare_exchange_strong(chunk, fresh,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return fresh;
        }
        delete[] fresh;  // lost the install race; use the winner's chunk
        return chunk;
    }

    T* PopFree() {
        uint64_t head = freeHead_.load(std::memory_order_acquire);
        for (;;) {
            const uint32_t top = static_cast<uint32_t>(head & 0xFFFFFFFFu);
            if (top == 0) return nullptr;
            T* slot = &chunks_[(top - 1) / kChunkSize].load(std::memory_order_acquire)[(top - 1) % kChunkSize];
            const uint64_t desired = ((head >> 32) << 32) | slot->nextFree;
            if (freeHead_.compare_exchange_weak(head, desired,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                return slot;
            }
        }
    }

    std::array<std::atomic<T*>, kMaxChunks> chunks_{};
    std::atomic<uint32_t> fresh_{0};
    std::atomic<uint64_t> freeHead_{0};  // (tag << 32) | (slot index + 1); 0 = empty
};

} // namespace ResourceManagement
