#include "pch.h"
#include "CacherAllocationHelpers.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace CashSystem {

using namespace ResourceManagement;

// Simple record for direct allocations to track VkDeviceMemory
struct DirectAllocationRecord {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

std::optional<BufferAllocation> CacherAllocationHelpers::AllocateBuffer(
    DeviceBudgetManager* budgetManager,
    Vixen::Vulkan::Resources::VulkanDevice* device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memoryFlags,
    const char* debugName
) {
    if (!device) {
        return std::nullopt;
    }

    // Budget-tracked path via DeviceBudgetManager
    if (budgetManager) {
        BufferAllocationRequest request{
            .size = size,
            .usage = usage,
            .location = MemoryFlagsToLocation(memoryFlags),
            .debugName = debugName ? debugName : "CacherBuffer"
        };

        auto result = budgetManager->AllocateBuffer(request);
        if (result) {
            return *result;
        }
        // Fall through to direct allocation if budget-tracked fails
    }

    // Direct Vulkan allocation (no budget tracking)
    return AllocateBufferDirect(device, size, usage, memoryFlags);
}

void CacherAllocationHelpers::FreeBuffer(
    DeviceBudgetManager* budgetManager,
    Vixen::Vulkan::Resources::VulkanDevice* device,
    BufferAllocation& allocation
) {
    if (allocation.buffer == VK_NULL_HANDLE) {
        return;
    }

    // If allocation came from DeviceBudgetManager (allocation handle is set and budget manager exists)
    if (budgetManager && allocation.allocation != nullptr) {
        budgetManager->FreeBuffer(allocation);
        allocation = {};
        return;
    }

    // Direct Vulkan free
    FreeBufferDirect(device, allocation);
}

MemoryLocation CacherAllocationHelpers::MemoryFlagsToLocation(VkMemoryPropertyFlags flags) {
    if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            return MemoryLocation::HostCached;
        }
        return MemoryLocation::DeviceLocal;
    }
    if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        return MemoryLocation::HostVisible;
    }
    return MemoryLocation::DeviceLocal;
}

std::optional<BufferAllocation> CacherAllocationHelpers::AllocateBufferDirect(
    Vixen::Vulkan::Resources::VulkanDevice* device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memoryFlags
) {
    if (!device) {
        return std::nullopt;
    }

    // VulkanDevice has public members: device (VkDevice) and gpu (VkPhysicalDevice*)
    VkDevice vkDevice = device->device;
    VkPhysicalDevice physicalDevice = device->gpu ? *(device->gpu) : VK_NULL_HANDLE;

    if (vkDevice == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
        return std::nullopt;
    }

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return std::nullopt;
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vkDevice, buffer, &memRequirements);

    // Check if buffer requires device address (for RT/AS buffers)
    const bool needsDeviceAddress = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;

    // Allocate memory with device address flag if needed
    VkMemoryAllocateFlagsInfo flagsInfo{};
    if (needsDeviceAddress) {
        flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = needsDeviceAddress ? &flagsInfo : nullptr;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, memoryFlags);

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, buffer, nullptr);
        return std::nullopt;
    }

    // Bind buffer to memory
    if (vkBindBufferMemory(vkDevice, buffer, memory, 0) != VK_SUCCESS) {
        vkFreeMemory(vkDevice, memory, nullptr);
        vkDestroyBuffer(vkDevice, buffer, nullptr);
        return std::nullopt;
    }

    // Get device address if needed
    VkDeviceAddress deviceAddress = 0;
    if (needsDeviceAddress) {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer;
        deviceAddress = vkGetBufferDeviceAddress(vkDevice, &addressInfo);
    }

    // Create record to track memory for freeing later
    auto* record = new DirectAllocationRecord{
        .memory = memory,
        .size = memRequirements.size
    };

    // Construct result - use allocation field to store our record
    BufferAllocation result{
        .buffer = buffer,
        .allocation = record,
        .size = memRequirements.size,
        .offset = 0,
        .mappedData = nullptr,
        .deviceAddress = deviceAddress,
        .canAlias = false,
        .isAliased = false
    };

    return result;
}

void CacherAllocationHelpers::FreeBufferDirect(
    Vixen::Vulkan::Resources::VulkanDevice* device,
    BufferAllocation& allocation
) {
    if (!device || allocation.buffer == VK_NULL_HANDLE) {
        return;
    }

    VkDevice vkDevice = device->device;
    if (vkDevice == VK_NULL_HANDLE) {
        return;
    }

    // Get memory from our record
    auto* record = static_cast<DirectAllocationRecord*>(allocation.allocation);
    if (record) {
        // Free memory first
        if (record->memory != VK_NULL_HANDLE) {
            vkFreeMemory(vkDevice, record->memory, nullptr);
        }
        delete record;
    }

    // Destroy buffer
    vkDestroyBuffer(vkDevice, allocation.buffer, nullptr);

    // Clear allocation
    allocation = {};
}

uint32_t CacherAllocationHelpers::FindMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    return FindMemoryType(memProperties, typeFilter, properties);
}

uint32_t CacherAllocationHelpers::FindMemoryType(
    const VkPhysicalDeviceMemoryProperties& memProperties,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
) {
    // Primary search: exact match of all requested properties
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    // Throw on failure - cachers expect this behavior for proper error handling
    throw std::runtime_error("[CacherAllocationHelpers::FindMemoryType] Failed to find suitable memory type");
}

void* CacherAllocationHelpers::MapBuffer(
    DeviceBudgetManager* budgetManager,
    Vixen::Vulkan::Resources::VulkanDevice* device,
    BufferAllocation& allocation
) {
    if (allocation.buffer == VK_NULL_HANDLE || !allocation.allocation) {
        return nullptr;
    }

    // Already mapped?
    if (allocation.mappedData != nullptr) {
        return allocation.mappedData;
    }

    // Budget-tracked path: use allocator's MapBuffer
    if (budgetManager) {
        auto* allocator = budgetManager->GetAllocator();
        if (allocator) {
            void* mapped = allocator->MapBuffer(allocation);
            if (mapped) {
                allocation.mappedData = mapped;
            }
            return mapped;
        }
    }

    // Direct path: map via VkDeviceMemory from our record
    if (!device) {
        return nullptr;
    }

    auto* record = static_cast<DirectAllocationRecord*>(allocation.allocation);
    if (!record || record->memory == VK_NULL_HANDLE) {
        return nullptr;
    }

    void* mapped = nullptr;
    VkResult result = vkMapMemory(device->device, record->memory, 0, allocation.size, 0, &mapped);
    if (result == VK_SUCCESS) {
        allocation.mappedData = mapped;
        return mapped;
    }

    return nullptr;
}

void CacherAllocationHelpers::UnmapBuffer(
    DeviceBudgetManager* budgetManager,
    Vixen::Vulkan::Resources::VulkanDevice* device,
    BufferAllocation& allocation
) {
    if (allocation.buffer == VK_NULL_HANDLE || allocation.mappedData == nullptr) {
        return;
    }

    // Budget-tracked path: use allocator's UnmapBuffer
    if (budgetManager) {
        auto* allocator = budgetManager->GetAllocator();
        if (allocator) {
            allocator->UnmapBuffer(allocation);
            allocation.mappedData = nullptr;
            return;
        }
    }

    // Direct path: unmap via VkDeviceMemory from our record
    if (!device) {
        return;
    }

    auto* record = static_cast<DirectAllocationRecord*>(allocation.allocation);
    if (record && record->memory != VK_NULL_HANDLE) {
        vkUnmapMemory(device->device, record->memory);
    }
    allocation.mappedData = nullptr;
}

} // namespace CashSystem
