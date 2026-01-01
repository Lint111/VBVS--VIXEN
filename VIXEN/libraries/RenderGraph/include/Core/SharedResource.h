#pragma once

#include "Core/IMemoryAllocator.h"
#include "Core/DeferredDestruction.h"
#include <memory>
#include <atomic>
#include <functional>
#include <cassert>

namespace Vixen::RenderGraph {

/**
 * @brief Shared resource ownership scope
 *
 * Determines how the resource lifetime is managed.
 */
enum class ResourceScope : uint8_t {
    Transient,      // Single frame lifetime, can be aliased
    Persistent,     // Survives across frames, manually released
    Shared          // Reference counted, destroyed when last ref drops
};

/**
 * @brief Thread-safe intrusive reference count base
 *
 * Provides atomic reference counting for resources.
 * Inherit from this for intrusive refcounting, or use SharedResource<T> wrapper.
 */
class RefCountBase {
public:
    RefCountBase() = default;
    virtual ~RefCountBase() = default;

    // Non-copyable (copying would share refcount incorrectly)
    RefCountBase(const RefCountBase&) = delete;
    RefCountBase& operator=(const RefCountBase&) = delete;

    // Movable (transfers ownership)
    RefCountBase(RefCountBase&& other) noexcept
        : refCount_(other.refCount_.load(std::memory_order_relaxed)) {
        other.refCount_.store(0, std::memory_order_relaxed);
    }

