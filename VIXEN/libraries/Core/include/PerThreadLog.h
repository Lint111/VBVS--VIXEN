#pragma once

// ============================================================================
// PerThreadLog<T> — thread-local append + merge-at-read (no-stall pattern 4).
//
// Every writing thread appends to a buffer only it writes; nothing is shared on the append path.
// A reader walks all buffers and sees every element whose publication it can observe: the writer
// constructs the element, then release-stores the buffer's count, and the reader acquire-loads the
// count and never reads past it. So reading while threads are still appending is race-free (the
// reader may miss the newest elements), and no lock is taken by either side.
//
//   Append(v)      — wait-free after the first call on a thread (the first call registers the
//                    thread's buffer with one CAS push; that is the only shared write)
//   ForEach(fn)    — merge: visits each buffer in registration order, each buffer in append order
//   Clear()        — logical reset: moves every buffer's read base to its current count. Storage is
//                    kept until the log is destroyed (this is for debug/instrumentation logs)
//
// T must be default-constructible and move/copy-assignable.
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

namespace Vixen {

template <class T>
class PerThreadLog {
    static constexpr std::size_t kChunkSize = 256;

    struct Chunk {
        T items[kChunkSize];
        std::atomic<Chunk*> next{nullptr};
    };

    struct Buffer {
        std::thread::id owner;
        Chunk* head = nullptr;
        Chunk* tail = nullptr;           // writer-only
        std::size_t tailFill = 0;        // writer-only
        std::atomic<std::size_t> count{0};  // published element count
        std::atomic<std::size_t> base{0};   // logical start (Clear)
        Buffer* nextBuffer = nullptr;       // immutable once registered
    };

public:
    PerThreadLog() : id_(NextId().fetch_add(1, std::memory_order_relaxed)) {}
    PerThreadLog(const PerThreadLog&) = delete;
    PerThreadLog& operator=(const PerThreadLog&) = delete;

    ~PerThreadLog() {
        Buffer* b = buffers_.load(std::memory_order_acquire);
        while (b) {
            Buffer* nextBuffer = b->nextBuffer;
            Chunk* c = b->head;
            while (c) {
                Chunk* nextChunk = c->next.load(std::memory_order_relaxed);
                delete c;
                c = nextChunk;
            }
            delete b;
            b = nextBuffer;
        }
    }

    template <class U>
    void Append(U&& value) {
        Buffer* b = LocalBuffer();
        if (b->tailFill == kChunkSize || !b->tail) {
            Chunk* fresh = new Chunk();
            if (b->tail) {
                b->tail->next.store(fresh, std::memory_order_release);
            } else {
                b->head = fresh;
            }
            b->tail = fresh;
            b->tailFill = 0;
        }
        b->tail->items[b->tailFill++] = std::forward<U>(value);
        b->count.store(b->count.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    /// Merge: fn(const T&) for every published element not cleared, buffer by buffer.
    template <class Fn>
    void ForEach(Fn&& fn) const {
        for (Buffer* b = buffers_.load(std::memory_order_acquire); b; b = b->nextBuffer) {
            const std::size_t count = b->count.load(std::memory_order_acquire);
            if (count == 0) continue;  // head is published by the first count store (release)
            const std::size_t base = b->base.load(std::memory_order_acquire);
            const Chunk* chunk = b->head;
            std::size_t index = 0;
            while (chunk && index < count) {
                const std::size_t inChunk = (count - index) < kChunkSize ? (count - index) : kChunkSize;
                for (std::size_t i = 0; i < inChunk; ++i) {
                    if (index + i >= base) fn(chunk->items[i]);
                }
                index += inChunk;
                chunk = chunk->next.load(std::memory_order_acquire);
            }
        }
    }

    std::size_t Size() const {
        std::size_t total = 0;
        for (Buffer* b = buffers_.load(std::memory_order_acquire); b; b = b->nextBuffer) {
            total += b->count.load(std::memory_order_acquire) - b->base.load(std::memory_order_acquire);
        }
        return total;
    }

    void Clear() {
        for (Buffer* b = buffers_.load(std::memory_order_acquire); b; b = b->nextBuffer) {
            b->base.store(b->count.load(std::memory_order_acquire), std::memory_order_release);
        }
    }

private:
    Buffer* LocalBuffer() {
        struct Cache {
            std::uint64_t logId = 0;
            Buffer* buffer = nullptr;
        };
        static thread_local Cache cache;
        if (cache.logId == id_) return cache.buffer;

        const std::thread::id self = std::this_thread::get_id();
        for (Buffer* b = buffers_.load(std::memory_order_acquire); b; b = b->nextBuffer) {
            if (b->owner == self) {
                cache = {id_, b};
                return b;
            }
        }
        Buffer* fresh = new Buffer();
        fresh->owner = self;
        Buffer* head = buffers_.load(std::memory_order_relaxed);
        do {
            fresh->nextBuffer = head;
        } while (!buffers_.compare_exchange_weak(head, fresh, std::memory_order_release, std::memory_order_relaxed));
        cache = {id_, fresh};
        return fresh;
    }

    static std::atomic<std::uint64_t>& NextId() {
        static std::atomic<std::uint64_t> next{1};
        return next;
    }

    const std::uint64_t id_;  // never reused, so a thread's cached buffer cannot alias a later log
    std::atomic<Buffer*> buffers_{nullptr};
};

}  // namespace Vixen
