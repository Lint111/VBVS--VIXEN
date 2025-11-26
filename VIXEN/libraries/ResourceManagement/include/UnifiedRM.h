#pragma once

#include "ResourceState.h"
#include "BoundedArray.h"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <any>
#include <array>
#include <memory>
#include <stdexcept>
#include <cstdint>

namespace ResourceManagement {

/**
 * @file UnifiedRM.h
 * @brief Unified Resource Management - Single system for all resource types
 *
 * Merges functionality from:
 * - RM.h (state management, metadata, generation)
 * - StackAllocatedRM.h (stack allocation tracking)
 * - Integration with ResourceBudgetManager (budget tracking)
 *
 * Key Features:
 * - Unified state management across all allocation strategies
 * - Flexible allocation strategies (stack/heap/device/automatic)
 * - Memory location tracking
 * - Generation-based cache invalidation
 * - Rich metadata system
 *
 * Design Goals:
 * 1. Single API for all resource types
 * 2. Automatic budget integration (when enabled)
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
 * @brief Convert AllocStrategy to string for debugging
 */
inline const char* AllocStrategyToString(AllocStrategy strategy) {
    switch (strategy) {
        case AllocStrategy::Stack: return "Stack";
        case AllocStrategy::Heap: return "Heap";
        case AllocStrategy::Device: return "Device";
        case AllocStrategy::Automatic: return "Automatic";
        default: return "Unknown";
    }
}

/**
 * @brief Convert MemoryLocation to string for debugging
 */
inline const char* MemoryLocationToString(MemoryLocation loc) {
    switch (loc) {
        case MemoryLocation::HostStack: return "HostStack";
        case MemoryLocation::HostHeap: return "HostHeap";
        case MemoryLocation::DeviceLocal: return "DeviceLocal";
        case MemoryLocation::HostVisible: return "HostVisible";
        case MemoryLocation::Unknown: return "Unknown";
        default: return "Invalid";
    }
}

/**
 * @brief Type-erased base for UnifiedRM
 *
 * Allows budget managers to track all UnifiedRM instances
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
    virtual bool Ready() const = 0;
};

/**
 * @brief Unified Resource Manager - Single wrapper for all resource types
 *
 * Replaces:
 * - RM<T> (with backward compatibility)
 * - StackAllocatedRM<T, N>
 * - Manual budget tracking
 *
 * Features:
 * - State management (Ready, Outdated, Locked, etc.)
 * - Metadata storage
 * - Generation tracking (cache invalidation)
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
    {
        DetermineMemoryLocation();
    }

    /**
     * @brief Construct with initial value
     */
    explicit UnifiedRM(
        T value,
        AllocStrategy strategy = AllocStrategy::Automatic,
        std::string_view debugName = "unnamed"
    )
        : storage_(std::move(value))
        , allocStrategy_(strategy)
        , debugName_(debugName)
        , state_(ResourceState::Ready)
        , generation_(1)
        , allocatedBytes_(sizeof(T))
        , memoryLocation_(MemoryLocation::Unknown)
    {
        DetermineMemoryLocation();
    }

    ~UnifiedRM() override = default;

    // Non-copyable (resources are owned)
    UnifiedRM(const UnifiedRM&) = delete;
    UnifiedRM& operator=(const UnifiedRM&) = delete;

    // Movable
    UnifiedRM(UnifiedRM&& other) noexcept
        : storage_(std::move(other.storage_))
        , allocStrategy_(other.allocStrategy_)
        , debugName_(other.debugName_)
        , state_(other.state_)
        , generation_(other.generation_)
        , allocatedBytes_(other.allocatedBytes_)
        , memoryLocation_(other.memoryLocation_)
        , metadata_(std::move(other.metadata_))
    {
        other.state_ = ResourceState::Uninitialized;
        other.allocatedBytes_ = 0;
    }

    UnifiedRM& operator=(UnifiedRM&& other) noexcept {
        if (this != &other) {
            storage_ = std::move(other.storage_);
            allocStrategy_ = other.allocStrategy_;
            debugName_ = other.debugName_;
            state_ = other.state_;
            generation_ = other.generation_;
            allocatedBytes_ = other.allocatedBytes_;
            memoryLocation_ = other.memoryLocation_;
            metadata_ = std::move(other.metadata_);

            other.state_ = ResourceState::Uninitialized;
            other.allocatedBytes_ = 0;
        }
        return *this;
    }

