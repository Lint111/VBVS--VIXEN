#pragma once

#include "CacherBase.h"
#include "ILoggable.h"
#include "CacherAllocationHelpers.h"
#include "Memory/DeviceBudgetManager.h"
#include "Memory/IMemoryAllocator.h"
#include "VulkanDevice.h"
// Note: BatchedUploader now owned by VulkanDevice (Sprint 5 Phase 2.5.3)
// Access via m_device->Upload() instead of GetUploader()

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <typeindex>
#include <vector>

#ifdef _DEBUG
#include "VixenHash.h"  // For collision detection
#include <mutex>
#include <unordered_map>
#endif

// Forward declarations for type safety
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {

// TypedCacher<D, CI>
// D: resource wrapper type (e.g., PipelineWrapper)
// CI: creation-info struct used to create D
//
// Publication model — lock-free federation §2.4 (P4, CACHE/MEMO → immutable generations):
//
//   * One builder per key. GetOrCreate() looks the key up without any synchronization; on a miss
//     it claims the key's slot with a single CAS on a cell of the current generation table. The
//     CAS winner is the key's only builder and runs Create() outside every shared structure.
//     Everyone else who arrives at the same key finds the winner's slot and waits on that slot's
//     state word HOLDING NOTHING (std::atomic::wait) — there is no cacher-wide lock to hold, so
//     the six same-key `future.get()`-under-shared-lock deadlock paths of the inventory
//     (finding #2) cannot be written against this class.
//   * Immutable generations. A generation is an open-addressed table of slot pointers whose cells
//     only ever go null → slot; a slot's key/ci are written before it is published and never
//     change; its resource is written once, before the state word flips to Ready. When a table
//     reaches its load threshold the claimer publishes a larger successor whose `prev` is the
//     old one; readers probe newest → oldest, so an old generation stays valid and is never
//     copied or moved. Erase()/a failed build only tombstone a slot (state flip), never unlink.
//   * Invalidation is an epoch flip. Clear() swaps in an empty generation; the retired chain is
//     kept until the cacher is destroyed (retirement after the last reader — today the readers'
//     quiescence point is teardown, the assembler's wave boundary later), so a reader that is
//     still probing the old chain is never freed from under it.
//
// Same-key rule (design §2.4): equal key ⇒ interchangeable value, so a single builder's result is
// canonical and no arbitration beyond the claim CAS is needed.
template<typename D, typename CI>
class TypedCacher : public CacherBase, public ILoggable {
public:
    using ResourceT = D;
    using CreateInfoT = CI;
    using PtrT = std::shared_ptr<ResourceT>;

    TypedCacher() : m_head(new Table(kInitialCapacity, nullptr)), m_device(nullptr), m_initialized(false) {}

    virtual ~TypedCacher() {
        DeleteChain(m_head.load(std::memory_order_acquire));
        Table* retired = m_retired.load(std::memory_order_acquire);
        while (retired) {
            Table* next = retired->retiredNext;
            DeleteChain(retired);
            retired = next;
        }
    }

    TypedCacher(const TypedCacher&) = delete;
    TypedCacher& operator=(const TypedCacher&) = delete;

    /**
     * @brief Initialize the cacher with device context
     */
    virtual void Initialize(Vixen::Vulkan::Resources::VulkanDevice* device) {
        m_device = device;
        m_initialized = true;
        OnInitialize();
    }

    /**
     * @brief Check if the cacher has been initialized
     * @note For device-dependent cachers, both flag and device pointer must be valid
     */
    bool IsInitialized() const noexcept { return m_initialized && m_device != nullptr; }

    /**
     * @brief Get the device context
     */
    Vixen::Vulkan::Resources::VulkanDevice* GetDevice() const noexcept { return m_device; }

    // Typed convenience API — callers should use this.
    // Hit: one lock-free probe. Miss: claim the key's builder slot (one CAS), build outside any
    // shared state, publish by flipping the slot's state word. Same-key concurrent callers wait
    // on that word holding nothing.
    PtrT GetOrCreate(const CreateInfoT& ci) {
        const std::uint64_t key = ComputeKey(ci);

#ifdef _DEBUG
        // Collision detection: check if same key maps to different content
        CheckCollision(key, ci);
#endif

        for (;;) {
            Claim claim = ClaimBuilder(key, ci);
            if (claim.role == Role::Retry) continue;
            if (claim.role == Role::Waiter) {
                if (std::optional<PtrT> ready = AwaitSlot(claim.slot)) return *ready;
                continue;  // the slot we waited on was retracted/erased: look the key up again
            }
            return Build(claim.slot, ci);
        }
    }

    // CacherBase overrides (typed mapping)
    bool Has(std::uint64_t key) const noexcept override {
        return Find(key) != nullptr;
    }

    std::shared_ptr<void> Get(std::uint64_t key) override {
        return Find(key);
    }

    std::shared_ptr<void> Insert(std::uint64_t key, const std::any& creationParams) override {
        // try to cast creationParams to CI
        try {
            auto ci = std::any_cast<CreateInfoT>(creationParams);
            PtrT created = Create(ci);
            return Publish(key, ci, created);
        } catch (const std::bad_any_cast&) {
            return nullptr;
        }
    }

    // Tombstone the key's live slot. Lookups skip it from now on; the next GetOrCreate for the key
    // claims a fresh slot. Nothing is unlinked (readers may still be probing past the slot).
    void Erase(std::uint64_t key) override {
        Slot* slot = FindSlot(m_head.load(std::memory_order_seq_cst), key);
        if (!slot) return;
        std::uint32_t expected = slot->state.load(std::memory_order_acquire);
        while (IsLive(expected)) {
            if (slot->state.compare_exchange_weak(expected, kErased,
                                                  std::memory_order_acq_rel, std::memory_order_acquire)) {
                slot->state.notify_all();
                return;
            }
        }
    }

    // Epoch flip: publish an empty generation. The previous chain is retired, not freed — a reader
    // still probing it stays valid — and is released with the cacher.
    void Clear() override {
        Table* fresh = new Table(kInitialCapacity, nullptr);
        Table* old = m_head.exchange(fresh, std::memory_order_seq_cst);
        old->retiredNext = m_retired.load(std::memory_order_relaxed);
        while (!m_retired.compare_exchange_weak(old->retiredNext, old,
                                                std::memory_order_acq_rel, std::memory_order_relaxed)) {
        }
    }

    void Cleanup() override {
        // Default implementation: just clear the cache
        // Derived classes should override to destroy Vulkan resources before clearing
        Clear();
    }

    bool SerializeToFile(const std::filesystem::path& path) const override {
        // default stub: derived classes should override if they need real serialization
        (void)path;
        return true;
    }

    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override {
        (void)path; (void)device;
        return true;
    }

    std::string_view name() const noexcept override { return "TypedCacher"; }

    /// Number of published (Ready) entries across the current generation chain.
    std::size_t EntryCount() const noexcept {
        std::size_t n = 0;
        ForEachEntry([&](const CacheEntry&) { ++n; });
        return n;
    }

protected:
    // Derived must implement how to create and how to compute keys
    virtual PtrT Create(const CreateInfoT& ci) = 0;
    virtual std::uint64_t ComputeKey(const CreateInfoT& ci) const = 0;

    struct CacheEntry {
        std::uint64_t key;
        CreateInfoT ci;
        PtrT resource;
    };

    /// Lock-free lookup of a published entry. nullptr = absent, pending, erased or failed.
    PtrT Find(std::uint64_t key) const noexcept {
        Slot* slot = FindSlot(m_head.load(std::memory_order_seq_cst), key);
        if (!slot || slot->state.load(std::memory_order_acquire) != kReady) return nullptr;
        return slot->resource;
    }

    /// Visit every published entry of the current generation chain (newest table first; each live
    /// key appears exactly once). `fn(const CacheEntry&)`. Entries published concurrently with
    /// the walk may or may not be visited — a persistence/teardown walk runs at a quiescent point.
    template<typename Fn>
    void ForEachEntry(Fn&& fn) const {
        for (Table* t = m_head.load(std::memory_order_seq_cst); t; t = t->prev) {
            for (std::size_t i = 0; i < t->capacity; ++i) {
                Slot* slot = t->cells[i].load(std::memory_order_seq_cst);
                if (slot && slot->state.load(std::memory_order_acquire) == kReady) {
                    fn(static_cast<const CacheEntry&>(*slot));
                }
            }
        }
    }

    /// Copy of every published entry — for serializers that write a count before the rows.
    std::vector<CacheEntry> Snapshot() const {
        std::vector<CacheEntry> out;
        ForEachEntry([&](const CacheEntry& e) { out.push_back(e); });
        return out;
    }

    /// Publish a pre-built resource under `key` (deserialization / Insert). If the key already has
    /// a live slot, that slot wins (same key ⇒ interchangeable value) and its resource is returned.
    PtrT Publish(std::uint64_t key, const CreateInfoT& ci, PtrT resource) {
        for (;;) {
            Claim claim = ClaimBuilder(key, ci);
            if (claim.role == Role::Retry) continue;
            if (claim.role == Role::Waiter) {
                if (std::optional<PtrT> ready = AwaitSlot(claim.slot)) return *ready;
                continue;
            }
            claim.slot->resource = resource;
            std::uint32_t expected = kPending;
            claim.slot->state.compare_exchange_strong(expected, kReady,
                                                      std::memory_order_acq_rel, std::memory_order_acquire);
            claim.slot->state.notify_all();
            return resource;
        }
    }

    // Device context and initialization tracking
    Vixen::Vulkan::Resources::VulkanDevice* m_device;
    bool m_initialized;

private:
    // ---- generation storage -------------------------------------------------------------------

    // Slot state word. Pending/Ready are "live" (the key is claimed); the rest are tombstones a
    // probe skips. A tombstoned key is re-claimed by a later requester in a fresh cell.
    static constexpr std::uint32_t kPending   = 0;  // builder running; waiters wait on this word
    static constexpr std::uint32_t kReady     = 1;  // resource published (may be null)
    static constexpr std::uint32_t kFailed    = 2;  // Create() threw; `error` holds the exception
    static constexpr std::uint32_t kRetracted = 3;  // claim landed in a table that stopped being head
    static constexpr std::uint32_t kErased    = 4;  // Erase()

    static constexpr bool IsLive(std::uint32_t s) noexcept { return s == kPending || s == kReady; }

    struct Slot : CacheEntry {
        std::atomic<std::uint32_t> state{kPending};
        std::exception_ptr error;
    };

    static constexpr std::size_t kInitialCapacity = 64;  // cells; power of two

    struct Table {
        const std::size_t capacity;
        const std::size_t mask;
        const std::size_t threshold;                        // claims admitted before growth (≤ capacity/2)
        std::unique_ptr<std::atomic<Slot*>[]> cells;        // null → Slot*, never back
        std::atomic<std::size_t> claims{0};                 // admission counter (fetch_add)
        Table* const prev;                                  // older generation (probed after this one)
        Table* retiredNext = nullptr;                       // Clear() retirement stack link

        Table(std::size_t cap, Table* older)
            : capacity(cap), mask(cap - 1), threshold(cap / 2),
              cells(new std::atomic<Slot*>[cap]), prev(older) {
            for (std::size_t i = 0; i < cap; ++i) cells[i].store(nullptr, std::memory_order_relaxed);
        }
        ~Table() {
            for (std::size_t i = 0; i < capacity; ++i) delete cells[i].load(std::memory_order_relaxed);
        }
        Table(const Table&) = delete;
        Table& operator=(const Table&) = delete;
    };

    /// Free a generation chain (newest first). Only ever called from the destructor.
    static void DeleteChain(Table* t) noexcept {
        while (t) {
            Table* older = t->prev;
            delete t;
            t = older;
        }
    }

    static std::size_t Home(std::uint64_t key, std::size_t mask) noexcept {
        // Fibonacci mix: ComputeKey() may be a small integer as well as a full-width hash.
        return static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ull) >> 20) & mask;
    }

    /// Probe `table` for a live slot with `key`. Cells are seq_cst loads so a claim published by a
    /// grower's predecessor is ordered against the grower's head swap (Dekker pair with Claim()).
    static Slot* ProbeTable(const Table* table, std::uint64_t key) noexcept {
        std::size_t i = Home(key, table->mask);
        for (;;) {
            Slot* slot = table->cells[i].load(std::memory_order_seq_cst);
            if (!slot) return nullptr;
            if (slot->key == key && IsLive(slot->state.load(std::memory_order_acquire))) return slot;
            i = (i + 1) & table->mask;
        }
    }

    static Slot* FindSlot(Table* head, std::uint64_t key) noexcept {
        for (Table* t = head; t; t = t->prev) {
            if (Slot* slot = ProbeTable(t, key)) return slot;
        }
        return nullptr;
    }

    enum class Role { Builder, Waiter, Retry };
    struct Claim { Role role; Slot* slot; };

    /// Look the key up across the chain; on a miss claim its builder slot in the head table.
    ///   Builder — `slot` is ours to fill (state Pending, key/ci set).
    ///   Waiter  — `slot` is another thread's live slot for the key.
    ///   Retry   — the head table grew (or was cleared) under us; look up again.
    Claim ClaimBuilder(std::uint64_t key, const CreateInfoT& ci) {
        Table* head = m_head.load(std::memory_order_seq_cst);
        if (Slot* existing = FindSlot(head, key)) return {Role::Waiter, existing};

        if (head->claims.fetch_add(1, std::memory_order_relaxed) >= head->threshold) {
            Grow(head);
            return {Role::Retry, nullptr};
        }

        std::unique_ptr<Slot> mine(new Slot());
        mine->key = key;
        mine->ci = ci;

        std::size_t i = Home(key, head->mask);
        for (;;) {
            std::atomic<Slot*>& cell = head->cells[i];
            Slot* current = cell.load(std::memory_order_seq_cst);
            if (!current) {
                if (cell.compare_exchange_strong(current, mine.get(),
                                                 std::memory_order_seq_cst, std::memory_order_seq_cst)) {
                    Slot* claimed = mine.release();  // the table owns it now
                    if (m_head.load(std::memory_order_seq_cst) != head) {
                        // The chain moved between our lookup and our claim: a grower's post-swap
                        // lookup may have missed this cell. Retract so it can never be a second
                        // builder for the key (waiters on it retry).
                        claimed->state.store(kRetracted, std::memory_order_release);
                        claimed->state.notify_all();
                        return {Role::Retry, nullptr};
                    }
                    return {Role::Builder, claimed};
                }
                // lost the cell — `current` is the winner; inspect it like any other occupant
            }
            if (current->key == key && IsLive(current->state.load(std::memory_order_acquire))) {
                return {Role::Waiter, current};
            }
            i = (i + 1) & head->mask;
        }
    }

    /// Publish a successor table twice the size; a concurrent grower's success is ours too.
    void Grow(Table* head) {
        Table* fresh = new Table(head->capacity * 2, head);
        Table* expected = head;
        if (!m_head.compare_exchange_strong(expected, fresh, std::memory_order_seq_cst, std::memory_order_seq_cst)) {
            delete fresh;  // another thread already replaced `head`; ours holds no slots
        }
    }

    /// Wait for another thread's slot holding nothing. Ready → its resource; Failed → the
    /// builder's exception; retracted/erased → nullopt (caller looks the key up again).
    std::optional<PtrT> AwaitSlot(Slot* slot) {
        std::uint32_t state = slot->state.load(std::memory_order_acquire);
        while (state == kPending) {
            slot->state.wait(kPending, std::memory_order_acquire);
            state = slot->state.load(std::memory_order_acquire);
        }
        switch (state) {
            case kReady:  return slot->resource;
            case kFailed: std::rethrow_exception(slot->error);
            default:      return std::nullopt;
        }
    }

    /// Run Create() as the key's builder and publish the result (or the failure) on the slot.
    PtrT Build(Slot* slot, const CreateInfoT& ci) {
        PtrT created;
        try {
            created = Create(ci);
        } catch (...) {
            slot->error = std::current_exception();
            slot->state.store(kFailed, std::memory_order_release);
            slot->state.notify_all();
            throw;
        }
        slot->resource = created;
        std::uint32_t expected = kPending;
        // An Erase() that raced the build wins: the resource is returned but not published.
        slot->state.compare_exchange_strong(expected, kReady, std::memory_order_acq_rel, std::memory_order_acquire);
        slot->state.notify_all();
        return created;
    }

    std::atomic<Table*> m_head;
    std::atomic<Table*> m_retired{nullptr};

