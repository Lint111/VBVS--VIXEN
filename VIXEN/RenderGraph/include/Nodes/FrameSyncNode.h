#pragma once

#include "Core/TypedNodeInstance.h"
#include "Nodes/FrameSyncNodeConfig.h"
#include "VulkanResources/VulkanDevice.h"
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief FrameSyncNodeType - Defines frame-in-flight synchronization node
 */
class FrameSyncNodeType : public TypedNodeType<FrameSyncNodeConfig> {
public:
    FrameSyncNodeType(const std::string& typeName = "FrameSync")
        : TypedNodeType<FrameSyncNodeConfig>(typeName) {}

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief FrameSyncNode - Manages frame-in-flight synchronization primitives
 *
 * Phase 0.2: Creates and manages MAX_FRAMES_IN_FLIGHT fences and semaphores
 * for CPU-GPU synchronization to prevent CPU from racing ahead of GPU.
 *
 * Inputs:
 *  - VULKAN_DEVICE (VulkanDevicePtr): Device to create sync primitives on
 *
 * Outputs:
 *  - CURRENT_FRAME_INDEX (uint32_t): Current frame-in-flight index (0 to MAX_FRAMES_IN_FLIGHT-1)
 *  - IN_FLIGHT_FENCE (VkFence): Fence for current frame (CPU-GPU sync)
 *  - IMAGE_AVAILABLE_SEMAPHORE (VkSemaphore): Semaphore for image acquisition (GPU-GPU)
 *  - RENDER_COMPLETE_SEMAPHORE (VkSemaphore): Semaphore for render completion (GPU-GPU)
 *
 * Usage pattern:
 *  1. Wait on IN_FLIGHT_FENCE before starting frame work
 *  2. Reset fence
 *  3. Use IMAGE_AVAILABLE_SEMAPHORE for vkAcquireNextImageKHR
 *  4. Use RENDER_COMPLETE_SEMAPHORE for vkQueuePresentKHR
 *  5. Signal fence at queue submit
 *  6. Advance CURRENT_FRAME_INDEX (wraps at MAX_FRAMES_IN_FLIGHT)
 */
class FrameSyncNode : public TypedNode<FrameSyncNodeConfig> {
public:
    FrameSyncNode(
        const std::string& instanceName,
        NodeType* nodeType
    );

    ~FrameSyncNode() override = default;

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(Context& ctx) override;
    void CompileImpl(Context& ctx) override;
    void ExecuteImpl(Context& ctx) override;
    void CleanupImpl() override;

private:
    // Per-flight synchronization data (for CPU-GPU sync)
    struct FrameSyncData {
        VkFence inFlightFence = VK_NULL_HANDLE;
    };

    std::vector<FrameSyncData> frameSyncData;    // Size = MAX_FRAMES_IN_FLIGHT
    std::vector<VkSemaphore> imageAvailableSemaphores;  // Size = swapchain image count
    std::vector<VkSemaphore> renderCompleteSemaphores;  // Size = swapchain image count
    std::vector<VkFence> presentFences;          // Size = swapchain image count (VK_KHR_swapchain_maintenance1)
    uint32_t currentFrameIndex = 0;              // Current frame-in-flight index
    bool isCreated = false;
};

} // namespace Vixen::RenderGraph