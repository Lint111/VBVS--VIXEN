#pragma once

#include "Data/Core/ResourceTypeTraits.h"
#include "Data/Core/ResourceTypes.h"
#include "Data/Core/ResourceVariant.h"
#include <memory>
#include <utility>

// Import ResourceLocation from VIXEN namespace
namespace VIXEN {
    enum class ResourceLocation;
}

namespace Vixen::RenderGraph {

// Import ResourceLocation into this namespace for convenience
using VIXEN::ResourceLocation;

// Forward declarations
class ResourcePool;
class Resource;

/**
 * @brief RAII wrapper for automatic resource management
 *
 * RM<T> provides automatic resource allocation and cleanup with:
 * - RAII semantics (cleanup on destruction)
 * - Move-only semantics (prevents double-free)
 * - Transparent access to underlying resource
 * - Integration with ResourcePool for aliasing
 * - Type-safe access via ResourceTypeTraits
 *
 * Example:
 * @code
 * auto texture = RM<VkImage>::Request(pool, descriptor, ResourceLifetime::FrameLocal);
 * vkCmdBindImage(..., texture.Get(), ...);
 * // Automatic cleanup when texture goes out of scope
 * @endcode
 */
template<typename T>
class RM {
public:
    // === Construction ===

    /**
     * @brief Request a new managed resource from the pool
     * @param pool ResourcePool to allocate from
     * @param descriptor Resource descriptor (format, size, etc.)
     * @param lifetime Resource lifetime for aliasing optimization
     * @return RAII-managed resource
     */
    static RM<T> Request(
        ResourcePool& pool,
        const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
        ResourceLifetime lifetime = ResourceLifetime::Transient
    );

    /**
     * @brief Construct empty (null) resource
     */
    RM() = default;

    /**
     * @brief Destructor - automatically releases resource
     */
    ~RM();

    // === Move semantics (move-only) ===

    RM(RM&& other) noexcept;
    RM& operator=(RM&& other) noexcept;

    // Delete copy (prevent double-free)
    RM(const RM&) = delete;
    RM& operator=(const RM&) = delete;

    // === Access ===

    /**
     * @brief Get underlying resource pointer
     */
    T* Get() const;

    /**
     * @brief Arrow operator for transparent access
     */
    T* operator->() const { return Get(); }

    /**
     * @brief Dereference operator
     */
    T& operator*() const { return *Get(); }

    /**
     * @brief Boolean conversion (check if valid)
     */
    explicit operator bool() const { return resource_ != nullptr; }

    /**
     * @brief Check if resource is null
     */
    bool IsNull() const { return resource_ == nullptr; }

    // === Status Queries ===

    /**
     * @brief Check if resource was aliased (reused memory)
     */
    bool IsAliased() const;

    /**
     * @brief Get resource location (stack/heap/VRAM)
     *
     * @note TODO: Currently returns placeholder. Needs integration with Resource
     *       metadata to query actual allocation location.
     */
    ResourceLocation GetLocation() const;

    /**
     * @brief Get resource size in bytes
     *
     * @note TODO: Currently returns 0. Needs integration with Resource metadata
     *       to query actual memory footprint.
     */
    size_t GetBytes() const;

    /**
     * @brief Get resource lifetime
     */
    ResourceLifetime GetLifetime() const { return lifetime_; }

    /**
     * @brief Release ownership without cleanup
     * @return Raw resource pointer (caller responsible for cleanup)
     */
    Resource* Release();

private:
    // Private constructor (use Request() factory)
    RM(Resource* resource, ResourcePool* pool, ResourceLifetime lifetime);

