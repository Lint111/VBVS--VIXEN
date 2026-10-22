#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace ResourceManagement {

/**
 * @brief Resource type categories for budget tracking
 */
enum class BudgetResourceType : uint8_t {
    HostMemory,      // System RAM
    DeviceMemory,    // GPU VRAM
    CommandBuffers,  // Vulkan command buffers
    Descriptors,     // Descriptor sets/pools
    UserDefined      // Custom resource types
};

/**
 * @brief Budget constraint for a specific resource type
 */
struct ResourceBudget {
    uint64_t maxBytes = 0;        // Maximum allowed allocation (0 = unlimited)
    uint64_t warningThreshold = 0; // Warn when usage exceeds this (0 = no warning)
    bool strict = false;           // If true, fail allocation when over limit

    ResourceBudget() = default;
    ResourceBudget(uint64_t max, uint64_t warning = 0, bool strictMode = false)
        : maxBytes(max), warningThreshold(warning), strict(strictMode) {}
};

/**
 * @brief Current resource usage statistics
 */
struct BudgetResourceUsage {
    uint64_t currentBytes = 0;    // Currently allocated
    uint64_t peakBytes = 0;       // Peak allocation
    uint32_t allocationCount = 0; // Number of active allocations

    void Reset() {
        currentBytes = 0;
        peakBytes = 0;
        allocationCount = 0;
    }
};

/**
 * @brief Thread-safe atomic usage counters for fast-path operations
 */
struct AtomicResourceUsage {
    std::atomic<uint64_t> currentBytes{0};
    std::atomic<uint64_t> peakBytes{0};
    std::atomic<uint32_t> allocationCount{0};

    AtomicResourceUsage() = default;

    // Non-copyable due to atomics, but movable for map insertion
    AtomicResourceUsage(const AtomicResourceUsage& other)
        : currentBytes(other.currentBytes.load(std::memory_order_relaxed))
        , peakBytes(other.peakBytes.load(std::memory_order_relaxed))
        , allocationCount(other.allocationCount.load(std::memory_order_relaxed)) {}

    AtomicResourceUsage& operator=(const AtomicResourceUsage& other) {
        if (this != &other) {
            currentBytes.store(other.currentBytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
            peakBytes.store(other.peakBytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
            allocationCount.store(other.allocationCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }

    void Reset() {
        currentBytes.store(0, std::memory_order_relaxed);
        peakBytes.store(0, std::memory_order_relaxed);
        allocationCount.store(0, std::memory_order_relaxed);
    }

    BudgetResourceUsage ToUsage() const {
        BudgetResourceUsage usage;
        usage.currentBytes = currentBytes.load(std::memory_order_acquire);
        usage.peakBytes = peakBytes.load(std::memory_order_acquire);
        usage.allocationCount = allocationCount.load(std::memory_order_acquire);
        return usage;
    }
};

/**
 * @brief Phase F.1: Resource Budget Manager (Thread-Safe)
 *
 * Tracks and enforces resource usage limits for:
 * - Host memory (system RAM)
 * - Device memory (GPU VRAM)
 * - Command buffers, descriptor sets
 * - User-defined resource types
 *
 * Features:
 * - Per-resource-type budgets with soft/hard limits
 * - Runtime usage tracking and peak monitoring
 * - Warning thresholds for approaching limits
 * - Optional strict enforcement (fail allocations over budget)
 * - Query available budget before allocation
 * - Thread-safe for concurrent allocation/deallocation without locks: standard types are a
 *   fixed array of atomic slots; custom types are an append-only lock-free list
 */
class ResourceBudgetManager {
public:
    ResourceBudgetManager() = default;
    ~ResourceBudgetManager();
    ResourceBudgetManager(const ResourceBudgetManager&) = delete;
    ResourceBudgetManager& operator=(const ResourceBudgetManager&) = delete;

    // Budget configuration
    void SetBudget(BudgetResourceType type, const ResourceBudget& budget);
    void SetBudget(const std::string& customType, const ResourceBudget& budget);

    std::optional<ResourceBudget> GetBudget(BudgetResourceType type) const;
    std::optional<ResourceBudget> GetBudget(const std::string& customType) const;

    // Usage tracking
    bool TryAllocate(BudgetResourceType type, uint64_t bytes);
    bool TryAllocate(const std::string& customType, uint64_t bytes);

    void RecordAllocation(BudgetResourceType type, uint64_t bytes);
    void RecordAllocation(const std::string& customType, uint64_t bytes);

    void RecordDeallocation(BudgetResourceType type, uint64_t bytes);
    void RecordDeallocation(const std::string& customType, uint64_t bytes);

    // Query current state
    BudgetResourceUsage GetUsage(BudgetResourceType type) const;
    BudgetResourceUsage GetUsage(const std::string& customType) const;

    uint64_t GetAvailableBytes(BudgetResourceType type) const;
    uint64_t GetAvailableBytes(const std::string& customType) const;

    bool IsOverBudget(BudgetResourceType type) const;
    bool IsOverBudget(const std::string& customType) const;

    bool IsNearWarningThreshold(BudgetResourceType type) const;
    bool IsNearWarningThreshold(const std::string& customType) const;

    // System memory detection
    static uint64_t DetectHostMemoryBytes();
    static uint64_t DetectDeviceMemoryBytes(VkPhysicalDevice physicalDevice);

    // Utilities
    void Reset();
    void ResetUsage(BudgetResourceType type);
    void ResetUsage(const std::string& customType);

private:
    /// One budget + its usage counters, all atomics. `configured` is release-stored after the
    /// budget fields, so a reader that acquires it sees a complete budget. SetBudget is an
    /// owner-phase call (startup / budget retune); usage updates come from any thread.
    struct Slot {
        std::atomic<bool> configured{false};
        std::atomic<uint64_t> maxBytes{0};
        std::atomic<uint64_t> warningThreshold{0};
        std::atomic<bool> strict{false};
        AtomicResourceUsage usage;

        void Set(const ResourceBudget& budget) noexcept;
        std::optional<ResourceBudget> Get() const noexcept;
        void ResetAll() noexcept;
    };

    /// Named slot for user-defined types: an append-only lock-free list (push by CAS, never
    /// removed, freed with the manager), so lookups walk immutable nodes without a lock.
    struct CustomSlot : Slot {
        explicit CustomSlot(std::string slotName) : name(std::move(slotName)) {}
        const std::string name;
        CustomSlot* next = nullptr;  // set before the node is published, immutable afterwards
    };

    static constexpr size_t kStandardTypeCount = static_cast<size_t>(BudgetResourceType::UserDefined) + 1;

    std::array<Slot, kStandardTypeCount> slots_;
    std::atomic<CustomSlot*> customHead_{nullptr};

    Slot* FindSlot(BudgetResourceType type) noexcept;
    const Slot* FindSlot(BudgetResourceType type) const noexcept;
    CustomSlot* FindCustom(const std::string& name) const noexcept;
    CustomSlot* GetOrCreateCustom(const std::string& name);

    static bool TryAllocateImpl(const Slot& slot, uint64_t bytes);
    static uint64_t AvailableBytesImpl(const Slot* slot);
    static bool NearWarningImpl(const Slot* slot);
    static void RecordAllocationImpl(AtomicResourceUsage* usage, uint64_t bytes);
    static void RecordDeallocationImpl(AtomicResourceUsage* usage, uint64_t bytes);
};

} // namespace ResourceManagement
