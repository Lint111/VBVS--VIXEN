#pragma once
#include "RenderGraph/NodeInstance.h"
#include "RenderGraph/NodeType.h"
#include "RenderGraph/TypedNodeInstance.h"
#include "VulkanSwapChain.h"
#include "RenderGraph/Nodes/SwapChainNodeConfig.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for managing swapchain lifecycle
 * 
 * Handles swapchain creation, image acquisition, and recreation on resize.
 * This is a stateful node that maintains the presentation surface.
 * 
 * Type ID: 102
 */
class SwapChainNodeType : public NodeType {
public:
    SwapChainNodeType();
    virtual ~SwapChainNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

/**
 * @brief Node instance for swapchain management
 * 
 * Parameters:
 * - width (uint32_t): Swapchain width
 * - height (uint32_t): Swapchain height
 * - presentMode (string): "Immediate", "Mailbox", "Fifo", "FifoRelaxed" (default: "Fifo")
 * - imageCount (uint32_t): Desired number of swapchain images (default: 3)
 * 
 * Outputs:
 * - swapchain: VkSwapchainKHR handle
 * - colorImageViews: Array of swapchain image views
 * - currentImageIndex: Currently acquired image index
 */
class SwapChainNode : public TypedNode<SwapChainNodeConfig> {
public:
    SwapChainNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );
    virtual ~SwapChainNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // Accessors
    VkSwapchainKHR GetSwapchain() const;
    const std::vector<VkImageView>& GetColorImageViews() const;
    SwapChainPublicVariables* GetSwapchainPublic() const;
    uint32_t GetImageCount() const;
    uint32_t GetCurrentImageIndex() const { return currentImageIndex; }
    VkSemaphore GetImageAvailableSemaphore() const {
        if (imageAvailableSemaphores.empty()) return VK_NULL_HANDLE;
        const uint32_t frameIndex = (currentFrame > 0 ? currentFrame - 1 : 0) % imageAvailableSemaphores.size();
        return imageAvailableSemaphores[frameIndex];
    }
    VkFormat GetFormat() const;

    // Set the VulkanSwapChain wrapper to use
    void SetSwapChainWrapper(VulkanSwapChain* swapchain);

    // Acquire next swapchain image (returns image index)
    uint32_t AcquireNextImage(VkSemaphore presentCompleteSemaphore);

    // Recreate swapchain (for resize handling)
    void Recreate(uint32_t newWidth, uint32_t newHeight);

private:
    // Swapchain wrapper (from existing VulkanSwapChain)
    VulkanSwapChain* swapChainWrapper = nullptr;

    // Synchronization
    std::vector<VkSemaphore> imageAvailableSemaphores;  // Signaled when image is acquired
    uint32_t currentFrame = 0;

    // Current state
    uint32_t currentImageIndex = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

} // namespace Vixen::RenderGraph
