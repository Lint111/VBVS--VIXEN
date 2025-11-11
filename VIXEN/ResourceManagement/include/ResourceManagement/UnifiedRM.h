#pragma once

#include "ResourceState.h"
#include "../../RenderGraph/include/Core/VulkanLimits.h"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <any>
#include <array>
#include <memory>
#include <stdexcept>

namespace Vixen {
namespace RenderGraph {
    class UnifiedBudgetManager;  // Forward declare
}
}

namespace ResourceManagement {

/**
 * @file UnifiedRM.h
 * @brief Unified Resource Management - Single system for all resource types
 *
 * Merges functionality from:
 * - RM.h (state management, metadata, generation)
 * - StackAllocatedRM.h (stack allocation tracking)
 * - Integration with ResourceBudgetManager (budget tracking)
 * - Integration with PerFrameResources (per-frame pattern)
 *
 * Key Features:
 * - Unified state management across all allocation strategies
 * - Automatic budget tracking (stack + heap + device)
 * - Flexible allocation strategies (stack/heap/device/automatic)
 * - Memory location tracking
 * - Generation-based cache invalidation
 * - Rich metadata system
 *
 * Design Goals:
 * 1. Single API for all resource types
 * 2. Automatic budget integration
 * 3. Zero overhead for unused features
 * 4. Type-safe at compile time
 * 5. Observable lifecycle
 */

/**
 * @brief Allocation strategy for UnifiedRM
 */
enum class AllocStrategy : uint8_t {
    Stack,      ///< Fixed-size stack allocation (std::array)
    Heap,       ///< Dynamic heap allocation (std::vector, std::unique_ptr)
    Device,     ///< GPU device memory (VkBuffer, VkImage, etc.)
    Automatic   ///< Let system decide based on size/type/lifetime
};

/**
 * @brief Memory location tracking
 */
enum class MemoryLocation : uint8_t {
    HostStack,      ///< CPU stack memory
    HostHeap,       ///< CPU heap memory (malloc/new)
    DeviceLocal,    ///< GPU VRAM (not CPU-accessible)
    HostVisible,    ///< GPU memory mapped to CPU
    Unknown         ///< Not yet determined
};

/**
 * @brief Helper: Bounded array for stack allocation
 *
 * Combines std::array storage with dynamic count tracking.
 * Replacement for std::vector in stack-allocated contexts.
 *
 * Usage:
 * @code
 * BoundedArray<VkImageView, 4> views;
 * views.Add(view1);
 * views.Add(view2);
 * vkCreateFramebuffer(..., views.Size(), views.Data());
 * @endcode
 */
template<typename T, size_t N>
struct BoundedArray {
    std::array<T, N> data;
    size_t count = 0;

    void Add(const T& value) {
        if (count >= N) {
            throw std::runtime_error("BoundedArray overflow");
        }
        data[count++] = value;
    }

    void Add(T&& value) {
        if (count >= N) {
            throw std::runtime_error("BoundedArray overflow");
        }
        data[count++] = std::move(value);
    }

    void Clear() { count = 0; }
    bool Empty() const { return count == 0; }
    bool Full() const { return count >= N; }
    size_t Size() const { return count; }
    constexpr size_t Capacity() const { return N; }

    T* Data() { return data.data(); }
    const T* Data() const { return data.data(); }

    T& operator[](size_t i) { return data[i]; }
    const T& operator[](size_t i) const { return data[i]; }

    T* begin() { return data.data(); }
    T* end() { return data.data() + count; }
    const T* begin() const { return data.data(); }
    const T* end() const { return data.data() + count; }
};

/**
 * @brief Type-erased base for UnifiedRM
 *
 * Allows UnifiedBudgetManager to track all UnifiedRM instances
 * without template parameters.
 */
class UnifiedRM_Base {
public:
    virtual ~UnifiedRM_Base() = default;