#ifdef _DEBUG
    // Collision detection: maps cache key -> content hash of CreateInfo
    // If same key appears with different content hash, we have a collision
    mutable std::unordered_map<std::uint64_t, std::uint64_t> m_debugContentHashes;
    mutable std::mutex m_debugMutex;

    /**
     * @brief Compute content hash of CreateInfo for collision detection
     *
     * Uses raw bytes of the struct. This won't catch all collisions
     * (e.g., if CreateInfo contains pointers), but catches most cases.
     */
    std::uint64_t ComputeContentHash(const CreateInfoT& ci) const {
        return ::Vixen::Hash::ComputeHash64(&ci, sizeof(CreateInfoT));
    }

    /**
     * @brief Check for hash collisions in debug builds
     *
     * Logs an error if the same cache key maps to different CreateInfo content.
     * This indicates a bug in ComputeKey() implementation.
     */
    void CheckCollision(std::uint64_t key, const CreateInfoT& ci) const {
        std::uint64_t contentHash = ComputeContentHash(ci);

        std::lock_guard<std::mutex> lock(m_debugMutex);
        auto it = m_debugContentHashes.find(key);
        if (it != m_debugContentHashes.end()) {
            if (it->second != contentHash) {
                // COLLISION DETECTED!
                LOG_ERROR("[" + std::string(name()) + "] HASH COLLISION DETECTED! "
                          "Key=" + std::to_string(key) +
                          " has different content (existing hash=" + std::to_string(it->second) +
                          ", new hash=" + std::to_string(contentHash) + "). "
                          "This indicates a bug in ComputeKey() implementation.");
            }
        } else {
            m_debugContentHashes[key] = contentHash;
        }
    }
