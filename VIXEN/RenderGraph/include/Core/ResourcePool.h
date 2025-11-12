#pragma once

#include "ResourceBudgetManager.h"
#include "Data/Core/ResourceVariant.h"
#include "Data/Core/ResourceTypes.h"
#include <memory>
#include <cstdint>

namespace Vixen::RenderGraph {

// Forward declarations
class AliasingEngine;
class ResourceProfiler;
class ResourceLifetimeAnalyzer;

/**
 * @brief Central resource management pool with aliasing, budgets, and profiling
 *
 * ResourcePool unifies all resource allocation strategies:
 * - Stack allocation via RequestStackResource
 * - Heap allocation via RequestResource
 * - VRAM allocation with automatic aliasing
 * - Budget enforcement (soft/strict modes)
 * - Per-node profiling
 *
 * This class serves as the primary interface for all resource management
 * operations within the render graph. It coordinates between:
 * - ResourceBudgetManager: Enforces memory budgets and tracks usage
 * - AliasingEngine: Optimizes memory by reusing allocations (TODO)
 * - ResourceProfiler: Collects performance metrics (TODO)
 * - ResourceLifetimeAnalyzer: Determines optimal resource lifetimes
 *
 * Example Usage:
 * @code
 *   ResourcePool pool;
 *   pool.SetBudget(BudgetResourceType::VRAM, {1024*1024*1024, BudgetMode::Soft});
 *   pool.EnableAliasing(true);
 *
 *   auto* texture = pool.AllocateResource<Texture2D>(
 *       descriptor,
 *       ResourceLifetime::PerFrame,
 *       AllocStrategy::Automatic
 *   );
 * @endcode
 */
class ResourcePool {
public:
    /**
     * @brief Construct a new ResourcePool with default settings
     *
     * Initializes:
     * - ResourceBudgetManager with no budgets set
     * - Aliasing disabled by default
     * - Profiling disabled by default
     * - 1 MB aliasing threshold
     */
    ResourcePool();

    /**
     * @brief Destroy the ResourcePool and cleanup all resources
     */
    ~ResourcePool();

    // === Resource Allocation ===

    /**
     * @brief Allocate a new resource with specified lifetime and strategy
     *
     * This is the primary allocation method that integrates all subsystems:
     * 1. Checks budget constraints (soft/strict mode)
     * 2. Attempts aliasing if enabled and resource qualifies
     * 3. Creates new allocation if aliasing not possible
     * 4. Records allocation in profiler if enabled
     *
     * @tparam T Resource type (e.g., Texture2D, Buffer, RenderTarget)
     * @param descriptor Type-specific resource descriptor
     * @param lifetime Lifetime management strategy (PerFrame, Transient, etc.)
     * @param strategy Allocation strategy (Automatic, Stack, Heap, VRAM)
     * @return Resource* Allocated resource, or nullptr if budget exceeded in strict mode
     *
     * @note In soft budget mode, allocation always succeeds but logs warnings
     * @note In strict budget mode, allocation fails if budget would be exceeded
     */
    template<typename T>
    Resource* AllocateResource(
        const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
        ResourceLifetime lifetime,
        ::ResourceManagement::AllocStrategy strategy = ::ResourceManagement::AllocStrategy::Automatic
    );

    /**
     * @brief Release a resource back to the pool
     *
     * Depending on configuration:
     * - If aliasing enabled: Marks resource as available for reuse
     * - If aliasing disabled: Immediately deletes resource
     * - Updates budget tracking
     * - Records deallocation in profiler if enabled
     *
     * @param resource Resource to release (can be nullptr, which is a no-op)
     */
    void ReleaseResource(Resource* resource);

    // === Aliasing Control ===

    /**
     * @brief Enable or disable memory aliasing optimization
     *
     * When enabled, the pool will attempt to reuse memory allocations
     * from resources that are no longer alive. This can significantly
     * reduce peak memory usage in complex render graphs.
     *
     * @param enable True to enable aliasing, false to disable
     *
     * @note Requires ResourceLifetimeAnalyzer to be set for optimal results
     * @note Only resources above aliasingThreshold_ will be considered
     */
    void EnableAliasing(bool enable);

    /**
     * @brief Check if aliasing is currently enabled
     * @return true if aliasing is enabled, false otherwise
     */
    bool IsAliasingEnabled() const;

    /**
     * @brief Set minimum resource size for aliasing consideration
     *
     * Resources smaller than this threshold will not be aliased,
     * as the overhead of aliasing may exceed the memory savings.
     *
     * @param minBytes Minimum size in bytes (default: 1 MB)
     *
     * @note Typical values: 512 KB - 4 MB depending on use case
     */
    void SetAliasingThreshold(size_t minBytes);

    // === Budget Control ===

