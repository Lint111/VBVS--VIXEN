#pragma once

#include "StackResourceTracker.h"
#include "ResourceBudgetManager.h"
#include "BoundedArray.h"
#include <memory>
#include <string_view>
#include <cstdint>
#include <variant>
#include <vector>
#include <optional>

namespace Vixen::RenderGraph {

// ============================================================================
// ALLOCATION STRATEGY AND LIFETIME
// ============================================================================

/**
 * @brief Resource allocation strategy
 *
 * Determines how resources are allocated and which specialized
 * manager handles the allocation.
 */
enum class ResourceAllocStrategy : uint8_t {
    Stack,      ///< Fixed-size stack allocation (BoundedArray, std::array)
    Heap,       ///< Dynamic heap allocation (std::vector, std::unique_ptr)
    Device,     ///< GPU device memory (VkBuffer, VkImage via Vulkan)
    Automatic   ///< Let ResourceManagerBase decide based on type/size
};

/**
 * @brief Resource lifetime for tracking purposes
 */
enum class ResourceLifetime : uint8_t {
    FrameLocal,     ///< Released at end of frame
    GraphLocal,     ///< Released at graph destruction
    Persistent      ///< Manually managed lifetime
};

/**
 * @brief Memory location hint for device allocations
 */
enum class MemoryLocationHint : uint8_t {
    DontCare,       ///< Let the system decide
    HostLocal,      ///< Prefer CPU-accessible memory
    DeviceLocal,    ///< Prefer GPU VRAM (fastest for GPU access)
    HostVisible     ///< GPU memory that CPU can write to (staging)
};

// ============================================================================
// ALLOCATION ERROR TYPES
// ============================================================================

/**
 * @brief Error codes for allocation failures
 */
enum class AllocationError : uint8_t {
    None = 0,
    OutOfStackMemory,       ///< Stack budget exceeded
    OutOfHeapMemory,        ///< Heap allocation failed
    OutOfDeviceMemory,      ///< GPU memory exhausted
    BudgetExceeded,         ///< Soft budget limit reached (strict mode)
    InvalidConfig,          ///< Bad configuration parameters
    UnsupportedStrategy,    ///< Strategy not available for this type
    InternalError           ///< Unexpected internal failure
};

/**
 * @brief Convert AllocationError to string for debugging
 */
inline const char* AllocationErrorToString(AllocationError err) {
    switch (err) {
        case AllocationError::None: return "None";
        case AllocationError::OutOfStackMemory: return "OutOfStackMemory";
        case AllocationError::OutOfHeapMemory: return "OutOfHeapMemory";
        case AllocationError::OutOfDeviceMemory: return "OutOfDeviceMemory";
        case AllocationError::BudgetExceeded: return "BudgetExceeded";
        case AllocationError::InvalidConfig: return "InvalidConfig";
        case AllocationError::UnsupportedStrategy: return "UnsupportedStrategy";
        case AllocationError::InternalError: return "InternalError";
        default: return "Unknown";
    }
}

// ============================================================================
// ALLOCATION CONFIGURATION
// ============================================================================

/**
 * @brief Configuration for resource allocation requests
 *
 * Usage:
 * @code
 * AllocationConfig config;
 * config.strategy = ResourceAllocStrategy::Stack;
 * config.lifetime = ResourceLifetime::GraphLocal;
 * config.debugName = "framebuffers";
 * config.allowHeapFallback = true;  // Fall back to heap if stack fails
 * @endcode
 */
struct AllocationConfig {
    ResourceAllocStrategy strategy = ResourceAllocStrategy::Automatic;
    ResourceLifetime lifetime = ResourceLifetime::FrameLocal;
    MemoryLocationHint memoryHint = MemoryLocationHint::DontCare;

    std::string_view debugName = "unnamed";
    uint64_t ownerNodeId = 0;

    // Fallback behavior
    bool allowHeapFallback = true;      ///< If stack fails, try heap
    bool allowDeviceFallback = false;   ///< If heap fails, try device (rare)