    RefCountBase& operator=(RefCountBase&& other) noexcept {
        if (this != &other) {
            refCount_.store(other.refCount_.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
            other.refCount_.store(0, std::memory_order_relaxed);
        }
        return *this;
    }

    /**
     * @brief Increment reference count
     * @return New reference count
     */
    uint32_t AddRef() noexcept {
        return refCount_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    /**
     * @brief Decrement reference count
     * @return New reference count (0 means object should be destroyed)
     */
    uint32_t Release() noexcept {
        uint32_t prev = refCount_.fetch_sub(1, std::memory_order_acq_rel);
        return prev - 1;
    }

    /**
     * @brief Get current reference count
     */
    [[nodiscard]] uint32_t GetRefCount() const noexcept {
        return refCount_.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if this is the only reference
     */
    [[nodiscard]] bool IsUnique() const noexcept {
        return GetRefCount() == 1;
    }

private:
    std::atomic<uint32_t> refCount_{1};  // Starts at 1 (creator holds initial ref)
};

/**
 * @brief Shared GPU buffer with reference counting and deferred destruction
 *
 * Wraps a BufferAllocation with:
 * - Atomic reference counting
 * - Automatic deferred destruction when refcount hits 0
 * - Integration with DeviceBudgetManager for tracking
 *
 * Thread-safe: Yes
 */
class SharedBuffer : public RefCountBase {
public:
    /**
     * @brief Create shared buffer from allocation
     *
     * @param allocation Buffer allocation to wrap (takes ownership)
     * @param allocator Allocator that created the buffer (for destruction)
     * @param scope Resource lifetime scope
     */
    SharedBuffer(
        BufferAllocation allocation,
        IMemoryAllocator* allocator,
        ResourceScope scope = ResourceScope::Shared)
        : allocation_(std::move(allocation))
        , allocator_(allocator)
        , scope_(scope)
    {}

    ~SharedBuffer() {
        // Note: Destruction should happen via Release() path with deferred queue
        // Direct destructor call means improper cleanup
        if (allocation_ && allocator_) {
            // Fallback direct cleanup (shouldn't happen in normal use)
            allocator_->FreeBuffer(allocation_);
        }
    }

    // Non-copyable
    SharedBuffer(const SharedBuffer&) = delete;
    SharedBuffer& operator=(const SharedBuffer&) = delete;

    // Movable
    SharedBuffer(SharedBuffer&& other) noexcept
        : RefCountBase(std::move(other))
        , allocation_(std::move(other.allocation_))
        , allocator_(other.allocator_)
        , scope_(other.scope_)
    {
        other.allocator_ = nullptr;
    }

    SharedBuffer& operator=(SharedBuffer&& other) noexcept {
        if (this != &other) {
            RefCountBase::operator=(std::move(other));
            allocation_ = std::move(other.allocation_);
            allocator_ = other.allocator_;
            scope_ = other.scope_;
            other.allocator_ = nullptr;
        }
        return *this;
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    [[nodiscard]] VkBuffer GetBuffer() const noexcept {
        return allocation_.buffer;
    }

    [[nodiscard]] VkDeviceSize GetSize() const noexcept {
        return allocation_.size;
    }

    [[nodiscard]] VkDeviceSize GetOffset() const noexcept {
        return allocation_.offset;
    }

    [[nodiscard]] void* GetMappedData() const noexcept {
        return allocation_.mappedData;
    }

    [[nodiscard]] const BufferAllocation& GetAllocation() const noexcept {
        return allocation_;
    }

    [[nodiscard]] ResourceScope GetScope() const noexcept {
        return scope_;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return static_cast<bool>(allocation_);
    }

    explicit operator bool() const noexcept {
        return IsValid();
    }

    // =========================================================================
    // Destruction
    // =========================================================================

    /**
     * @brief Queue for deferred destruction
     *
     * @param queue Deferred destruction queue
     * @param currentFrame Current frame number
     */
    void QueueDestruction(DeferredDestructionQueue& queue, uint64_t currentFrame) {
        if (!allocation_ || !allocator_) {
            return;
        }

        // Capture by value for deferred lambda
        BufferAllocation alloc = std::move(allocation_);
        IMemoryAllocator* allocator = allocator_;
        allocator_ = nullptr;

        queue.AddGeneric([allocator, alloc]() mutable {
            allocator->FreeBuffer(alloc);
        }, currentFrame);
    }

private:
    BufferAllocation allocation_;
    IMemoryAllocator* allocator_ = nullptr;
    ResourceScope scope_ = ResourceScope::Shared;
};

/**
 * @brief Shared GPU image with reference counting and deferred destruction
 */
class SharedImage : public RefCountBase {
public:
    SharedImage(
        ImageAllocation allocation,
        IMemoryAllocator* allocator,
        ResourceScope scope = ResourceScope::Shared)
        : allocation_(std::move(allocation))
        , allocator_(allocator)
        , scope_(scope)
    {}

    ~SharedImage() {
        if (allocation_ && allocator_) {
            allocator_->FreeImage(allocation_);
        }
    }

    // Non-copyable
    SharedImage(const SharedImage&) = delete;
    SharedImage& operator=(const SharedImage&) = delete;

    // Movable
    SharedImage(SharedImage&& other) noexcept
        : RefCountBase(std::move(other))
        , allocation_(std::move(other.allocation_))
        , allocator_(other.allocator_)
        , scope_(other.scope_)
    {
        other.allocator_ = nullptr;
    }

    SharedImage& operator=(SharedImage&& other) noexcept {
        if (this != &other) {
            RefCountBase::operator=(std::move(other));
            allocation_ = std::move(other.allocation_);
            allocator_ = other.allocator_;
            scope_ = other.scope_;
            other.allocator_ = nullptr;
        }
        return *this;
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    [[nodiscard]] VkImage GetImage() const noexcept {
        return allocation_.image;
    }

    [[nodiscard]] VkDeviceSize GetSize() const noexcept {
        return allocation_.size;
    }

    [[nodiscard]] const ImageAllocation& GetAllocation() const noexcept {
        return allocation_;
    }

    [[nodiscard]] ResourceScope GetScope() const noexcept {
        return scope_;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return static_cast<bool>(allocation_);
    }

    explicit operator bool() const noexcept {
        return IsValid();
    }

    void QueueDestruction(DeferredDestructionQueue& queue, uint64_t currentFrame) {
        if (!allocation_ || !allocator_) {
            return;
        }

        ImageAllocation alloc = std::move(allocation_);
        IMemoryAllocator* allocator = allocator_;
        allocator_ = nullptr;

        queue.AddGeneric([allocator, alloc]() mutable {
            allocator->FreeImage(alloc);
        }, currentFrame);
    }

private:
    ImageAllocation allocation_;
    IMemoryAllocator* allocator_ = nullptr;
    ResourceScope scope_ = ResourceScope::Shared;
};

/**
 * @brief Smart pointer for shared resources with deferred destruction
 *
 * Similar to std::shared_ptr but integrates with DeferredDestructionQueue.
 * When the last reference is released, the resource is queued for deferred
 * destruction rather than immediately destroyed.
 *
 * @tparam T SharedBuffer or SharedImage
 */
template<typename T>
class SharedResourcePtr {
public:
    SharedResourcePtr() noexcept = default;

    explicit SharedResourcePtr(T* resource,
                               DeferredDestructionQueue* queue = nullptr,
                               uint64_t* frameCounter = nullptr) noexcept
        : resource_(resource)
        , destructionQueue_(queue)
        , frameCounter_(frameCounter)
    {}

    ~SharedResourcePtr() {
        Release();
    }

    // Copy (adds reference)
    SharedResourcePtr(const SharedResourcePtr& other) noexcept
        : resource_(other.resource_)
        , destructionQueue_(other.destructionQueue_)
        , frameCounter_(other.frameCounter_)
    {
        if (resource_) {
            resource_->AddRef();
        }
    }

    SharedResourcePtr& operator=(const SharedResourcePtr& other) noexcept {
        if (this != &other) {
            Release();
            resource_ = other.resource_;
            destructionQueue_ = other.destructionQueue_;
            frameCounter_ = other.frameCounter_;
            if (resource_) {
                resource_->AddRef();
            }
        }
        return *this;
    }

    // Move (transfers ownership)
    SharedResourcePtr(SharedResourcePtr&& other) noexcept
        : resource_(other.resource_)
        , destructionQueue_(other.destructionQueue_)
        , frameCounter_(other.frameCounter_)
    {
        other.resource_ = nullptr;
    }

    SharedResourcePtr& operator=(SharedResourcePtr&& other) noexcept {
        if (this != &other) {
            Release();
            resource_ = other.resource_;
            destructionQueue_ = other.destructionQueue_;
            frameCounter_ = other.frameCounter_;
            other.resource_ = nullptr;
        }
        return *this;
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    [[nodiscard]] T* Get() const noexcept {
        return resource_;
    }

    [[nodiscard]] T* operator->() const noexcept {
        return resource_;
    }

    [[nodiscard]] T& operator*() const noexcept {
        return *resource_;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return resource_ != nullptr;
    }

    [[nodiscard]] uint32_t UseCount() const noexcept {
        return resource_ ? resource_->GetRefCount() : 0;
    }

    [[nodiscard]] bool IsUnique() const noexcept {
        return resource_ && resource_->IsUnique();
    }

    // =========================================================================
    // Modifiers
    // =========================================================================

    void Reset() noexcept {
        Release();
    }

    void Reset(T* resource,
               DeferredDestructionQueue* queue = nullptr,
               uint64_t* frameCounter = nullptr) noexcept {
        Release();
        resource_ = resource;
        destructionQueue_ = queue;
        frameCounter_ = frameCounter;
    }

    /**
     * @brief Swap with another pointer
     */
    void Swap(SharedResourcePtr& other) noexcept {
        std::swap(resource_, other.resource_);
        std::swap(destructionQueue_, other.destructionQueue_);
        std::swap(frameCounter_, other.frameCounter_);
    }

private:
    void Release() noexcept {
        if (resource_) {
            if (resource_->Release() == 0) {
                // Last reference - queue for destruction
                // Both queue and frame counter must be set, or neither (for testing)
                assert((destructionQueue_ == nullptr) == (frameCounter_ == nullptr) &&
                       "SharedResourcePtr: destructionQueue and frameCounter must both be set or both null");
                if (destructionQueue_ && frameCounter_) {
                    resource_->QueueDestruction(*destructionQueue_, *frameCounter_);
                }
                delete resource_;
            }
            resource_ = nullptr;
        }
    }

    T* resource_ = nullptr;
    DeferredDestructionQueue* destructionQueue_ = nullptr;
    uint64_t* frameCounter_ = nullptr;
};

// Type aliases
using SharedBufferPtr = SharedResourcePtr<SharedBuffer>;
using SharedImagePtr = SharedResourcePtr<SharedImage>;

/**
 * @brief Factory for creating shared resources
 */
class SharedResourceFactory {
public:
    SharedResourceFactory(
        IMemoryAllocator* allocator,
        DeferredDestructionQueue* destructionQueue,
        uint64_t* frameCounter)
        : allocator_(allocator)
        , destructionQueue_(destructionQueue)
        , frameCounter_(frameCounter)
    {}

    /**
     * @brief Create a shared buffer
     */
    [[nodiscard]] SharedBufferPtr CreateBuffer(
        const BufferAllocationRequest& request,
        ResourceScope scope = ResourceScope::Shared)
    {
        if (!allocator_) {
            return SharedBufferPtr{};
        }

        auto result = allocator_->AllocateBuffer(request);
        if (!result.has_value()) {
            return SharedBufferPtr{};
        }

        auto* buffer = new SharedBuffer(std::move(result.value()), allocator_, scope);
        return SharedBufferPtr(buffer, destructionQueue_, frameCounter_);
    }

    /**
     * @brief Create a shared image
     */
    [[nodiscard]] SharedImagePtr CreateImage(
        const ImageAllocationRequest& request,
        ResourceScope scope = ResourceScope::Shared)
    {
        if (!allocator_) {
            return SharedImagePtr{};
        }

        auto result = allocator_->AllocateImage(request);
        if (!result.has_value()) {
            return SharedImagePtr{};
        }

        auto* image = new SharedImage(std::move(result.value()), allocator_, scope);
        return SharedImagePtr(image, destructionQueue_, frameCounter_);
    }

private:
    IMemoryAllocator* allocator_;
    DeferredDestructionQueue* destructionQueue_;
    uint64_t* frameCounter_;
};

} // namespace Vixen::RenderGraph