    /**
     * @brief Set memory budget for a specific resource type
     *
     * Configures how much memory can be allocated for a given resource category.
     * Supports both soft (warning) and strict (enforcement) modes.
     *
     * @param type Resource type to set budget for (RAM, VRAM, etc.)
     * @param budget Budget configuration with limit and mode
     *
     * @see ResourceBudgetManager::SetBudget for detailed behavior
     */
    void SetBudget(BudgetResourceType type, const ResourceBudget& budget);

    /**
     * @brief Get current budget for a resource type
     *
     * @param type Resource type to query
     * @return std::optional<ResourceBudget> Budget if set, nullopt otherwise
     */
    std::optional<ResourceBudget> GetBudget(BudgetResourceType type) const;

    /**
     * @brief Get current usage statistics for a resource type
     *
     * @param type Resource type to query
     * @return BudgetResourceUsage Current and peak usage statistics
     *
     * @note Usage is tracked even if no budget is set
     */
    BudgetResourceUsage GetUsage(BudgetResourceType type) const;

    // === Profiling ===

    /**
     * @brief Begin profiling for a new frame
     *
     * Starts tracking all resource operations for the given frame.
     * Should be called at the beginning of render graph execution.
     *
     * @param frameNumber Frame identifier for profiling data
     *
     * @note Only takes effect if profiling is enabled
     * @see EnableProfiling
     */
    void BeginFrameProfiling(uint64_t frameNumber);

    /**
     * @brief End profiling for the current frame
     *
     * Finalizes profiling data collection and makes statistics available.
     * Should be called at the end of render graph execution.
     *
     * @note Only takes effect if profiling is enabled
     */
    void EndFrameProfiling();

    /**
     * @brief Enable or disable resource profiling
     *
     * When enabled, the pool tracks detailed statistics about:
     * - Per-node resource allocations
     * - Memory usage over time
     * - Aliasing effectiveness
     * - Budget utilization
     *
     * @param enable True to enable profiling, false to disable
     *
     * @note Profiling has minimal overhead when disabled
     * @note Profiling data can be exported for analysis (TODO)
     */
    void EnableProfiling(bool enable);

    /**
     * @brief Check if profiling is currently enabled
     * @return true if profiling is enabled, false otherwise
     */
    bool IsProfilingEnabled() const;

    // === Stack Tracking (Integration) ===

    /**
     * @brief Begin frame-level stack tracking
     *
     * Initializes stack-based allocation tracking for the frame.
     * Used by FramebufferNode and other nodes that allocate frame-scoped resources.
     *
     * @param frameNumber Frame identifier
     *
     * @see ResourceBudgetManager::BeginFrameStackTracking
     */
    void BeginFrameStackTracking(uint64_t frameNumber);

    /**
     * @brief End frame-level stack tracking and cleanup
     *
     * Automatically releases all stack-allocated resources for the frame.
     *
     * @see ResourceBudgetManager::EndFrameStackTracking
     */
    void EndFrameStackTracking();

    // === Accessors ===

    /**
     * @brief Get mutable access to the budget manager
     * @return ResourceBudgetManager* Pointer to budget manager (never null)
     */
    ResourceBudgetManager* GetBudgetManager() { return budgetManager_.get(); }

    /**
     * @brief Get const access to the budget manager
     * @return const ResourceBudgetManager* Pointer to budget manager (never null)
     */
    const ResourceBudgetManager* GetBudgetManager() const { return budgetManager_.get(); }

    /**
     * @brief Get mutable access to the resource profiler
     * @return ResourceProfiler* Pointer to profiler (never null)
     */
    ResourceProfiler* GetProfiler() { return profiler_.get(); }

    /**
     * @brief Get const access to the resource profiler
     * @return const ResourceProfiler* Pointer to profiler (never null)
     */
    const ResourceProfiler* GetProfiler() const { return profiler_.get(); }

    /**
     * @brief Set the lifetime analyzer for aliasing optimization
     *
     * The lifetime analyzer provides information about when resources
     * are actively used, enabling safe memory aliasing.
     *
     * @param analyzer Non-owning pointer to analyzer (managed by RenderGraph)
     *
     * @note Required for aliasing to work effectively
     * @note ResourcePool does not take ownership
     */
    void SetLifetimeAnalyzer(ResourceLifetimeAnalyzer* analyzer);

private:
    // Core managers (owned by ResourcePool)
    std::unique_ptr<ResourceBudgetManager> budgetManager_;
    std::unique_ptr<AliasingEngine> aliasingEngine_;      // TODO: Implement AliasingEngine
    std::unique_ptr<ResourceProfiler> profiler_;          // TODO: Implement ResourceProfiler

    // External components (non-owning pointers)
    ResourceLifetimeAnalyzer* lifetimeAnalyzer_ = nullptr;

    // Configuration state
    bool aliasingEnabled_ = false;                        // Aliasing disabled by default
    size_t aliasingThreshold_ = 1 * 1024 * 1024;         // 1 MB minimum for aliasing
    bool profilingEnabled_ = false;                       // Profiling disabled by default

    // Current frame tracking
    uint64_t currentFrame_ = 0;                           // Current frame number for profiling
};

} // namespace Vixen::RenderGraph