    // Budget control
    bool strictBudget = false;          ///< Fail if budget exceeded (vs. warn)

    // Builder pattern for convenience
    AllocationConfig& WithStrategy(ResourceAllocStrategy s) { strategy = s; return *this; }
    AllocationConfig& WithLifetime(ResourceLifetime l) { lifetime = l; return *this; }
    AllocationConfig& WithMemoryHint(MemoryLocationHint h) { memoryHint = h; return *this; }
    AllocationConfig& WithName(std::string_view n) { debugName = n; return *this; }
    AllocationConfig& WithOwner(uint64_t id) { ownerNodeId = id; return *this; }
    AllocationConfig& WithHeapFallback(bool allow) { allowHeapFallback = allow; return *this; }
    AllocationConfig& WithStrictBudget(bool strict) { strictBudget = strict; return *this; }
};

// ============================================================================
// ALLOCATION RESULT TYPES
// ============================================================================

/**
 * @brief Result wrapper for stack-allocated BoundedArray
 */
template<typename T, size_t N>
struct StackAllocationResult {
    ResourceManagement::BoundedArray<T, N> data;
    uint64_t trackingHash = 0;

    // Convenience accessors
    T* Data() { return data.Data(); }
    const T* Data() const { return data.Data(); }
    size_t Size() const { return data.Size(); }
    size_t Capacity() const { return N; }
    void Add(const T& value) { data.Add(value); }
    void Clear() { data.Clear(); }
    T& operator[](size_t i) { return data[i]; }
    const T& operator[](size_t i) const { return data[i]; }

    // Iterator support
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }
};

/**
 * @brief Result wrapper for heap-allocated vector (fallback)
 */
template<typename T>
struct HeapAllocationResult {
    std::vector<T> data;
    uint64_t trackingHash = 0;

    // Convenience accessors matching BoundedArray API
    T* Data() { return data.data(); }
    const T* Data() const { return data.data(); }
    size_t Size() const { return data.size(); }
    void Add(const T& value) { data.push_back(value); }
    void Clear() { data.clear(); }
    void Reserve(size_t n) { data.reserve(n); }
    T& operator[](size_t i) { return data[i]; }
    const T& operator[](size_t i) const { return data[i]; }

    // Iterator support
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }
};

/**
 * @brief Unified allocation result using std::variant
 *
 * Contains either:
 * - AllocationError (on failure)
 * - StackAllocationResult<T, N> (stack success)
 * - HeapAllocationResult<T> (heap fallback success)
 *
 * Usage:
 * @code
 * auto result = rm->RequestAllocation<VkFramebuffer, MAX_SWAPCHAIN_IMAGES>(config);
 *
 * if (result.IsError()) {
 *     LOG_ERROR("Allocation failed: " << result.GetErrorString());
 *     return;
 * }
 *
 * if (result.IsStack()) {
 *     auto& stack = result.GetStack();
 *     // Use stack.data directly
 * } else {
 *     auto& heap = result.GetHeap();
 *     // Use heap.data (vector)
 * }
 *
 * // Or use the unified visitor pattern:
 * result.Visit([](auto& storage) {
 *     for (auto& item : storage) {
 *         // Process items regardless of storage type
 *     }
 * });
 * @endcode
 */
template<typename T, size_t N>
class AllocationResult {
public:
    using StackType = StackAllocationResult<T, N>;
    using HeapType = HeapAllocationResult<T>;
    using VariantType = std::variant<AllocationError, StackType, HeapType>;

    // Constructors
    AllocationResult() : data_(AllocationError::None) {}
    explicit AllocationResult(AllocationError error) : data_(error) {}
    explicit AllocationResult(StackType&& stack) : data_(std::move(stack)) {}
    explicit AllocationResult(HeapType&& heap) : data_(std::move(heap)) {}

    // Type queries
    bool IsError() const { return std::holds_alternative<AllocationError>(data_); }
    bool IsStack() const { return std::holds_alternative<StackType>(data_); }
    bool IsHeap() const { return std::holds_alternative<HeapType>(data_); }
    bool IsSuccess() const { return !IsError(); }

