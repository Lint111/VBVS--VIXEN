#include "Core/PerFrameResources.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace Vixen::RenderGraph {

void PerFrameResources::Initialize(Vixen::Vulkan::Resources::VulkanDevice* devicePtr, uint32_t frameCount) {
    if (!devicePtr) {
        throw std::runtime_error("PerFrameResources::Initialize - device is null");
    }

    if (frameCount == 0) {
        throw std::runtime_error("PerFrameResources::Initialize - frameCount must be > 0");
    }

    device = devicePtr;
    frames.resize(frameCount);
}

VkBuffer PerFrameResources::CreateUniformBuffer(uint32_t frameIndex, VkDeviceSize bufferSize) {
    ValidateFrameIndex(frameIndex, "CreateUniformBuffer");

    if (bufferSize == 0) {
        throw std::runtime_error("PerFrameResources::CreateUniformBuffer - bufferSize must be > 0");
    }

    auto& frame = frames[frameIndex];

    // Cleanup existing buffer if present
    if (frame.uniformBuffer != VK_NULL_HANDLE) {
        if (frame.uniformMappedData) {
            vkUnmapMemory(device->device, frame.uniformMemory);
            frame.uniformMappedData = nullptr;
        }
        vkDestroyBuffer(device->device, frame.uniformBuffer, nullptr);
        vkFreeMemory(device->device, frame.uniformMemory, nullptr);
    }

    // Create uniform buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device->device, &bufferInfo, nullptr, &frame.uniformBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("PerFrameResources::CreateUniformBuffer - vkCreateBuffer failed");
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device->device, frame.uniformBuffer, &memRequirements);

    // Allocate memory (HOST_VISIBLE | HOST_COHERENT for persistent mapping)
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    result = vkAllocateMemory(device->device, &allocInfo, nullptr, &frame.uniformMemory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(device->device, frame.uniformBuffer, nullptr);
        frame.uniformBuffer = VK_NULL_HANDLE;
        throw std::runtime_error("PerFrameResources::CreateUniformBuffer - vkAllocateMemory failed");
    }

    // Bind buffer to memory
    vkBindBufferMemory(device->device, frame.uniformBuffer, frame.uniformMemory, 0);

    // Map memory persistently (HOST_COHERENT means no need to flush/invalidate)
    result = vkMapMemory(device->device, frame.uniformMemory, 0, bufferSize, 0, &frame.uniformMappedData);
    if (result != VK_SUCCESS) {
        vkFreeMemory(device->device, frame.uniformMemory, nullptr);
        vkDestroyBuffer(device->device, frame.uniformBuffer, nullptr);
        frame.uniformBuffer = VK_NULL_HANDLE;
        frame.uniformMemory = VK_NULL_HANDLE;
        throw std::runtime_error("PerFrameResources::CreateUniformBuffer - vkMapMemory failed");
    }

    frame.uniformBufferSize = bufferSize;

    return frame.uniformBuffer;
}

VkBuffer PerFrameResources::GetUniformBuffer(uint32_t frameIndex) const {
    ValidateFrameIndex(frameIndex, "GetUniformBuffer");
    return frames[frameIndex].uniformBuffer;
}

void* PerFrameResources::GetUniformBufferMapped(uint32_t frameIndex) const {
    ValidateFrameIndex(frameIndex, "GetUniformBufferMapped");
    return frames[frameIndex].uniformMappedData;
}

void PerFrameResources::SetDescriptorSet(uint32_t frameIndex, VkDescriptorSet descriptorSet) {
    ValidateFrameIndex(frameIndex, "SetDescriptorSet");
    frames[frameIndex].descriptorSet = descriptorSet;
}

VkDescriptorSet PerFrameResources::GetDescriptorSet(uint32_t frameIndex) const {
    ValidateFrameIndex(frameIndex, "GetDescriptorSet");
    return frames[frameIndex].descriptorSet;
}

void PerFrameResources::SetCommandBuffer(uint32_t frameIndex, VkCommandBuffer commandBuffer) {
    ValidateFrameIndex(frameIndex, "SetCommandBuffer");
    frames[frameIndex].commandBuffer = commandBuffer;
}

VkCommandBuffer PerFrameResources::GetCommandBuffer(uint32_t frameIndex) const {
    ValidateFrameIndex(frameIndex, "GetCommandBuffer");
    return frames[frameIndex].commandBuffer;
}

PerFrameResources::FrameData& PerFrameResources::GetFrameData(uint32_t frameIndex) {
    ValidateFrameIndex(frameIndex, "GetFrameData");
    return frames[frameIndex];
}

const PerFrameResources::FrameData& PerFrameResources::GetFrameData(uint32_t frameIndex) const {
    ValidateFrameIndex(frameIndex, "GetFrameData");
    return frames[frameIndex];
}

void PerFrameResources::Cleanup() {
    if (!device) {
        return; // Not initialized
    }

    for (size_t i = 0; i < frames.size(); i++) {
        auto& frame = frames[i];

        // Unmap memory
        if (frame.uniformMappedData) {
            vkUnmapMemory(device->device, frame.uniformMemory);
            frame.uniformMappedData = nullptr;
        }

        // Destroy uniform buffer
        if (frame.uniformBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device->device, frame.uniformBuffer, nullptr);
            frame.uniformBuffer = VK_NULL_HANDLE;
        }

        // Free memory
        if (frame.uniformMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device->device, frame.uniformMemory, nullptr);
            frame.uniformMemory = VK_NULL_HANDLE;
        }

        // Note: descriptor sets are destroyed with descriptor pool (not owned here)
        // Note: command buffers are destroyed with command pool (not owned here)
        frame.descriptorSet = VK_NULL_HANDLE;
        frame.commandBuffer = VK_NULL_HANDLE;
    }

    frames.clear();
    device = nullptr;
}

uint32_t PerFrameResources::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    if (!device) {
        throw std::runtime_error("PerFrameResources::FindMemoryType - device is null");
    }

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(*device->gpu, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("PerFrameResources::FindMemoryType - failed to find suitable memory type");
}

void PerFrameResources::ValidateFrameIndex(uint32_t frameIndex, const char* funcName) const {
    if (frames.empty()) {
        std::string msg = "PerFrameResources::";
        msg += funcName;
        msg += " - not initialized (call Initialize first)";
        throw std::runtime_error(msg);
    }

    if (frameIndex >= frames.size()) {
        std::string msg = "PerFrameResources::";
        msg += funcName;
        msg += " - frameIndex " + std::to_string(frameIndex) +
               " out of range (max=" + std::to_string(frames.size() - 1) + ")";
        throw std::runtime_error(msg);
    }
}

} // namespace Vixen::RenderGraph