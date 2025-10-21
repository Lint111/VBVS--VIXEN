#pragma once
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for presenting rendered images to the swapchain
 * 
 * Queues presentation operations with proper synchronization.
 * This is the final node in the rendering pipeline.
 * 
 * Type ID: 110
 */
class PresentNodeType : public NodeType {
public:
    PresentNodeType();
    virtual ~PresentNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

/**
 * @brief Node instance for presentation operations
 * 
 * Parameters:
 * - waitForIdle (bool): Whether to wait for device idle after present (default: true for compatibility)
 * 
 * Inputs (via Set methods):
 * - swapchain: VkSwapchainKHR to present to
 * - imageIndex: Index of the swapchain image to present
 * - queue: VkQueue to submit present operation to
 * - renderCompleteSemaphore: Semaphore to wait on before presenting
 * - presentFunction: Function pointer to vkQueuePresentKHR
 * 
 * Outputs:
 * - result: VkResult of the present operation
 */
class PresentNode : public NodeInstance {
public:
    PresentNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );
    virtual ~PresentNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // Set input references
    void SetSwapchain(VkSwapchainKHR swapchain);
    void SetImageIndex(uint32_t index);
    void SetQueue(VkQueue queue);
    void SetRenderCompleteSemaphore(VkSemaphore semaphore);
    void SetPresentFunction(PFN_vkQueuePresentKHR func);

    // Execute presentation
    VkResult Present();

    // Get last present result
    VkResult GetLastResult() const { return lastResult; }

private:
    // Input references
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    uint32_t imageIndex = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkSemaphore renderCompleteSemaphore = VK_NULL_HANDLE;
    PFN_vkQueuePresentKHR fpQueuePresent = nullptr;

    // Configuration
    bool waitForIdle = true;

    // State
    VkResult lastResult = VK_SUCCESS;
};

} // namespace Vixen::RenderGraph
