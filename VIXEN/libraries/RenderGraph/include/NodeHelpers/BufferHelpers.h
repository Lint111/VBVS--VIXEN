#pragma once

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <string>

namespace RenderGraph::NodeHelpers {

struct BufferAllocationResult {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

/// Finds the appropriate memory type index for Vulkan memory requirements.
/// Throws if no suitable memory type found.
inline uint32_t FindMemoryType(
    const VkPhysicalDeviceMemoryProperties& memProperties,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties,
    const std::string& context = ""
) {
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties)) {
            return i;
        }
    }
    throw std::runtime_error(
        "Failed to find suitable memory type" + (context.empty() ? "" : " for " + context)
    );
}

/// Creates a device-local GPU buffer with appropriate memory allocation.
/// Caller is responsible for cleanup via vkDestroyBuffer/vkFreeMemory.
inline BufferAllocationResult CreateDeviceLocalBuffer(
    VkDevice device,
    VkPhysicalDeviceMemoryProperties memProperties,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    const std::string& bufferName = ""
) {
    BufferAllocationResult result;

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult vkResult = vkCreateBuffer(device, &bufferInfo, nullptr, &result.buffer);
    if (vkResult != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to create buffer" + (bufferName.empty() ? "" : ": " + bufferName)
        );
    }

    // Query memory requirements
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, result.buffer, &memReq);

    // Find device-local memory type
    uint32_t memoryTypeIndex = FindMemoryType(
        memProperties,
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        bufferName
    );

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    vkResult = vkAllocateMemory(device, &allocInfo, nullptr, &result.memory);
    if (vkResult != VK_SUCCESS) {
        vkDestroyBuffer(device, result.buffer, nullptr);
        throw std::runtime_error(
            "Failed to allocate memory for buffer" + (bufferName.empty() ? "" : ": " + bufferName)
        );
    }

    // Bind memory to buffer
    vkBindBufferMemory(device, result.buffer, result.memory, 0);

    return result;
}

/// Destroys a buffer and its associated memory. Asserts device is not null.
inline void DestroyBuffer(
    VkDevice device,
    VkBuffer& buffer,
    VkDeviceMemory& memory,
    const std::string& bufferName = ""
) {
    if (!device) return;

    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }

    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

} // namespace RenderGraph::NodeHelpers
