#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include "../../ResourceManagement/include/ResourceManagement/UnifiedRM_TypeSafe.h"
#include "Data/Core/ResourceVariant.h"
#include "Data/Core/ResourceTypeTraits.h"

namespace Vixen::RenderGraph {

// Forward declarations
class NodeInstance;
class ResourceLifetimeAnalyzer;

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

    // ========================================================================
    // PHASE H: UNIFIED RESOURCE REGISTRY
    // ========================================================================

    /**
     * @brief Resource metadata for tracking allocations
     *
     * Tracks metadata for each Resource* managed by URM.
     * Resources are identified by Resource* identity (not by slot!).
     */
    struct ResourceMetadata {
        Resource* resource;                                    // Back-pointer for validation
        ResourceManagement::AllocStrategy strategy;
        ResourceManagement::MemoryLocation location;
        size_t allocatedBytes;
        uint64_t allocationTimestamp;                          // For debugging/profiling
    };

    /**
     * @brief Create and track a new resource (slot-agnostic!)
     *
     * This is the central resource creation API. All non-trivial allocations
     * should go through this method for budget enforcement and lifetime tracking.
     *
     * The created Resource is owned by URM (stored in resource pool).
     * Nodes receive Resource* which they can populate and wire to slots.
     *
     * @param descriptor Resource descriptor (ImageDescriptor, BufferDescriptor, etc.)
     * @param strategy Allocation strategy (Stack/Heap/Device/Automatic)
     * @return Resource* owned by URM, ready to be populated
     *
     * Usage:
     * @code
     * BufferDescriptor desc{.size = 1024, ...};
     * Resource* buffer = budgetManager->CreateResource<VkBuffer>(desc, AllocStrategy::Device);
     * // ... populate buffer ...
     * // ... wire to slots via ctx.Out() ...
     * @endcode
     */
    template<typename T>
    Resource* CreateResource(
        const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
        ResourceManagement::AllocStrategy strategy = ResourceManagement::AllocStrategy::Automatic
    );

    /**
     * @brief Get metadata for a resource
     *
     * Used for budgeting, reporting, and lifetime analysis.
     *
     * @param resource Resource pointer
     * @return ResourceMetadata pointer, or nullptr if not tracked
     */
    const ResourceMetadata* GetResourceMetadata(Resource* resource) const;

    /**
     * @brief Update resource size after handle is set
     *
     * Called when the actual Vulkan/resource handle is created and size is known.
     *
     * @param resource Resource pointer
     * @param newSize Actual allocated size in bytes
     */
    void UpdateResourceSize(Resource* resource, size_t newSize);

    /**
     * @brief Get all tracked resources (for reporting)
     */
    size_t GetTrackedResourceCount() const { return resourceRegistry_.size(); }

    /**
     * @brief Update aliasing pools from topology analysis
     *
     * Creates memory aliasing pools based on ResourceLifetimeAnalyzer's
     * computed non-overlapping resource lifetimes.
     *
     * Called automatically from RenderGraph::Compile() after topology sort.
     *
     * @param analyzer Resource lifetime analyzer with computed timelines
     */
    void UpdateAliasingPoolsFromTopology(const ResourceLifetimeAnalyzer& analyzer);

    /**
     * @brief Print resource tracking report
     */
    void PrintResourceReport() const;

    /**
     * @brief Print aliasing efficiency report
     */
    void PrintAliasingReport() const;

private:
    // Standard resource type budgets
    std::unordered_map<BudgetResourceType, ResourceBudget> budgets_;
    std::unordered_map<BudgetResourceType, BudgetResourceUsage> usage_;

    // Custom/user-defined resource budgets
    std::unordered_map<std::string, ResourceBudget> customBudgets_;
    std::unordered_map<std::string, BudgetResourceUsage> customUsage_;

    // Phase H: Resource pool (URM owns all Resources)
    std::vector<std::unique_ptr<Resource>> resources_;

    // Phase H: Resource registry (track by Resource* identity, NOT by slot!)
    std::unordered_map<Resource*, ResourceMetadata> resourceRegistry_;

