#pragma once

#include "CacherBase.h"
#include "ILoggable.h"
#include "CacherAllocationHelpers.h"
#include "Memory/DeviceBudgetManager.h"
#include "Memory/IMemoryAllocator.h"
// Note: BatchedUploader now owned by VulkanDevice (Sprint 5 Phase 2.5.3)
// Access via m_device->Upload() instead of GetUploader()

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <future>
#include <optional>
#include <vector>
#include <cstdint>
#include <typeindex>

#ifdef _DEBUG
#include "VixenHash.h"  // For collision detection
#endif

// Forward declarations for type safety
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {

// TypedCacher<D, CI>
// D: resource wrapper type (e.g., PipelineWrapper)
// CI: creation-info struct used to create D
template<typename D, typename CI>
class TypedCacher : public CacherBase, public ILoggable {
public:
    using ResourceT = D;
    using CreateInfoT = CI;
    using PtrT = std::shared_ptr<ResourceT>;

    TypedCacher() : m_device(nullptr), m_initialized(false) {}
    virtual ~TypedCacher() = default;

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

    // Typed convenience API — callers should use this
    PtrT GetOrCreate(const CreateInfoT& ci) {
        auto key = ComputeKey(ci);

#ifdef _DEBUG
        // Collision detection: check if same key maps to different content
        CheckCollision(key, ci);
#endif

        { // fast path read
            std::shared_lock rlock(m_lock);
            auto it = m_entries.find(key);
            if (it != m_entries.end()) {
                return it->second.resource;
            }
            auto pit = m_pending.find(key);
            if (pit != m_pending.end()) return pit->second.get();
        }

        // try to create
        std::unique_lock wlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) return pit->second.get();

        // create a promise to signal others
        auto prom = std::make_shared<std::promise<PtrT>>();
        std::shared_future<PtrT> fut = prom->get_future().share();
        m_pending.emplace(key, fut);

        // unlock during heavy create work
        wlock.unlock();

        PtrT created = Create(ci);

        // re-lock and insert
        wlock.lock();
        CacheEntry e; e.key = key; e.ci = ci; e.resource = created;
        m_entries.emplace(key, std::move(e));

        // fulfill promise and remove pending
        prom->set_value(created);
        m_pending.erase(key);
        return created;
    }

    // CacherBase overrides (typed mapping)
    bool Has(std::uint64_t key) const noexcept override {
        std::shared_lock lock(m_lock);
        return m_entries.find(key) != m_entries.end();
    }

    std::shared_ptr<void> Get(std::uint64_t key) override {
        std::shared_lock lock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) return it->second.resource;
        return nullptr;
    }

    std::shared_ptr<void> Insert(std::uint64_t key, const std::any& creationParams) override {
        // try to cast creationParams to CI
        try {
            auto ci = std::any_cast<CreateInfoT>(creationParams);
            PtrT created = Create(ci);
            std::unique_lock lock(m_lock);
            CacheEntry e; e.key = key; e.ci = ci; e.resource = created;
            m_entries.emplace(key, std::move(e));
            return created;
        } catch (const std::bad_any_cast&) {
            return nullptr;
        }
    }

    void Erase(std::uint64_t key) override {
        std::unique_lock lock(m_lock);
        m_entries.erase(key);
    }

    void Clear() override {
        std::unique_lock lock(m_lock);
        m_entries.clear();
        m_pending.clear();
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

protected:
    // Derived must implement how to create and how to compute keys
    virtual PtrT Create(const CreateInfoT& ci) = 0;
    virtual std::uint64_t ComputeKey(const CreateInfoT& ci) const = 0;

    struct CacheEntry {
        std::uint64_t key;
        CreateInfoT ci;
        PtrT resource;
    };

    mutable std::shared_mutex m_lock;
    std::unordered_map<std::uint64_t, CacheEntry> m_entries;
    std::unordered_map<std::uint64_t, std::shared_future<PtrT>> m_pending;

    // Device context and initialization tracking
    Vixen::Vulkan::Resources::VulkanDevice* m_device;
    bool m_initialized;

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

    // Sprint 4 Phase D: Budget manager for tracked allocations
    ResourceManagement::DeviceBudgetManager* m_budgetManager = nullptr;

    // Hook for derived classes to perform initialization
    virtual void OnInitialize() {}

public:
    /**
     * @brief Set budget manager for GPU allocation tracking
     * @param manager DeviceBudgetManager pointer (externally owned)
     */
    void SetBudgetManager(ResourceManagement::DeviceBudgetManager* manager) {
        m_budgetManager = manager;
    }

    /**
     * @brief Get budget manager for GPU allocation tracking
     * @return DeviceBudgetManager pointer, or nullptr if not configured
     */
    ResourceManagement::DeviceBudgetManager* GetBudgetManager() const {
        return m_budgetManager;
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
            m_budgetManager, m_device, size, usage, memoryFlags, debugName);
    }

    /**
     * @brief Free buffer using appropriate path based on how it was allocated
     *
     * @param allocation The allocation to free
     * @note Safe to call with invalid/empty allocation
     */
    void FreeBufferTracked(ResourceManagement::BufferAllocation& allocation) {
        CacherAllocationHelpers::FreeBuffer(m_budgetManager, m_device, allocation);
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
        return CacherAllocationHelpers::MapBuffer(m_budgetManager, m_device, allocation);
    }

    /**
     * @brief Unmap previously mapped buffer memory
     *
     * @param allocation Buffer allocation to unmap
     */
    void UnmapBufferTracked(ResourceManagement::BufferAllocation& allocation) {
        CacherAllocationHelpers::UnmapBuffer(m_budgetManager, m_device, allocation);
    }

    /**
     * @brief Helper to convert VkMemoryPropertyFlags to MemoryLocation
     */
    static ResourceManagement::MemoryLocation MemoryFlagsToLocation(VkMemoryPropertyFlags flags) {
        return CacherAllocationHelpers::MemoryFlagsToLocation(flags);
    }
};

} // namespace CashSystem

