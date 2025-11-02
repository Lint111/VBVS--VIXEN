#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace Vixen::RenderGraph {

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
 * @brief Phase F.1: Resource Budget Manager
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
 */
class ResourceBudgetManager {
public:
    ResourceBudgetManager() = default;
    ~ResourceBudgetManager() = default;

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
    // Standard resource type budgets
    std::unordered_map<BudgetResourceType, ResourceBudget> budgets_;
    std::unordered_map<BudgetResourceType, BudgetResourceUsage> usage_;

    // Custom/user-defined resource budgets
    std::unordered_map<std::string, ResourceBudget> customBudgets_;
    std::unordered_map<std::string, BudgetResourceUsage> customUsage_;

    // Internal helpers
    bool TryAllocateImpl(const ResourceBudget* budget, BudgetResourceUsage* usage, uint64_t bytes);
    void RecordAllocationImpl(BudgetResourceUsage* usage, uint64_t bytes);
    void RecordDeallocationImpl(BudgetResourceUsage* usage, uint64_t bytes);
};

} // namespace Vixen::RenderGraph