    // Phase H: Aliasing pools
    struct AliasingPool {
        std::string poolID;
        size_t totalSize;
        void* sharedMemory;
        std::vector<Resource*> aliasedResources;                // Track by Resource*!
        std::vector<std::pair<uint32_t, uint32_t>> lifetimes;  // (birth, death) indices
    };
    std::unordered_map<std::string, AliasingPool> aliasingPools_;

    // Internal helpers
    bool TryAllocateImpl(const ResourceBudget* budget, BudgetResourceUsage* usage, uint64_t bytes);
    void RecordAllocationImpl(BudgetResourceUsage* usage, uint64_t bytes);
    void RecordDeallocationImpl(BudgetResourceUsage* usage, uint64_t bytes);

    // Helper to determine memory location from type and strategy
    template<typename T>
    ResourceManagement::MemoryLocation DetermineMemoryLocation(
        ResourceManagement::AllocStrategy strategy
    ) const;

    // Helper to estimate size from descriptor
    template<typename T>
    size_t EstimateSize(const typename ResourceTypeTraits<T>::DescriptorT& descriptor) const;
};

// ============================================================================
// TEMPLATE IMPLEMENTATIONS
// ============================================================================

template<typename T>
Resource* ResourceBudgetManager::CreateResource(
    const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
    ResourceManagement::AllocStrategy strategy
) {
    // Create Resource object with descriptor
    auto resource = Resource::Create<T>(descriptor);

    // Store in pool (URM owns it)
    Resource* resPtr = resource.get();
    resources_.push_back(std::move(resource));

    // Determine memory location based on type and strategy
    auto location = DetermineMemoryLocation<T>(strategy);

    // Estimate size from descriptor
    size_t estimatedSize = EstimateSize<T>(descriptor);

    // Track metadata
    ResourceMetadata metadata{
        .resource = resPtr,
        .strategy = strategy,
        .location = location,
        .allocatedBytes = estimatedSize,
        .allocationTimestamp = 0  // TODO: Add timestamp when needed
    };
    resourceRegistry_[resPtr] = metadata;

    // Record budget allocation
    BudgetResourceType budgetType = (location == ResourceManagement::MemoryLocation::DeviceLocal)
        ? BudgetResourceType::DeviceMemory
        : BudgetResourceType::HostMemory;

    RecordAllocation(budgetType, estimatedSize);

    return resPtr;
}

template<typename T>
ResourceManagement::MemoryLocation ResourceBudgetManager::DetermineMemoryLocation(
    ResourceManagement::AllocStrategy strategy
) const {
    // Simple heuristic for now
    // TODO: Make this more sophisticated based on resource type
    if (strategy == ResourceManagement::AllocStrategy::Device) {
        return ResourceManagement::MemoryLocation::DeviceLocal;
    } else if (strategy == ResourceManagement::AllocStrategy::Stack ||
               strategy == ResourceManagement::AllocStrategy::Heap) {
        return ResourceManagement::MemoryLocation::HostVisible;
    }

    // Automatic: decide based on type
    // Vulkan handles typically go to device memory
    return ResourceManagement::MemoryLocation::DeviceLocal;
}

template<typename T>
size_t ResourceBudgetManager::EstimateSize(
    const typename ResourceTypeTraits<T>::DescriptorT& descriptor
) const {
    // Estimate size from descriptor
    // For images: width * height * bytesPerPixel
    // For buffers: size field
    // For handles: minimal overhead

    if constexpr (std::is_same_v<typename ResourceTypeTraits<T>::DescriptorT, ImageDescriptor>) {
        // Image: estimate based on dimensions and format
        uint32_t bytesPerPixel = 4; // Default RGBA8
        return descriptor.width * descriptor.height * bytesPerPixel;
    } else if constexpr (std::is_same_v<typename ResourceTypeTraits<T>::DescriptorT, BufferDescriptor>) {
        // Buffer: use size field
        return descriptor.size;
    } else {
        // Handle types: minimal tracking overhead
        return sizeof(T);
    }
}

} // namespace Vixen::RenderGraph