    // Accessors (throws if wrong type)
    AllocationError GetError() const { return std::get<AllocationError>(data_); }
    StackType& GetStack() { return std::get<StackType>(data_); }
    const StackType& GetStack() const { return std::get<StackType>(data_); }
    HeapType& GetHeap() { return std::get<HeapType>(data_); }
    const HeapType& GetHeap() const { return std::get<HeapType>(data_); }

    // Safe accessors (returns nullptr/nullopt if wrong type)
    std::optional<AllocationError> TryGetError() const {
        if (IsError()) return GetError();
        return std::nullopt;
    }
    StackType* TryGetStack() {
        if (IsStack()) return &std::get<StackType>(data_);
        return nullptr;
    }
    HeapType* TryGetHeap() {
        if (IsHeap()) return &std::get<HeapType>(data_);
        return nullptr;
    }

    // Error string helper
    const char* GetErrorString() const {
        if (IsError()) return AllocationErrorToString(GetError());
        return "Success";
    }

    // Visitor pattern for unified access
    template<typename Visitor>
    auto Visit(Visitor&& visitor) {
        return std::visit([&](auto& val) -> decltype(auto) {
            using ValType = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<ValType, AllocationError>) {
                // Skip error case in visitor (or handle separately)
                return visitor(val);
            } else {
                return visitor(val);
            }
        }, data_);
    }

    // ========================================================================
    // UNIFIED CONVENIENCE API - Works regardless of stack/heap storage
    // ========================================================================

    // Get raw pointer to data
    T* Data() {
        if (IsStack()) return GetStack().Data();
        if (IsHeap()) return GetHeap().Data();
        return nullptr;
    }

    const T* Data() const {
        if (IsStack()) return GetStack().Data();
        if (IsHeap()) return GetHeap().Data();
        return nullptr;
    }

    // Get current size
    size_t Size() const {
        if (IsStack()) return GetStack().Size();
        if (IsHeap()) return GetHeap().Size();
        return 0;
    }

    // Check if empty
    bool Empty() const {
        return Size() == 0;
    }

    // Add an element (works for both stack and heap)
    void Add(const T& value) {
        if (IsStack()) {
            GetStack().Add(value);
        } else if (IsHeap()) {
            GetHeap().Add(value);
        }
        // If error state, silently ignore (or could throw)
    }

    // Clear all elements
    void Clear() {
        if (IsStack()) {
            GetStack().Clear();
        } else if (IsHeap()) {
            GetHeap().Clear();
        }
    }

    // Index operator
    T& operator[](size_t i) {
        if (IsStack()) return GetStack()[i];
        return GetHeap()[i];  // Will throw if error state (undefined behavior)
    }

    const T& operator[](size_t i) const {
        if (IsStack()) return GetStack()[i];
        return GetHeap()[i];
    }

    // ========================================================================
    // ITERATOR SUPPORT - Enables range-for loops
    // ========================================================================

    // Custom iterator that works with both storage types
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        Iterator(T* ptr) : ptr_(ptr) {}

        reference operator*() const { return *ptr_; }
        pointer operator->() { return ptr_; }
        Iterator& operator++() { ++ptr_; return *this; }
        Iterator operator++(int) { Iterator tmp = *this; ++ptr_; return tmp; }
        friend bool operator==(const Iterator& a, const Iterator& b) { return a.ptr_ == b.ptr_; }
        friend bool operator!=(const Iterator& a, const Iterator& b) { return a.ptr_ != b.ptr_; }

    private:
        T* ptr_;
    };

    class ConstIterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        ConstIterator(const T* ptr) : ptr_(ptr) {}

        reference operator*() const { return *ptr_; }
        pointer operator->() const { return ptr_; }
        ConstIterator& operator++() { ++ptr_; return *this; }
        ConstIterator operator++(int) { ConstIterator tmp = *this; ++ptr_; return tmp; }
        friend bool operator==(const ConstIterator& a, const ConstIterator& b) { return a.ptr_ == b.ptr_; }
        friend bool operator!=(const ConstIterator& a, const ConstIterator& b) { return a.ptr_ != b.ptr_; }

    private:
        const T* ptr_;
    };

    Iterator begin() { return Iterator(Data()); }
    Iterator end() { return Iterator(Data() + Size()); }
    ConstIterator begin() const { return ConstIterator(Data()); }
    ConstIterator end() const { return ConstIterator(Data() + Size()); }
    ConstIterator cbegin() const { return ConstIterator(Data()); }
    ConstIterator cend() const { return ConstIterator(Data() + Size()); }