    virtual AllocStrategy GetAllocStrategy() const = 0;
    virtual MemoryLocation GetMemoryLocation() const = 0;
    virtual size_t GetAllocatedBytes() const = 0;
    virtual std::string_view GetDebugName() const = 0;
    virtual ResourceState GetState() const = 0;
};

/**
 * @brief Unified Resource Manager - Single wrapper for all resource types
 *
 * Replaces:
 * - RM<T>
 * - StackAllocatedRM<T, N>
 * - Manual budget tracking
 *
 * Features:
 * - State management (Ready, Outdated, Locked, etc.)
 * - Metadata storage
 * - Generation tracking (cache invalidation)
 * - Automatic budget registration
 * - Allocation strategy tracking
 * - Memory location tracking
 *
 * @tparam T Resource type (VkPipeline, VkBuffer, BoundedArray<VkImageView, N>, etc.)
 */
template<typename T>
class UnifiedRM : public UnifiedRM_Base {
public:
    /**
     * @brief Construct with allocation strategy
     *
     * @param strategy Stack/Heap/Device/Automatic
     * @param debugName Name for debugging and budget tracking
     */
    explicit UnifiedRM(
        AllocStrategy strategy = AllocStrategy::Automatic,
        std::string_view debugName = "unnamed"
    )
        : allocStrategy_(strategy)
        , debugName_(debugName)
        , state_(ResourceState::Uninitialized)
        , generation_(0)
        , allocatedBytes_(0)
        , memoryLocation_(MemoryLocation::Unknown)
        , budgetManager_(nullptr)
    {
        DetermineMemoryLocation();
    }

    ~UnifiedRM() {
        if (budgetManager_) {
            UnregisterFromBudget();
        }
    }

    // Non-copyable (resources are owned)
    UnifiedRM(const UnifiedRM&) = delete;
    UnifiedRM& operator=(const UnifiedRM&) = delete;

    // Movable
    UnifiedRM(UnifiedRM&& other) noexcept = default;
    UnifiedRM& operator=(UnifiedRM&& other) noexcept = default;

    // ========================================================================
    // VALUE ACCESS (from original RM.h)
    // ========================================================================

    bool Ready() const {
        return storage_.has_value() && HasState(state_, ResourceState::Ready);
    }

    T& Value() {
        if (!Ready()) {
            throw std::runtime_error("UnifiedRM::Value() called on unready resource");
        }
        return *storage_;
    }

    const T& Value() const {
        if (!Ready()) {
            throw std::runtime_error("UnifiedRM::Value() called on unready resource");
        }
        return *storage_;
    }

    T ValueOr(const T& defaultValue) const {
        return Ready() ? *storage_ : defaultValue;
    }

    T& operator*() { return *storage_; }
    const T& operator*() const { return *storage_; }

    T* operator->() { return &(*storage_); }
    const T* operator->() const { return &(*storage_); }

    explicit operator bool() const { return Ready(); }

    // ========================================================================
    // VALUE MUTATION
    // ========================================================================

    void Set(T value) {
        size_t oldSize = allocatedBytes_;
        storage_ = std::move(value);
        state_ = state_ | ResourceState::Ready;
        generation_++;

        // Update allocation size
        allocatedBytes_ = sizeof(T);  // Basic estimate
        UpdateBudget(oldSize, allocatedBytes_);
    }

    void Reset() {
        size_t oldSize = allocatedBytes_;
        storage_.reset();
        state_ = ResourceState::Uninitialized;
        metadata_.clear();
        allocatedBytes_ = 0;
        UpdateBudget(oldSize, 0);
    }

    // ========================================================================
    // STATE MANAGEMENT (from original RM.h)
    // ========================================================================

    bool Has(ResourceState checkState) const override {
        return HasState(state_, checkState);
    }

    ResourceState GetState() const override {
        return state_;
    }

    void SetState(ResourceState newState) {
        state_ = newState;
    }

    void AddState(ResourceState flags) {
        state_ = state_ | flags;
    }

    void RemoveState(ResourceState flags) {
        state_ = static_cast<ResourceState>(
            static_cast<uint32_t>(state_) & ~static_cast<uint32_t>(flags)
        );
    }

