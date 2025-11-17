#pragma once

#include "Headers.h"
#include <vector>
#include <memory>
#include <stdexcept>

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

/**
 * @brief Helper class for managing per-frame GPU resources
 *
 * Implements ring buffer pattern to prevent CPU-GPU race conditions.
 * Each swapchain image gets its own set of resources (UBOs, command buffers, etc).
 *
 * Pattern:
 *   Frame N:   GPU reads resources[imageIndex=0], CPU writes resources[imageIndex=1]
 *   Frame N+1: GPU reads resources[imageIndex=1], CPU writes resources[imageIndex=2]
 *   Frame N+2: GPU reads resources[imageIndex=2], CPU writes resources[imageIndex=0]
 *
 * Usage:
 *   // In Compile():
 *   perFrameResources.Initialize(device, imageCount);
 *   for (uint32_t i = 0; i < imageCount; i++) {
 *       perFrameResources.CreateUniformBuffer(i, sizeof(MyUBO));
 *   }
 *
 *   // In Execute():
 *   uint32_t imageIndex = GetCurrentImageIndex();
 *   void* mappedData = perFrameResources.GetUniformBufferMapped(imageIndex);
 *   memcpy(mappedData, &ubo, sizeof(ubo));
 */
class PerFrameResources {
public:
    /**
     * @brief Per-frame resource data
     *
     * Each frame gets its own uniform buffer, descriptor set, and optional command buffer.
     */
    struct FrameData {
        // Uniform buffer resources
        VkBuffer uniformBuffer = VK_NULL_HANDLE;
        VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
        void* uniformMappedData = nullptr;
        VkDeviceSize uniformBufferSize = 0;

        // Descriptor set (if using per-frame descriptors)
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

        // Command buffer (if node records per-frame commands)
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        // Frame synchronization (optional - for future use)
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore semaphore = VK_NULL_HANDLE;
    };

    PerFrameResources() = default;
    ~PerFrameResources() = default;

    /**
     * @brief Initialize per-frame resources
     * @param device Vulkan device pointer
     * @param frameCount Number of frames in flight (usually swapchain image count)
     */
    void Initialize(Vixen::Vulkan::Resources::VulkanDevice* device, uint32_t frameCount);

    /**
     * @brief Create uniform buffer for a specific frame
     * @param frameIndex Frame index (0 to frameCount-1)
     * @param bufferSize Size of uniform buffer in bytes
     * @return VkBuffer handle
     */
    VkBuffer CreateUniformBuffer(uint32_t frameIndex, VkDeviceSize bufferSize);

    /**
     * @brief Get uniform buffer for a frame
     * @param frameIndex Frame index
     * @return VkBuffer handle (VK_NULL_HANDLE if not created)
     */
    VkBuffer GetUniformBuffer(uint32_t frameIndex) const;

    /**
     * @brief Get mapped memory pointer for uniform buffer
     * @param frameIndex Frame index
     * @return Mapped memory pointer (nullptr if not created/mapped)
     */
    void* GetUniformBufferMapped(uint32_t frameIndex) const;

    /**
     * @brief Set descriptor set for a frame
     * @param frameIndex Frame index
     * @param descriptorSet VkDescriptorSet handle
     */
    void SetDescriptorSet(uint32_t frameIndex, VkDescriptorSet descriptorSet);

    /**
     * @brief Get descriptor set for a frame
     * @param frameIndex Frame index
     * @return VkDescriptorSet handle (VK_NULL_HANDLE if not set)
     */
    VkDescriptorSet GetDescriptorSet(uint32_t frameIndex) const;

    /**
     * @brief Set command buffer for a frame
     * @param frameIndex Frame index
     * @param commandBuffer VkCommandBuffer handle
     */
    void SetCommandBuffer(uint32_t frameIndex, VkCommandBuffer commandBuffer);

    /**
     * @brief Get command buffer for a frame
     * @param frameIndex Frame index
     * @return VkCommandBuffer handle (VK_NULL_HANDLE if not set)
     */
    VkCommandBuffer GetCommandBuffer(uint32_t frameIndex) const;

    /**
     * @brief Get frame data for a specific frame
     * @param frameIndex Frame index
     * @return FrameData reference
     */
    FrameData& GetFrameData(uint32_t frameIndex);
    const FrameData& GetFrameData(uint32_t frameIndex) const;

    /**
     * @brief Get number of frames
     * @return Frame count
     */
    uint32_t GetFrameCount() const { return static_cast<uint32_t>(frames.size()); }

    /**
     * @brief Check if initialized
     * @return True if initialized
     */
    bool IsInitialized() const { return device != nullptr && !frames.empty(); }

    /**
     * @brief Cleanup all resources
     *
     * Destroys all uniform buffers, frees memory.
     * Note: Does NOT destroy descriptor sets (owned by descriptor pool).
     * Note: Does NOT destroy command buffers (owned by command pool).
     */
    void Cleanup();

private:
    Vixen::Vulkan::Resources::VulkanDevice* device = nullptr;
    std::vector<FrameData> frames;

    // Helper: Find memory type
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // Validation
    void ValidateFrameIndex(uint32_t frameIndex, const char* funcName) const;
};

} // namespace Vixen::RenderGraph