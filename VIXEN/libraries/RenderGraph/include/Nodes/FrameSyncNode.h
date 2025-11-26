#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/VulkanLimits.h"
#include "Core/ResourceManagerBase.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "VulkanDevice.h"
#include "BoundedArray.h"
#include <array>

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
 * Phase H: Uses RequestAllocation API for automatic resource tracking.
 * Creates and manages MAX_FRAMES_IN_FLIGHT fences and semaphores
 * for CPU-GPU synchronization to prevent CPU from racing ahead of GPU.
 *
 * Inputs:
 *  - VULKAN_DEVICE (VulkanDevice*): Device to create sync primitives on
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
    // Type aliases for allocation results
    using ImageAvailableSemaphoreAllocation = AllocationResult<VkSemaphore, MAX_FRAMES_IN_FLIGHT>;
    using RenderCompleteSemaphoreAllocation = AllocationResult<VkSemaphore, MAX_SWAPCHAIN_IMAGES>;
    using PresentFenceAllocation = AllocationResult<VkFence, MAX_SWAPCHAIN_IMAGES>;

    FrameSyncNode(
        const std::string& instanceName,
        NodeType* nodeType
    );

    ~FrameSyncNode() override = default;

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Per-flight synchronization data (for CPU-GPU sync)
    struct FrameSyncData {
        VkFence inFlightFence = VK_NULL_HANDLE;
    };

    // Phase H: Stack-allocated array for in-flight fences (fixed size)
    std::array<FrameSyncData, MAX_FRAMES_IN_FLIGHT> frameSyncData_{};

    // Phase H: Resource allocations with automatic tracking
    ImageAvailableSemaphoreAllocation imageAvailableSemaphores_;
    RenderCompleteSemaphoreAllocation renderCompleteSemaphores_;
    PresentFenceAllocation presentFences_;

    uint32_t currentFrameIndex_ = 0;
    uint32_t flightCount_ = 0;
    uint32_t imageCount_ = 0;
    bool isCreated_ = false;
};

} // namespace Vixen::RenderGraph