    Resource* resource_ = nullptr;
    ResourcePool* pool_ = nullptr;
    ResourceLifetime lifetime_ = ResourceLifetime::Transient;
    bool wasAliased_ = false;
};

// === Implementation ===

template<typename T>
RM<T> RM<T>::Request(
    ResourcePool& pool,
    const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
    ResourceLifetime lifetime
) {
    // Allocate resource through pool (may use aliasing)
    // TODO: Query wasAliased_ status from pool after allocation
    auto* resource = pool.AllocateResource<T>(descriptor, lifetime);
    return RM<T>(resource, &pool, lifetime);
}

template<typename T>
RM<T>::RM(Resource* resource, ResourcePool* pool, ResourceLifetime lifetime)
    : resource_(resource)
    , pool_(pool)
    , lifetime_(lifetime)
    , wasAliased_(false)  // TODO: Query from pool/resource metadata
{
}

template<typename T>
RM<T>::~RM() {
    if (resource_ && pool_) {
        pool_->ReleaseResource(resource_);
        resource_ = nullptr;
    }
}

template<typename T>
RM<T>::RM(RM&& other) noexcept
    : resource_(other.resource_)
    , pool_(other.pool_)
    , lifetime_(other.lifetime_)
    , wasAliased_(other.wasAliased_)
{
    other.resource_ = nullptr;
    other.pool_ = nullptr;
}

template<typename T>
RM<T>& RM<T>::operator=(RM&& other) noexcept {
    if (this != &other) {
        // Release current resource
        if (resource_ && pool_) {
            pool_->ReleaseResource(resource_);
        }

        // Move from other
        resource_ = other.resource_;
        pool_ = other.pool_;
        lifetime_ = other.lifetime_;
        wasAliased_ = other.wasAliased_;

        // Clear other
        other.resource_ = nullptr;
        other.pool_ = nullptr;
    }
    return *this;
}

template<typename T>
T* RM<T>::Get() const {
    if (!resource_) {
        return nullptr;
    }

    // TODO: Use ResourceTypeTraits to extract typed resource from variant
    // The Resource class needs to provide a method to extract the underlying
    // resource data based on type. Possible approaches:
    //
    // Option 1: Resource::As<T>() - Direct type extraction
    //   return resource_->As<T>();
    //
    // Option 2: Resource::GetData<T>() - Typed data accessor
    //   return resource_->GetData<T>();
    //
    // Option 3: std::get<T>(resource_->GetVariant())
    //   return std::get<T>(&resource_->GetVariant());
    //
    // For now, use placeholder cast (will be replaced with proper variant extraction)
    return static_cast<T*>(resource_);  // Placeholder - needs proper implementation
}

template<typename T>
bool RM<T>::IsAliased() const {
    // TODO: Query aliasing status from resource metadata or pool
    // The AliasingEngine tracks which resources are aliased, but this information
    // needs to be exposed through the Resource class or stored during allocation.
    //
    // Possible implementation:
    //   if (resource_) {
    //       return resource_->GetMetadata().isAliased;
    //   }
    //   return false;
    return wasAliased_;
}

template<typename T>
ResourceLocation RM<T>::GetLocation() const {
    // TODO: Query from resource metadata
    // The Resource class should track where the resource is allocated:
    // - ResourceLocation::Stack (stack allocator)
    // - ResourceLocation::Heap (standard heap)
    // - ResourceLocation::VRAM (GPU device memory)
    //
    // Possible implementation:
    //   if (resource_) {
    //       return resource_->GetMetadata().location;
    //   }
    //   return ResourceLocation::Heap;
    return ResourceLocation::VRAM;  // Placeholder - assume VRAM for GPU resources
}

template<typename T>
size_t RM<T>::GetBytes() const {
    // TODO: Query from resource metadata
    // The Resource class should track the size of the allocated memory.
    // This is particularly important for budget tracking and profiling.
    //
    // Possible implementation:
    //   if (resource_) {
    //       return resource_->GetMetadata().sizeInBytes;
    //   }
    //   return 0;
    return 0;  // Placeholder
}

template<typename T>
Resource* RM<T>::Release() {
    auto* temp = resource_;
    resource_ = nullptr;
    pool_ = nullptr;
    return temp;
}

} // namespace Vixen::RenderGraph
