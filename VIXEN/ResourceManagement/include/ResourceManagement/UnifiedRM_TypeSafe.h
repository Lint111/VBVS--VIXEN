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
#include <type_traits>

namespace Vixen {
namespace RenderGraph {
    class UnifiedBudgetManager;  // Forward declare
}
}

namespace ResourceManagement {

/**
 * @file UnifiedRM.h
 * @brief Unified Resource Management - Type-safe resource tracking
 *
 * Key Innovation: Uses compile-time type information and member pointers
 * instead of runtime strings for identification.
 *
 * Benefits over string-based approach:
 * - ✅ Compile-time type safety
 * - ✅ No typos possible
 * - ✅ Refactoring-friendly (rename works)
 * - ✅ Zero runtime overhead for identification
 * - ✅ Automatic uniqueness guarantees
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
 * @brief Compile-time resource identifier using member pointers
 *
 * Instead of runtime strings, we use the address of the member variable
 * itself as a unique identifier. This is:
 * - Compile-time constant
 * - Guaranteed unique per member
 * - Zero runtime overhead
 * - Refactoring-safe
 *
 * Usage:
 * @code
 * class MyNode {
 *     UnifiedRM<VkPipeline> pipeline_{this, &MyNode::pipeline_};
 *     UnifiedRM<VkBuffer> buffer_{this, &MyNode::buffer_};
 * };
 * @endcode
 *
 * The compiler automatically provides unique identity via member pointer.
 */
template<typename Owner, typename T>
struct ResourceIdentity {
    using MemberPtr = T Owner::*;

    const Owner* owner;       ///< Pointer to owning object
    MemberPtr memberPtr;      ///< Pointer-to-member (unique per member)

    ResourceIdentity(const Owner* o, MemberPtr mp)
        : owner(o), memberPtr(mp) {}

    /**
     * @brief Get unique identifier as uintptr_t
     *
     * Combines owner address and member offset for guaranteed uniqueness.
     */
    uintptr_t GetUniqueID() const {
        // Member pointer offset is compile-time constant
        // Combined with owner address gives globally unique ID
        return reinterpret_cast<uintptr_t>(owner) ^
               reinterpret_cast<uintptr_t>(&(owner->*memberPtr));
    }

    /**
     * @brief Get debug name using type information
     *
     * Uses C++ RTTI to get human-readable name.
     * Only enabled in debug builds.
     */
    std::string GetDebugName() const {
        #ifndef NDEBUG
        // Use typeid for human-readable type names
        std::string ownerType = typeid(Owner).name();
        std::string memberType = typeid(T).name();

        // Member offset within owner (for disambiguation)
        auto offset = reinterpret_cast<const char*>(&(owner->*memberPtr)) -
                      reinterpret_cast<const char*>(owner);

        return ownerType + "::" + memberType + "@" + std::to_string(offset);
        #else
        return "";  // No overhead in release
        #endif
    }

    bool operator==(const ResourceIdentity& other) const {
        return owner == other.owner && memberPtr == other.memberPtr;
    }
};

/**
 * @brief Hash specialization for ResourceIdentity
 */
template<typename Owner, typename T>
struct ResourceIdentityHash {
    size_t operator()(const ResourceIdentity<Owner, T>& id) const {
        return static_cast<size_t>(id.GetUniqueID());
    }
};

/**
 * @brief Helper: Bounded array for stack allocation
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
 */
class UnifiedRM_Base {
public:
    virtual ~UnifiedRM_Base() = default;

    virtual AllocStrategy GetAllocStrategy() const = 0;
    virtual MemoryLocation GetMemoryLocation() const = 0;
    virtual size_t GetAllocatedBytes() const = 0;
    virtual uintptr_t GetUniqueID() const = 0;  // Changed from string to unique ID
    virtual std::string GetDebugName() const = 0;  // Only for debug output
    virtual ResourceState GetState() const = 0;
};

/**
 * @brief Unified Resource Manager - Type-safe resource wrapper
 *
 * Uses member pointers for compile-time type-safe identification.
 *
 * Usage in a node:
 * @code
 * class MyNode : public TypedNode<MyConfig> {
 *     // Self-identifying resources using member pointers
 *     UnifiedRM<VkPipeline> pipeline_{this, &MyNode::pipeline_};
 *     UnifiedRM<VkBuffer> vertexBuffer_{this, &MyNode::vertexBuffer_};
 *     UnifiedRM<BoundedArray<VkImageView, 4>> views_{this, &MyNode::views_};
 *
 *     void SetupImpl(TypedSetupContext& ctx) override {
 *         // Resources automatically tracked via member pointer
 *         pipeline_.RegisterWithBudget(ctx.budgetManager);
 *
 *         // No string names needed - type-safe!
 *         pipeline_.Set(myPipeline);
 *     }
 * };
 * @endcode
 *
 * @tparam Owner Type of the owning class (for member pointer)
 * @tparam T Resource type
 */
template<typename Owner, typename T>
class UnifiedRM : public UnifiedRM_Base {
public:
    using Identity = ResourceIdentity<Owner, T>;
    using MemberPtr = typename Identity::MemberPtr;

