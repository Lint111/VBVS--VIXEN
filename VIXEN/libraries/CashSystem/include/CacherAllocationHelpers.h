#pragma once

#include "Memory/DeviceBudgetManager.h"
#include "Memory/IMemoryAllocator.h"
#include <vulkan/vulkan.h>
#include <optional>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {

/**
 * @brief Helper functions for budget-tracked buffer allocation in cachers
 *
 * These non-template functions provide the actual allocation logic
 * for TypedCacher::AllocateBufferTracked and FreeBufferTracked.
 */
class CacherAllocationHelpers {
public:
    /**
     * @brief Allocate buffer using budget manager if available, else direct Vulkan
     *
     * @param budgetManager Budget manager (nullptr = use direct allocation)
     * @param device Vulkan device for direct allocation fallback
     * @param size Buffer size in bytes
     * @param usage Vulkan buffer usage flags
     * @param memoryFlags Vulkan memory property flags
     * @param debugName Optional debug name
     * @return BufferAllocation on success, empty optional on failure
     */
    static std::optional<ResourceManagement::BufferAllocation> AllocateBuffer(
        ResourceManagement::DeviceBudgetManager* budgetManager,
        Vixen::Vulkan::Resources::VulkanDevice* device,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memoryFlags,
        const char* debugName = nullptr
    );

    /**
     * @brief Free buffer using appropriate path
     *
     * @param budgetManager Budget manager used for allocation (nullptr = direct free)
     * @param device Vulkan device for direct free fallback
     * @param allocation Allocation to free
     */
    static void FreeBuffer(
        ResourceManagement::DeviceBudgetManager* budgetManager,
        Vixen::Vulkan::Resources::VulkanDevice* device,
        ResourceManagement::BufferAllocation& allocation
    );

    /**
     * @brief Convert VkMemoryPropertyFlags to MemoryLocation
     */
    static ResourceManagement::MemoryLocation MemoryFlagsToLocation(VkMemoryPropertyFlags flags);

    /**
     * @brief Map buffer memory for CPU access
     *
     * Works with both budget-tracked and direct allocations.
     *
     * @param budgetManager Budget manager (nullptr = direct mapping)
     * @param device Vulkan device for direct mapping fallback
     * @param allocation Buffer allocation to map
     * @return Mapped pointer or nullptr on failure
     */
    static void* MapBuffer(
        ResourceManagement::DeviceBudgetManager* budgetManager,
        Vixen::Vulkan::Resources::VulkanDevice* device,
        ResourceManagement::BufferAllocation& allocation
    );

    /**
     * @brief Unmap previously mapped buffer memory
     *
     * @param budgetManager Budget manager (nullptr = direct unmapping)
     * @param device Vulkan device for direct unmapping fallback
     * @param allocation Buffer allocation to unmap
     */
    static void UnmapBuffer(
        ResourceManagement::DeviceBudgetManager* budgetManager,
        Vixen::Vulkan::Resources::VulkanDevice* device,
        ResourceManagement::BufferAllocation& allocation
    );

    /**
     * @brief Find suitable memory type for allocation
     *
     * Consolidated utility to replace duplicate implementations in cachers.
     * Uses cached memory properties from VulkanDevice when available.
     *
     * @param physicalDevice Physical device for memory properties query
     * @param typeFilter Memory type bits from vkGetBufferMemoryRequirements
     * @param properties Required memory property flags
     * @return Memory type index
     * @throws std::runtime_error if no suitable memory type found
     */
    static uint32_t FindMemoryType(
        VkPhysicalDevice physicalDevice,
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    );

    /**
     * @brief Find memory type using cached properties from VulkanDevice
     *
     * More efficient version using pre-cached memory properties.
     *
     * @param memProperties Cached memory properties from VulkanDevice
     * @param typeFilter Memory type bits from vkGetBufferMemoryRequirements
     * @param properties Required memory property flags
     * @return Memory type index
     * @throws std::runtime_error if no suitable memory type found
     */
    static uint32_t FindMemoryType(
        const VkPhysicalDeviceMemoryProperties& memProperties,
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    );

private:
    /**
     * @brief Direct Vulkan buffer allocation (no budget tracking)
     */
    static std::optional<ResourceManagement::BufferAllocation> AllocateBufferDirect(
        Vixen::Vulkan::Resources::VulkanDevice* device,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memoryFlags
    );

    /**
     * @brief Direct Vulkan buffer free
     */
    static void FreeBufferDirect(
        Vixen::Vulkan::Resources::VulkanDevice* device,
        ResourceManagement::BufferAllocation& allocation
    );
};

} // namespace CashSystem