    void MarkOutdated() {
        AddState(ResourceState::Outdated);
        RemoveState(ResourceState::Ready);
    }

    void MarkReady() {
        AddState(ResourceState::Ready);
        RemoveState(ResourceState::Outdated | ResourceState::Pending | ResourceState::Failed);
    }

    void Lock() { AddState(ResourceState::Locked); }
    void Unlock() { RemoveState(ResourceState::Locked); }
    bool IsLocked() const { return Has(ResourceState::Locked); }

    // ========================================================================
    // GENERATION TRACKING (from original RM.h)
    // ========================================================================

    uint64_t GetGeneration() const { return generation_; }
    void IncrementGeneration() { generation_++; }

    // ========================================================================
    // METADATA (from original RM.h)
    // ========================================================================

    template<typename MetaType>
    void SetMetadata(const std::string& key, MetaType value) {
        metadata_[key] = std::move(value);
    }

    template<typename MetaType>
    MetaType GetMetadata(const std::string& key) const {
        auto it = metadata_.find(key);
        if (it == metadata_.end()) {
            throw std::runtime_error("Metadata key not found: " + key);
        }
        return std::any_cast<MetaType>(it->second);
    }

    template<typename MetaType>
    MetaType GetMetadataOr(const std::string& key, const MetaType& defaultValue) const {
        auto it = metadata_.find(key);
        if (it == metadata_.end()) {
            return defaultValue;
        }
        try {
            return std::any_cast<MetaType>(it->second);
        } catch (const std::bad_any_cast&) {
            return defaultValue;
        }
    }

    bool HasMetadata(const std::string& key) const {
        return metadata_.find(key) != metadata_.end();
    }

    void RemoveMetadata(const std::string& key) {
        metadata_.erase(key);
    }

    void ClearMetadata() {
        metadata_.clear();
    }

    // ========================================================================
    // ALLOCATION TRACKING (NEW)
    // ========================================================================

    AllocStrategy GetAllocStrategy() const override { return allocStrategy_; }
    MemoryLocation GetMemoryLocation() const override { return memoryLocation_; }
    size_t GetAllocatedBytes() const override { return allocatedBytes_; }
    std::string_view GetDebugName() const override { return debugName_; }

    // ========================================================================
    // BUDGET INTEGRATION (NEW)
    // ========================================================================

    void RegisterWithBudget(Vixen::RenderGraph::UnifiedBudgetManager* budgetMgr);
    void UnregisterFromBudget();

private:
    std::optional<T> storage_;
    ResourceState state_;
    uint64_t generation_;
    std::unordered_map<std::string, std::any> metadata_;

    // NEW: Allocation tracking
    AllocStrategy allocStrategy_;
    MemoryLocation memoryLocation_;
    size_t allocatedBytes_;
    std::string_view debugName_;
    Vixen::RenderGraph::UnifiedBudgetManager* budgetManager_;

    // Helpers
    void DetermineMemoryLocation();
    void UpdateBudget(size_t oldSize, size_t newSize);
};

// ============================================================================
// TYPE ALIASES FOR COMMON USAGE PATTERNS
// ============================================================================

/**
 * @brief Stack-allocated image view array
 */
template<size_t N = Vixen::RenderGraph::MAX_SWAPCHAIN_IMAGES>
using StackImageViewArray = UnifiedRM<BoundedArray<VkImageView, N>>;

/**
 * @brief Stack-allocated descriptor write array
 */
template<size_t N = Vixen::RenderGraph::MAX_DESCRIPTOR_BINDINGS>
using StackDescriptorWriteArray = UnifiedRM<BoundedArray<VkWriteDescriptorSet, N>>;

/**
 * @brief Heap-allocated pipeline (typical usage)
 */
using HeapPipeline = UnifiedRM<VkPipeline>;

/**
 * @brief Device-allocated buffer
 */
using DeviceBuffer = UnifiedRM<VkBuffer>;

} // namespace ResourceManagement