private:
    VariantType data_;
};

/**
 * @brief Unified Resource Manager Base
 *
 * Acts as a facade/dispatcher to specialized resource managers:
 * - StackResourceTracker: Stack allocations, BoundedArray tracking
 * - ResourceBudgetManager: Budget tracking, memory limits
 * - (Future) VulkanResourceManager: Vulkan handle lifecycle
 * - (Future) ResourceProfiler: Profiling and statistics
 *
 * Design Philosophy:
 * - Single entry point for all resource operations
 * - Automatic routing to appropriate specialized manager
 * - Type-safe allocation with compile-time validation
 * - Zero overhead for unused features (pay-for-what-you-use)
 *
 * Usage:
 * @code
 * // From a node:
 * auto* rm = GetResourceManager();
 *
 * // Track a BoundedArray before output
 * rm->TrackBoundedArray(myArray, "framebuffers", GetInstanceId(), ResourceLifetime::GraphLocal);
 *
 * // Check stack usage
 * if (rm->IsStackOverWarningThreshold()) {
 *     // Log warning
 * }
 * @endcode
 */
class ResourceManagerBase {
public:
    ResourceManagerBase();
    ~ResourceManagerBase();

    // Non-copyable, movable
    ResourceManagerBase(const ResourceManagerBase&) = delete;
    ResourceManagerBase& operator=(const ResourceManagerBase&) = delete;
    ResourceManagerBase(ResourceManagerBase&&) = default;
    ResourceManagerBase& operator=(ResourceManagerBase&&) = default;

    // ========================================================================
    // FRAME LIFECYCLE
    // ========================================================================

    /**
     * @brief Begin tracking for a new frame
     * @param frameNumber Current frame number
     */
    void BeginFrame(uint64_t frameNumber);

    /**
     * @brief End frame tracking, cleanup temporary resources
     */
    void EndFrame();

    // ========================================================================
    // STACK RESOURCE TRACKING (BoundedArray, std::array)
    // ========================================================================

    /**
     * @brief Track a BoundedArray for profiling and budget tracking
     *
     * Call this before outputting a BoundedArray to a slot to register
     * it with the resource tracking system.
     *
     * @tparam T Element type
     * @tparam N Maximum capacity
     * @param array The BoundedArray to track
     * @param name Debug name for this resource
     * @param nodeId Node instance ID that owns this resource
     * @param lifetime Resource lifetime for cleanup tracking
     */
    template<typename T, size_t N>
    void TrackBoundedArray(
        const ResourceManagement::BoundedArray<T, N>& array,
        std::string_view name,
        uint64_t nodeId,
        ResourceLifetime lifetime = ResourceLifetime::FrameLocal
    ) {
        if (!stackTracker_) return;

        // Compute hash for this resource
        uint64_t resourceHash = ComputeResourceHash(nodeId, name);
        uint64_t scopeHash = ComputeScopeHash(nodeId, currentFrameNumber_);

        // Track the allocation
        stackTracker_->TrackAllocation(
            resourceHash,
            scopeHash,
            array.Data(),
            array.Size() * sizeof(T),  // Track actual used bytes, not capacity
            static_cast<uint32_t>(nodeId),
            lifetime == ResourceLifetime::FrameLocal
        );

        // Update budget tracking
        if (budgetManager_) {
            budgetManager_->RecordAllocation(
                BudgetResourceType::HostMemory,
                array.Size() * sizeof(T)
            );
        }
    }