    // ========================================================================
    // VALUE ACCESS (from original RM.h)
    // ========================================================================

    bool Ready() const override {
        return storage_.has_value() && HasState(state_, ResourceState::Ready);
    }

    T& Value() {
        if (!Ready()) {
            throw std::runtime_error("UnifiedRM::Value() called on unready resource: " +
                                     std::string(debugName_));
        }
        return *storage_;
    }

    const T& Value() const {
        if (!Ready()) {
            throw std::runtime_error("UnifiedRM::Value() called on unready resource: " +
                                     std::string(debugName_));
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

    /**
     * @brief Check if storage has value (regardless of state)
     */
    bool HasValue() const { return storage_.has_value(); }

    // ========================================================================
    // VALUE MUTATION
    // ========================================================================

    void Set(T value) {
        storage_ = std::move(value);
        state_ = state_ | ResourceState::Ready;
        generation_++;
        allocatedBytes_ = sizeof(T);
    }

    /**
     * @brief Set value with explicit size (for variable-size resources)
     */
    void Set(T value, size_t sizeBytes) {
        storage_ = std::move(value);
        state_ = state_ | ResourceState::Ready;
        generation_++;
        allocatedBytes_ = sizeBytes;
    }

    void Reset() {
        storage_.reset();
        state_ = ResourceState::Uninitialized;
        metadata_.clear();
        allocatedBytes_ = 0;
    }

    /**
     * @brief Emplace value in-place
     */
    template<typename... Args>
    T& Emplace(Args&&... args) {
        storage_.emplace(std::forward<Args>(args)...);
        state_ = state_ | ResourceState::Ready;
        generation_++;
        allocatedBytes_ = sizeof(T);
        return *storage_;
    }

    // ========================================================================
    // STATE MANAGEMENT (from original RM.h)
    // ========================================================================

    bool Has(ResourceState checkState) const {
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

    void MarkPending() {
        AddState(ResourceState::Pending);
        RemoveState(ResourceState::Ready);
    }

    void MarkFailed() {
        AddState(ResourceState::Failed);
        RemoveState(ResourceState::Ready | ResourceState::Pending);
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
    // ALLOCATION TRACKING
    // ========================================================================

    AllocStrategy GetAllocStrategy() const override { return allocStrategy_; }
    MemoryLocation GetMemoryLocation() const override { return memoryLocation_; }
    size_t GetAllocatedBytes() const override { return allocatedBytes_; }
    std::string_view GetDebugName() const override { return debugName_; }

    void SetAllocStrategy(AllocStrategy strategy) {
        allocStrategy_ = strategy;
        DetermineMemoryLocation();
    }

    void SetDebugName(std::string_view name) {
        debugName_ = name;
    }

    void SetAllocatedBytes(size_t bytes) {
        allocatedBytes_ = bytes;
    }

private:
    std::optional<T> storage_;
    AllocStrategy allocStrategy_;
    std::string_view debugName_;
    ResourceState state_;
    uint64_t generation_;
    size_t allocatedBytes_;
    MemoryLocation memoryLocation_;
    std::unordered_map<std::string, std::any> metadata_;

    /**
     * @brief Determine memory location based on allocation strategy
     */
    void DetermineMemoryLocation() {
        switch (allocStrategy_) {
            case AllocStrategy::Stack:
                memoryLocation_ = MemoryLocation::HostStack;
                break;
            case AllocStrategy::Heap:
                memoryLocation_ = MemoryLocation::HostHeap;
                break;
            case AllocStrategy::Device:
                memoryLocation_ = MemoryLocation::DeviceLocal;
                break;
            case AllocStrategy::Automatic:
                // Default to heap for Automatic until we have more context
                memoryLocation_ = MemoryLocation::HostHeap;
                break;
        }
    }
};

// ============================================================================
// CONVENIENCE TYPE ALIASES
// ============================================================================

/**
 * @brief Stack-allocated bounded array resource
 */
template<typename T, size_t N>
using StackRM = UnifiedRM<BoundedArray<T, N>>;

/**
 * @brief Heap-allocated resource (explicit)
 */
template<typename T>
using HeapRM = UnifiedRM<T>;

} // namespace ResourceManagement
