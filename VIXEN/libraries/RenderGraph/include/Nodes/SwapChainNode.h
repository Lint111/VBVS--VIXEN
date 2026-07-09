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
    // Unconditional trivial const accessors (fail-scenario window-stimulus contracts need these to
    // compare the swapchain's actual extent against the expected post-resize extent).
    uint32_t GetWidth() const { return width; }
    uint32_t GetHeight() const { return height; }

    // Set the VulkanSwapChain wrapper to use
    void SetSwapChainWrapper(VulkanSwapChain* swapchain);

    // Acquire next swapchain image (returns image index)
    uint32_t AcquireNextImage(VkSemaphore presentCompleteSemaphore);

    // Recreate swapchain (for resize handling)
    void Recreate(uint32_t newWidth, uint32_t newHeight);

#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
    // Fail-scenario seam: how many times CompileImpl has run (recreations + the initial compile),
    // so a regression test can assert a burst of resize events collapses into a bounded number of
    // recompiles instead of one per event.
    uint32_t CompileCountForTest() const { return compileCount_; }
#endif

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // === Compile phase helper methods ===
    void ValidateCompileInputs(GLFWwindow* window, VkInstance instance);
    void LoadExtensionsAndCreateSurface(VkInstance instance, GLFWwindow* window);
    void SetupFormatsAndCapabilities(uint32_t graphicsQueueIndex);
    void CreateSwapchainAndViews();
    void PublishCompileOutputs(TypedCompileContext& ctx);

    // FR-3: per-IMAGE sync resources are owned here, sized to the EXACT swapchain image
    // count (vkGetSwapchainImagesKHR), not pre-sized to a constant in FrameSyncNode.
    void CreatePerImageSyncResources();   // (re)creates, reusing existing arrays when the count is unchanged
    void DestroyPerImageSyncResources();

    // Swapchain wrapper (from existing VulkanSwapChain)
    VulkanSwapChain* swapChainWrapper = nullptr;

    // Per-IMAGE sync (indexed by acquired image index), sized to the actual image count.
    std::vector<VkSemaphore> renderCompleteSemaphores;  // signaled by render, waited by present
    std::vector<VkFence> presentFences;                 // per-image present fences (VK_EXT_swapchain_maintenance1)

    // Per-image in-flight fence tracking (canonical "imagesInFlight" pattern). Records which
    // per-FLIGHT fence (from FrameSyncNode) last submitted work for each image. When
    // MAX_FRAMES_IN_FLIGHT != swapchain image count, the flight ring and the image ring desync, so
    // FrameSyncNode's per-flight wait does NOT guarantee the previous submission that touched THIS
    // image's command buffer / descriptor set / query pool has completed. Before reusing an image
    // we wait on its recorded fence, then stamp it with the current frame's fence. NOT owned (the
    // fences belong to FrameSyncNode) — this is a non-owning bookkeeping map, so it is never
    // destroyed here, only resized/cleared. Empty entry = image never used yet.
    std::vector<VkFence> imagesInFlight;

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

    // SetupImpl re-runs on every recompile; subscribe to WindowResizedMessage only once so
    // subscriptions don't accumulate (each accumulated sub fires an extra MarkNeedsRecompile,
    // turning one resize into a storm of recompiles).
    bool resizeSubscribed_ = false;

#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
    uint32_t compileCount_ = 0;
#endif
};

} // namespace Vixen::RenderGraph