    /**
     * @brief Track a std::array for profiling and budget tracking
     */
    template<typename T, size_t N>
    void TrackStdArray(
        const std::array<T, N>& array,
        size_t usedCount,
        std::string_view name,
        uint64_t nodeId,
        ResourceLifetime lifetime = ResourceLifetime::FrameLocal
    ) {
        if (!stackTracker_) return;

        uint64_t resourceHash = ComputeResourceHash(nodeId, name);
        uint64_t scopeHash = ComputeScopeHash(nodeId, currentFrameNumber_);

        stackTracker_->TrackAllocation(
            resourceHash,
            scopeHash,
            array.data(),
            usedCount * sizeof(T),
            static_cast<uint32_t>(nodeId),
            lifetime == ResourceLifetime::FrameLocal
        );

        if (budgetManager_) {
            budgetManager_->RecordAllocation(
                BudgetResourceType::HostMemory,
                usedCount * sizeof(T)
            );
        }
    }

    // ========================================================================
    // BUDGET MANAGEMENT
    // ========================================================================

    /**
     * @brief Set budget for a resource type
     */
    void SetBudget(BudgetResourceType type, const ResourceBudget& budget);

    /**
     * @brief Get current budget usage
     */
    BudgetResourceUsage GetBudgetUsage(BudgetResourceType type) const;

    /**
     * @brief Check if budget is exceeded
     */
    bool IsBudgetExceeded(BudgetResourceType type) const;

    // ========================================================================
    // STACK USAGE QUERIES
    // ========================================================================

    /**
     * @brief Check if stack usage exceeds warning threshold
     */
    bool IsStackOverWarningThreshold() const;

    /**
     * @brief Check if stack usage exceeds critical threshold
     */
    bool IsStackOverCriticalThreshold() const;

    /**
     * @brief Get current frame stack usage statistics
     */
    const StackResourceTracker::FrameStackUsage& GetCurrentFrameStackUsage() const;

    /**
     * @brief Get stack usage statistics over recent frames
     */
    StackResourceTracker::UsageStats GetStackUsageStats() const;

    // ========================================================================
    // DIRECT MANAGER ACCESS (for advanced use cases)
    // ========================================================================

    /**
     * @brief Get the stack resource tracker
     * @return Pointer to StackResourceTracker, or nullptr if not available
     */
    StackResourceTracker* GetStackTracker() { return stackTracker_.get(); }
    const StackResourceTracker* GetStackTracker() const { return stackTracker_.get(); }

    /**
     * @brief Get the budget manager
     * @return Pointer to ResourceBudgetManager, or nullptr if not available
     */
    ResourceBudgetManager* GetBudgetManager() { return budgetManager_.get(); }
    const ResourceBudgetManager* GetBudgetManager() const { return budgetManager_.get(); }

    // ========================================================================
    // ALLOCATION API - Main entry point for resource allocation
    // ========================================================================

