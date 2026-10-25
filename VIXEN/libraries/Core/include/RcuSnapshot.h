#pragma once

// ============================================================================
// RcuSnapshot<T> — immutable publish with deferred retirement (no-stall pattern 2).
//
// One current immutable T. Readers pin it for the duration of a use; writers build a NEW T off to
// the side and publish it with one atomic pointer swap. Replaced snapshots retire onto a list and
// are freed only once no reader is pinned, so a reader never waits for a writer and a writer never
// waits for a reader (a retired snapshot that is still pinned simply stays on the list until a
// later Reclaim() finds the readers gone).
//
//   Read():    fetch_add(readers) then load(current)           — wait-free
//   Publish(): exchange(current), push old onto retired list    — wait-free apart from the retire push
//   Update():  read-copy-update with a CAS publish. The CAS loop retries only when ANOTHER writer
//              published in between (setup-time writers, never a frame path).
//
// Safety argument: a reader increments `readers_` BEFORE loading `current_`; a reclaimer takes the
// retired list FIRST and checks `readers_ == 0` AFTER. Every node on the taken list was swapped out
// of `current_` before it was pushed, so a reader that could still hold one incremented earlier than
// the reclaimer's check and is observed. All of these operations are seq_cst.
// ============================================================================

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace Vixen {

template <class T>
class RcuSnapshot {
    struct Node {
        T value;
        Node* retiredNext = nullptr;
        template <class... Args>
        explicit Node(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

public:
    /// Pins the snapshot that was current when Read() ran; the snapshot cannot be freed while alive.
    class Reader {
    public:
        Reader(const Reader&) = delete;
        Reader& operator=(const Reader&) = delete;
        Reader(Reader&& other) noexcept : owner_(other.owner_), node_(other.node_) { other.owner_ = nullptr; }
        ~Reader() {
            if (owner_) owner_->readers_.fetch_sub(1, std::memory_order_seq_cst);
        }
        const T& operator*() const noexcept { return node_->value; }
        const T* operator->() const noexcept { return &node_->value; }
        const T* get() const noexcept { return &node_->value; }

    private:
        friend class RcuSnapshot;
        Reader(const RcuSnapshot* owner, Node* node) : owner_(owner), node_(node) {}
        const RcuSnapshot* owner_;
        Node* node_;
    };

    RcuSnapshot() : current_(new Node()) {}
    explicit RcuSnapshot(std::unique_ptr<T> initial) : current_(new Node(std::move(*initial))) {}
    RcuSnapshot(const RcuSnapshot&) = delete;
    RcuSnapshot& operator=(const RcuSnapshot&) = delete;

    ~RcuSnapshot() {
        delete current_.load(std::memory_order_relaxed);
        FreeList(retired_.exchange(nullptr, std::memory_order_acquire));
    }

    Reader Read() const noexcept {
        readers_.fetch_add(1, std::memory_order_seq_cst);
        return Reader(this, current_.load(std::memory_order_seq_cst));
    }

    /// Replace the current snapshot unconditionally (last writer wins).
    void Publish(T next) {
        Node* fresh = new Node(std::move(next));
        Node* old = current_.exchange(fresh, std::memory_order_seq_cst);
        Retire(old);
        Reclaim();
    }

    /// Read-copy-update: `fn(const T& current)` returns a std::unique_ptr<T> holding the replacement,
    /// or null to leave the snapshot unchanged. Returns true when a replacement was published.
    template <class Fn>
    bool Update(Fn&& fn) {
        bool published = false;
        {
            for (;;) {
                Reader reader = Read();
                Node* observed = reader.node_;
                std::unique_ptr<T> next = fn(reader.node_->value);
                if (!next) break;
                Node* fresh = new Node(std::move(*next));
                if (current_.compare_exchange_strong(observed, fresh, std::memory_order_seq_cst)) {
                    Retire(observed);
                    published = true;
                    break;
                }
                delete fresh;  // another writer published first: rebuild from its snapshot
            }
        }
        Reclaim();
        return published;
    }

    /// Free retired snapshots that no reader can still hold. Never waits: when a reader is pinned the
    /// retired snapshots are put back for a later call.
    void Reclaim() noexcept {
        Node* list = retired_.exchange(nullptr, std::memory_order_seq_cst);
        if (!list) return;
        if (readers_.load(std::memory_order_seq_cst) == 0) {
            FreeList(list);
            return;
        }
        while (list) {
            Node* next = list->retiredNext;
            Retire(list);
            list = next;
        }
    }

    /// Retired snapshots not yet freed (diagnostics / tests).
    std::size_t RetiredCountApprox() const noexcept {
        std::size_t n = 0;
        for (Node* it = retired_.load(std::memory_order_acquire); it; it = it->retiredNext) ++n;
        return n;
    }

private:
    void Retire(Node* node) noexcept {
        Node* head = retired_.load(std::memory_order_relaxed);
        do {
            node->retiredNext = head;
        } while (!retired_.compare_exchange_weak(head, node, std::memory_order_seq_cst, std::memory_order_relaxed));
    }

    static void FreeList(Node* list) noexcept {
        while (list) {
            Node* next = list->retiredNext;
            delete list;
            list = next;
        }
    }

    std::atomic<Node*> current_;
    std::atomic<Node*> retired_{nullptr};
    mutable std::atomic<std::int64_t> readers_{0};
};

}  // namespace Vixen