    /**
     * @brief Construct with member pointer (type-safe identification)
     *
     * @param owner Pointer to owning object (usually 'this')
     * @param memberPtr Pointer-to-member (&OwnerClass::memberName)
     * @param strategy Allocation strategy hint
     */
    explicit UnifiedRM(
        Owner* owner,
        MemberPtr memberPtr,
        AllocStrategy strategy = AllocStrategy::Automatic
    )
        : identity_(owner, memberPtr)
        , allocStrategy_(strategy)
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
            throw std::runtime_error("UnifiedRM::Value() called on unready resource: " +
                                   identity_.GetDebugName());
        }
        return *storage_;
    }

    const T& Value() const {
        if (!Ready()) {
            throw std::runtime_error("UnifiedRM::Value() called on unready resource: " +
                                   identity_.GetDebugName());
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

        allocatedBytes_ = CalculateSize();
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
    uintptr_t GetUniqueID() const override { return identity_.GetUniqueID(); }
    std::string GetDebugName() const override { return identity_.GetDebugName(); }

    /**
     * @brief Get compile-time identity
     */
    const Identity& GetIdentity() const { return identity_; }

    // ========================================================================
    // BUDGET INTEGRATION (NEW)
    // ========================================================================

    void RegisterWithBudget(Vixen::RenderGraph::UnifiedBudgetManager* budgetMgr);
    void UnregisterFromBudget();

private:
    Identity identity_;  // Compile-time type-safe identification
    std::optional<T> storage_;
    ResourceState state_;
    uint64_t generation_;
    std::unordered_map<std::string, std::any> metadata_;

    // Allocation tracking
    AllocStrategy allocStrategy_;
    MemoryLocation memoryLocation_;
    size_t allocatedBytes_;
    Vixen::RenderGraph::UnifiedBudgetManager* budgetManager_;

    // Helpers
    void DetermineMemoryLocation() {
        // Infer memory location from type and strategy
        if constexpr (std::is_same_v<T, VkBuffer> || std::is_same_v<T, VkImage>) {
            memoryLocation_ = MemoryLocation::DeviceLocal;
        } else if (allocStrategy_ == AllocStrategy::Stack) {
            memoryLocation_ = MemoryLocation::HostStack;
        } else if (allocStrategy_ == AllocStrategy::Heap) {
            memoryLocation_ = MemoryLocation::HostHeap;
        }
    }

    size_t CalculateSize() const {
        // Calculate actual size based on type
        if constexpr (std::is_trivial_v<T>) {
            return sizeof(T);
        } else {
            // For complex types, would need custom size calculation
            return sizeof(T);  // Conservative estimate
        }
    }

    void UpdateBudget(size_t oldSize, size_t newSize);
};

// ============================================================================
// SIMPLIFIED USAGE FOR ANONYMOUS RESOURCES
// ============================================================================

/**
 * @brief UnifiedRM variant for resources without explicit owner
 *
 * For temporary/local resources that don't belong to a specific class member.
 * Uses static counter for unique IDs instead of member pointers.
 *
 * Usage:
 * @code
 * void MyFunction() {
 *     LocalRM<VkImageView> view;  // Auto-generated unique ID
 *     view.Set(myView);
 * }
 * @endcode
 */
template<typename T>
class LocalRM : public UnifiedRM_Base {
public:
    explicit LocalRM(AllocStrategy strategy = AllocStrategy::Automatic)
        : allocStrategy_(strategy)
        , state_(ResourceState::Uninitialized)
        , generation_(0)
        , allocatedBytes_(0)
        , memoryLocation_(MemoryLocation::Unknown)
        , budgetManager_(nullptr)
        , uniqueID_(GenerateUniqueID())
    {
        DetermineMemoryLocation();
    }

    // Same interface as UnifiedRM<Owner, T>...
    // (Implementation similar to UnifiedRM but uses uniqueID_ instead of identity_)

    bool Ready() const {
        return storage_.has_value() && HasState(state_, ResourceState::Ready);
    }

    T& Value() {
        if (!Ready()) {
            throw std::runtime_error("LocalRM::Value() called on unready resource");
        }
        return *storage_;
    }

    const T& Value() const {
        if (!Ready()) {
            throw std::runtime_error("LocalRM::Value() called on unready resource");
        }
        return *storage_;
    }

    void Set(T value) {
        size_t oldSize = allocatedBytes_;
        storage_ = std::move(value);
        state_ = state_ | ResourceState::Ready;
        generation_++;
        allocatedBytes_ = sizeof(T);
        UpdateBudget(oldSize, allocatedBytes_);
    }

    void Reset() {
        size_t oldSize = allocatedBytes_;
        storage_.reset();
        state_ = ResourceState::Uninitialized;
        allocatedBytes_ = 0;
        UpdateBudget(oldSize, 0);
    }

    // State management
    bool Has(ResourceState checkState) const {
        return HasState(state_, checkState);
    }

    ResourceState GetState() const override { return state_; }
    void MarkReady() {
        state_ = state_ | ResourceState::Ready;
        state_ = static_cast<ResourceState>(
            static_cast<uint32_t>(state_) &
            ~static_cast<uint32_t>(ResourceState::Outdated | ResourceState::Pending | ResourceState::Failed)
        );
    }

    // Tracking
    AllocStrategy GetAllocStrategy() const override { return allocStrategy_; }
    MemoryLocation GetMemoryLocation() const override { return memoryLocation_; }
    size_t GetAllocatedBytes() const override { return allocatedBytes_; }
    uintptr_t GetUniqueID() const override { return uniqueID_; }

    std::string GetDebugName() const override {
        #ifndef NDEBUG
        return std::string(typeid(T).name()) + "@" + std::to_string(uniqueID_);
        #else
        return "";
        #endif
    }

    void RegisterWithBudget(Vixen::RenderGraph::UnifiedBudgetManager* budgetMgr);
    void UnregisterFromBudget();

private:
    std::optional<T> storage_;
    ResourceState state_;
    uint64_t generation_;
    AllocStrategy allocStrategy_;
    MemoryLocation memoryLocation_;
    size_t allocatedBytes_;
    Vixen::RenderGraph::UnifiedBudgetManager* budgetManager_;
    uintptr_t uniqueID_;

    static uintptr_t GenerateUniqueID() {
        static std::atomic<uintptr_t> counter{0};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    void DetermineMemoryLocation() {
        if constexpr (std::is_same_v<T, VkBuffer> || std::is_same_v<T, VkImage>) {
            memoryLocation_ = MemoryLocation::DeviceLocal;
        } else if (allocStrategy_ == AllocStrategy::Stack) {
            memoryLocation_ = MemoryLocation::HostStack;
        } else {
            memoryLocation_ = MemoryLocation::HostHeap;
        }
    }

    void UpdateBudget(size_t oldSize, size_t newSize);
};

// ============================================================================
// TYPE ALIASES FOR COMMON USAGE PATTERNS
// ============================================================================

/**
 * @brief Stack-allocated image view array (member variable)
 *
 * Usage:
 * @code
 * class MyNode {
 *     StackImageViewArray<MyNode, 4> views_{this, &MyNode::views_};
 * };
 * @endcode
 */
template<typename Owner, size_t N = Vixen::RenderGraph::MAX_SWAPCHAIN_IMAGES>
using StackImageViewArray = UnifiedRM<Owner, BoundedArray<VkImageView, N>>;

/**
 * @brief Heap-allocated pipeline (member variable)
 */
template<typename Owner>
using HeapPipeline = UnifiedRM<Owner, VkPipeline>;

/**
 * @brief Device-allocated buffer (member variable)
 */
template<typename Owner>
using DeviceBuffer = UnifiedRM<Owner, VkBuffer>;

/**
 * @brief Local (non-member) variants for temporary resources
 */
template<size_t N = Vixen::RenderGraph::MAX_SWAPCHAIN_IMAGES>
using LocalStackImageViewArray = LocalRM<BoundedArray<VkImageView, N>>;

using LocalHeapPipeline = LocalRM<VkPipeline>;
using LocalDeviceBuffer = LocalRM<VkBuffer>;

} // namespace ResourceManagement