    /**
     * @brief Request allocation of a fixed-size array resource
     *
     * This is the main API for allocating resources. It:
     * 1. Checks budget constraints
     * 2. Attempts stack allocation (if strategy allows)
     * 3. Falls back to heap if stack fails and fallback is enabled
     * 4. Automatically tracks the allocation for profiling
     *
     * Nodes don't need to manually call tracking - it's handled internally.
     *
     * @tparam T Element type (e.g., VkFramebuffer, VkSemaphore)
     * @tparam N Maximum capacity for stack allocation
     * @param config Allocation configuration
     * @return AllocationResult containing either error, stack result, or heap result
     *
     * Usage:
     * @code
     * auto result = rm->RequestAllocation<VkFramebuffer, MAX_SWAPCHAIN_IMAGES>(
     *     AllocationConfig()
     *         .WithStrategy(ResourceAllocStrategy::Stack)
     *         .WithLifetime(ResourceLifetime::GraphLocal)
     *         .WithName("framebuffers")
     *         .WithOwner(GetInstanceId())
     * );
     *
     * if (result.IsError()) {
     *     throw std::runtime_error(result.GetErrorString());
     * }
     *
     * // Add items (works regardless of stack/heap)
     * for (size_t i = 0; i < count; i++) {
     *     VkFramebuffer fb = CreateFramebuffer(...);
     *     if (result.IsStack()) {
     *         result.GetStack().Add(fb);
     *     } else {
     *         result.GetHeap().Add(fb);
     *     }
     * }
     * @endcode
     */
    template<typename T, size_t N>
    AllocationResult<T, N> RequestAllocation(const AllocationConfig& config) {
        // Calculate required bytes for budget check
        const size_t requiredBytes = N * sizeof(T);

        // Check budget constraints
        if (config.strictBudget && budgetManager_) {
            if (!budgetManager_->TryAllocate(BudgetResourceType::HostMemory, requiredBytes)) {
                return AllocationResult<T, N>(AllocationError::BudgetExceeded);
            }
        }

        // Determine actual strategy
        ResourceAllocStrategy actualStrategy = config.strategy;
        if (actualStrategy == ResourceAllocStrategy::Automatic) {
            // Automatic: use stack if capacity fits and stack budget available
            actualStrategy = CanAllocateOnStack(requiredBytes)
                ? ResourceAllocStrategy::Stack
                : ResourceAllocStrategy::Heap;
        }

        // Attempt stack allocation
        if (actualStrategy == ResourceAllocStrategy::Stack) {
            if (CanAllocateOnStack(requiredBytes)) {
                // Create stack result
                StackAllocationResult<T, N> stackResult;
                stackResult.trackingHash = ComputeResourceHash(config.ownerNodeId, config.debugName);

                // Track the allocation internally
                TrackAllocationInternal(
                    stackResult.data.Data(),
                    requiredBytes,
                    config.ownerNodeId,
                    config.debugName,
                    config.lifetime,
                    true  // isStack
                );

                return AllocationResult<T, N>(std::move(stackResult));
            }

            // Stack failed - check if heap fallback is allowed
            if (!config.allowHeapFallback) {
                return AllocationResult<T, N>(AllocationError::OutOfStackMemory);
            }
            // Fall through to heap allocation
        }

        // Heap allocation (either requested directly or as fallback)
        if (actualStrategy == ResourceAllocStrategy::Heap || config.allowHeapFallback) {
            try {
                HeapAllocationResult<T> heapResult;
                heapResult.data.reserve(N);  // Reserve capacity matching stack
                heapResult.trackingHash = ComputeResourceHash(config.ownerNodeId, config.debugName);

                // Track the allocation internally
                TrackAllocationInternal(
                    nullptr,  // Heap address not known until items added
                    requiredBytes,
                    config.ownerNodeId,
                    config.debugName,
                    config.lifetime,
                    false  // isStack
                );

                return AllocationResult<T, N>(std::move(heapResult));
            } catch (const std::bad_alloc&) {
                return AllocationResult<T, N>(AllocationError::OutOfHeapMemory);
            }
        }

        return AllocationResult<T, N>(AllocationError::UnsupportedStrategy);
    }

    /**
     * @brief Check if stack can accommodate the requested allocation
     */
    bool CanAllocateOnStack(size_t bytes) const;

private:
    /**
     * @brief Internal tracking without requiring the caller to know about it
     */
    void TrackAllocationInternal(
        const void* address,
        size_t bytes,
        uint64_t nodeId,
        std::string_view name,
        ResourceLifetime lifetime,
        bool isStack
    );

private:
    // Specialized managers
    std::unique_ptr<StackResourceTracker> stackTracker_;
    std::unique_ptr<ResourceBudgetManager> budgetManager_;
    // Future: std::unique_ptr<VulkanResourceManager> vulkanManager_;
    // Future: std::unique_ptr<ResourceProfiler> profiler_;

    uint64_t currentFrameNumber_ = 0;

    // Hash computation helpers
    static uint64_t ComputeResourceHash(uint64_t nodeId, std::string_view name);
    static uint64_t ComputeScopeHash(uint64_t nodeId, uint64_t frameNumber);
};

} // namespace Vixen::RenderGraph
