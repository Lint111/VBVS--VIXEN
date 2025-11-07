#pragma once
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "VulkanSwapChain.h"
#include "Data/Nodes/SwapChainNodeConfig.h"
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
class SwapChainNodeType : public TypedNodeType<SwapChainNodeConfig> {
public:
    SwapChainNodeType(const std::string& typeName = "SwapChain")
        : TypedNodeType<SwapChainNodeConfig>(typeName) {}
    virtual ~SwapChainNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
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
 * - currentFrameImageView: VkImageView for the current frame's swapchain image
 */
class SwapChainNode : public TypedNode<SwapChainNodeConfig> {
public:
    using Base = TypedNode<SwapChainNodeConfig>;

    SwapChainNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~SwapChainNode() override = default;

    // Accessors
    VkSwapchainKHR GetSwapchain() const;
    const std::vector<VkImageView>& GetColorImageViews() const;
    SwapChainPublicVariables* GetSwapchainPublic() const;
    uint32_t GetImageCount() const;
    uint32_t GetCurrentImageIndex() const { return currentImageIndex; }
    // Phase 0.2: Removed GetImageAvailableSemaphore() - semaphores managed by FrameSyncNode
    VkFormat GetFormat() const;

    // Set the VulkanSwapChain wrapper to use
    void SetSwapChainWrapper(VulkanSwapChain* swapchain);

    // Acquire next swapchain image (returns image index)
    uint32_t AcquireNextImage(VkSemaphore presentCompleteSemaphore);

    // Recreate swapchain (for resize handling)
    void Recreate(uint32_t newWidth, uint32_t newHeight);

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;


private:
    // Swapchain wrapper (from existing VulkanSwapChain)
    VulkanSwapChain* swapChainWrapper = nullptr;

    // Device handle is stored in the parent NodeInstance::device member

    // Phase 0.2: Semaphores now managed by FrameSyncNode (per-flight pattern)
    // Removed: std::vector<VkSemaphore> imageAvailableSemaphores
    uint32_t currentFrame = 0;

    // Phase 0.4: Track semaphore availability to prevent reuse
    // Each semaphore is used with its corresponding image index
    std::vector<bool> semaphoreInFlight;  // true if semaphore is currently in use

    // Current state
    uint32_t currentImageIndex = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

} // namespace Vixen::RenderGraph
