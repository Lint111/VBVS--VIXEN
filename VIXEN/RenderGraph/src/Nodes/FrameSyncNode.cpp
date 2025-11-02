#include "Nodes/FrameSyncNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include <stdexcept>

namespace Vixen::RenderGraph {

// ====== FrameSyncNodeType ======

std::unique_ptr<NodeInstance> FrameSyncNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<FrameSyncNode>(
        instanceName,
        const_cast<NodeType*>(static_cast<const NodeType*>(this))
    );
}

// ====== FrameSyncNode ======

FrameSyncNode::FrameSyncNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<FrameSyncNodeConfig>(instanceName, nodeType)
{
}

void FrameSyncNode::SetupImpl(Context& ctx) {
    VulkanDevicePtr devicePtr = ctx.In(FrameSyncNodeConfig::VULKAN_DEVICE);

    if (devicePtr == nullptr) {
        std::string errorMsg = "FrameSyncNode: VulkanDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);
}

void FrameSyncNode::CompileImpl(Context& ctx) {
    // Phase 0.4: Separate concerns - fences for CPU-GPU, semaphores for GPU-GPU
    constexpr uint32_t flightCount = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;
    constexpr uint32_t imageCount = FrameSyncNodeConfig::MAX_SWAPCHAIN_IMAGES;

    NODE_LOG_INFO("Creating synchronization primitives: MAX_FRAMES_IN_FLIGHT="
                  + std::to_string(flightCount) + ", MAX_SWAPCHAIN_IMAGES=" + std::to_string(imageCount));

    // Create per-flight fences (CPU-GPU sync)
    frameSyncData.resize(flightCount);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled so first frame doesn't wait

    for (uint32_t i = 0; i < flightCount; i++) {
        if (vkCreateFence(device->device, &fenceInfo, nullptr, &frameSyncData[i].inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create in-flight fence for frame " + std::to_string(i));
        }
        NODE_LOG_INFO("Flight " + std::to_string(i) + ": fence=0x"
                      + std::to_string(reinterpret_cast<uint64_t>(frameSyncData[i].inFlightFence)));
    }

    // Phase 0.6: CORRECT per Vulkan validation guide
    // https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
    //
    // CRITICAL: renderComplete MUST be per-IMAGE, not per-FLIGHT!
    //
    // Reason: vkQueuePresentKHR holds the renderComplete semaphore until the presentation
    // engine finishes displaying the image. This can take longer than GPU rendering.
    // Fences only track GPU work completion, NOT presentation completion.
    //
    // - imageAvailable: per-FLIGHT (tracks frame pacing)
    // - renderComplete: per-IMAGE (tracks presentation engine usage per swapchain image)

    imageAvailableSemaphores.resize(flightCount);  // Per-FLIGHT for acquisition
    renderCompleteSemaphores.resize(imageCount);   // Per-IMAGE for presentation (CORRECT FIX)

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // Create per-FLIGHT acquisition semaphores
    for (uint32_t i = 0; i < flightCount; i++) {
        if (vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create imageAvailable semaphore for flight " + std::to_string(i));
        }
    }

    // Create per-IMAGE render complete semaphores (one per swapchain image)
    for (uint32_t i = 0; i < imageCount; i++) {
        if (vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &renderCompleteSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create renderComplete semaphore for image " + std::to_string(i));
        }
    }

    // Phase 0.7: Create per-IMAGE present fences (VK_KHR_swapchain_maintenance1)
    // These track when the presentation engine has finished with each swapchain image
    presentFences.resize(imageCount);

    VkFenceCreateInfo presentFenceInfo{};
    presentFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    presentFenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled (no wait on first use)

    for (uint32_t i = 0; i < imageCount; i++) {
        if (vkCreateFence(device->device, &presentFenceInfo, nullptr, &presentFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create present fence for image " + std::to_string(i));
        }
    }

    isCreated = true;
    currentFrameIndex = 0;

    // Set initial outputs (flight 0)
    ctx.Out(FrameSyncNodeConfig::CURRENT_FRAME_INDEX, currentFrameIndex);
    ctx.Out(FrameSyncNodeConfig::IN_FLIGHT_FENCE, frameSyncData[currentFrameIndex].inFlightFence);

    // Output semaphore arrays (imageAvailable=per-FLIGHT, renderComplete=per-IMAGE)
    ctx.Out(FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, imageAvailableSemaphores.data());
    ctx.Out(FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, renderCompleteSemaphores.data());
    ctx.Out(FrameSyncNodeConfig::PRESENT_FENCES_ARRAY, &presentFences);

    NODE_LOG_INFO("Synchronization primitives created successfully");
    NODE_LOG_INFO("Created " + std::to_string(imageAvailableSemaphores.size()) + " imageAvailable semaphores (per-flight)");
    NODE_LOG_INFO("Created " + std::to_string(renderCompleteSemaphores.size()) + " renderComplete semaphores (per-image)");
    NODE_LOG_INFO("Created " + std::to_string(presentFences.size()) + " present fences (per-image, VK_KHR_swapchain_maintenance1)");
}

void FrameSyncNode::ExecuteImpl(Context& ctx) {
    // Advance frame index (ring buffer for CPU-GPU sync)
    currentFrameIndex = (currentFrameIndex + 1) % FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

    // Phase 0.4: CRITICAL - Wait on the current flight's fence BEFORE acquiring the next image
    // This ensures the previous frame using this flight's resources has completed
    // Without this wait, we could reuse semaphores that are still in use by the presentation engine
    VkFence currentFence = frameSyncData[currentFrameIndex].inFlightFence;
    vkWaitForFences(device->device, 1, &currentFence, VK_TRUE, UINT64_MAX);

    // Note: Fence will be reset by GeometryRenderNode before submission

    // Update outputs with current frame's fence
    ctx.Out(FrameSyncNodeConfig::CURRENT_FRAME_INDEX, currentFrameIndex);
    ctx.Out(FrameSyncNodeConfig::IN_FLIGHT_FENCE, currentFence);

    // Semaphore arrays remain constant (no need to update every frame)
    // SwapChainNode will index into these arrays using the current frame index
}

void FrameSyncNode::CleanupImpl() {
    if (isCreated && device != nullptr && device->device != VK_NULL_HANDLE) {
        NODE_LOG_INFO("Destroying frame synchronization primitives");

        // Destroy per-flight fences
        for (auto& sync : frameSyncData) {
            if (sync.inFlightFence != VK_NULL_HANDLE) {
                vkDestroyFence(device->device, sync.inFlightFence, nullptr);
                sync.inFlightFence = VK_NULL_HANDLE;
            }
        }

        // Destroy per-image semaphores
        for (auto& semaphore : imageAvailableSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device->device, semaphore, nullptr);
                semaphore = VK_NULL_HANDLE;
            }
        }

        for (auto& semaphore : renderCompleteSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device->device, semaphore, nullptr);
                semaphore = VK_NULL_HANDLE;
            }
        }

        // Destroy per-image present fences
        for (auto& fence : presentFences) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(device->device, fence, nullptr);
                fence = VK_NULL_HANDLE;
            }
        }

        frameSyncData.clear();
        imageAvailableSemaphores.clear();
        renderCompleteSemaphores.clear();
        presentFences.clear();
        currentFrameIndex = 0;
        isCreated = false;

        NODE_LOG_INFO("Frame synchronization primitives destroyed");
    }
}

} // namespace Vixen::RenderGraph