#endif

protected:
    // Hook for derived classes to perform initialization
    virtual void OnInitialize() {}

public:
    /**
     * @brief Set budget manager for GPU allocation tracking
     * @deprecated Budget manager is now accessed via m_device->GetBudgetManager()
     * @param manager Ignored - budget manager comes from VulkanDevice
     */
    [[deprecated("Budget manager is accessed via VulkanDevice - this method is a no-op")]]
    void SetBudgetManager(ResourceManagement::DeviceBudgetManager* /*manager*/) {
        // No-op: budget manager comes from VulkanDevice
    }

    /**
     * @brief Get budget manager for GPU allocation tracking
     * @return DeviceBudgetManager pointer from VulkanDevice, or nullptr if not configured
     */
    ResourceManagement::DeviceBudgetManager* GetBudgetManager() const {
        return m_device ? m_device->GetBudgetManager() : nullptr;
    }

    // Note: Upload API moved to VulkanDevice (Sprint 5 Phase 2.5.3)
    // Use m_device->Upload() and m_device->WaitAllUploads() instead

protected:
    /**
     * @brief Allocate buffer using budget-tracked allocator if available
     *
     * Falls back to direct Vulkan allocation if no budget manager configured.
     * This provides backward compatibility while enabling budget tracking.
     *
     * @param size Buffer size in bytes
     * @param usage Vulkan buffer usage flags
     * @param memoryFlags Vulkan memory property flags
     * @param debugName Optional debug name for the allocation
     * @return BufferAllocation on success, or empty optional on failure
     *
     * @note When budget manager is available:
     *   - Uses IMemoryAllocator for tracked allocation
     *   - allocation.buffer is valid, allocation.memory may be VK_NULL_HANDLE (managed by allocator)
     * @note When no budget manager:
     *   - Uses direct Vulkan calls (vkCreateBuffer + vkAllocateMemory)
     *   - allocation.buffer and allocation.memory are both valid
     */
    std::optional<ResourceManagement::BufferAllocation> AllocateBufferTracked(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memoryFlags,
        const char* debugName = nullptr
    ) {
        return CacherAllocationHelpers::AllocateBuffer(
            GetBudgetManager(), m_device, size, usage, memoryFlags, debugName);
    }

    /**
     * @brief Free buffer using appropriate path based on how it was allocated
     *
     * @param allocation The allocation to free
     * @note Safe to call with invalid/empty allocation
     */
    void FreeBufferTracked(ResourceManagement::BufferAllocation& allocation) {
        CacherAllocationHelpers::FreeBuffer(GetBudgetManager(), m_device, allocation);
    }

    /**
     * @brief Map buffer memory for CPU access
     *
     * Works with both budget-tracked and direct allocations.
     *
     * @param allocation Buffer allocation to map
     * @return Mapped pointer or nullptr on failure
     */
    void* MapBufferTracked(ResourceManagement::BufferAllocation& allocation) {
        return CacherAllocationHelpers::MapBuffer(GetBudgetManager(), m_device, allocation);
    }

    /**
     * @brief Unmap previously mapped buffer memory
     *
     * @param allocation Buffer allocation to unmap
     */
    void UnmapBufferTracked(ResourceManagement::BufferAllocation& allocation) {
        CacherAllocationHelpers::UnmapBuffer(GetBudgetManager(), m_device, allocation);
    }

    /**
     * @brief Helper to convert VkMemoryPropertyFlags to MemoryLocation
     */
    static ResourceManagement::MemoryLocation MemoryFlagsToLocation(VkMemoryPropertyFlags flags) {
        return CacherAllocationHelpers::MemoryFlagsToLocation(flags);
    }
};

} // namespace CashSystem

