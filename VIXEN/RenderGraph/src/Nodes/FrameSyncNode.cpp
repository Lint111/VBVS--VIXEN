#include "Nodes/FrameSyncNode.h"
#include "Core/RenderGraph.h"
#include "Core/ResourceHash.h"
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

void FrameSyncNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("FrameSyncNode: Setup (graph-scope initialization)");
}

void FrameSyncNode::CompileImpl(TypedCompileContext& ctx) {
    // Access device input (compile-time dependency)
    VulkanDevicePtr devicePtr = ctx.In(FrameSyncNodeConfig::VULKAN_DEVICE);

    if (devicePtr == nullptr) {
        std::string errorMsg = "FrameSyncNode: VulkanDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);
    // Phase 0.4: Separate concerns - fences for CPU-GPU, semaphores for GPU-GPU
    constexpr uint32_t flightCount = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;
    constexpr uint32_t imageCount = FrameSyncNodeConfig::MAX_SWAPCHAIN_IMAGES;

    // Phase H: Validate counts against array bounds
    if (flightCount > MAX_FRAMES_IN_FLIGHT) {
        throw std::runtime_error("FrameSyncNode: flightCount (" + std::to_string(flightCount) +
                                 ") exceeds MAX_FRAMES_IN_FLIGHT (" + std::to_string(MAX_FRAMES_IN_FLIGHT) + ")");
    }
    if (imageCount > MAX_SWAPCHAIN_IMAGES) {
        throw std::runtime_error("FrameSyncNode: imageCount (" + std::to_string(imageCount) +
                                 ") exceeds MAX_SWAPCHAIN_IMAGES (" + std::to_string(MAX_SWAPCHAIN_IMAGES) + ")");
    }

    flightCount_ = flightCount;
    imageCount_ = imageCount;

    NODE_LOG_INFO("Creating synchronization primitives: MAX_FRAMES_IN_FLIGHT="
                  + std::to_string(flightCount) + ", MAX_SWAPCHAIN_IMAGES=" + std::to_string(imageCount));

    // Create per-flight fences (CPU-GPU sync) - no resize, use array directly

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

    // Phase H: Request URM-managed stack arrays using type-safe persistent hashes

    // Request imageAvailable semaphores array (per-FLIGHT)
    uint64_t imageAvailHash = ComputeResourceHashFor(GetInstanceId(), 0, imageAvailableSemaphores_);
    auto imageAvailResult = RequestStackResource<VkSemaphore, MAX_FRAMES_IN_FLIGHT>(imageAvailHash);
    if (!imageAvailResult) {
        throw std::runtime_error("FrameSyncNode: Failed to request imageAvailable semaphores array");
    }
    imageAvailableSemaphores_ = std::move(imageAvailResult.value());

    // Request renderComplete semaphores array (per-IMAGE)
    uint64_t renderCompleteHash = ComputeResourceHashFor(GetInstanceId(), 0, renderCompleteSemaphores_);
    auto renderCompleteResult = RequestStackResource<VkSemaphore, MAX_SWAPCHAIN_IMAGES>(renderCompleteHash);
    if (!renderCompleteResult) {
        throw std::runtime_error("FrameSyncNode: Failed to request renderComplete semaphores array");
    }
    renderCompleteSemaphores_ = std::move(renderCompleteResult.value());

    // Request presentFences array (per-IMAGE)
    uint64_t presentFencesHash = ComputeResourceHashFor(GetInstanceId(), 0, presentFences_);
    auto presentFencesResult = RequestStackResource<VkFence, MAX_SWAPCHAIN_IMAGES>(presentFencesHash);
    if (!presentFencesResult) {
        throw std::runtime_error("FrameSyncNode: Failed to request presentFences array");
    }
    presentFences_ = std::move(presentFencesResult.value());

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // Create per-FLIGHT acquisition semaphores in URM-managed array
    for (uint32_t i = 0; i < flightCount; i++) {
        if (vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &(*imageAvailableSemaphores_)[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create imageAvailable semaphore for flight " + std::to_string(i));
        }
    }

    // Create per-IMAGE render complete semaphores in URM-managed array
    for (uint32_t i = 0; i < imageCount; i++) {
        if (vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &(*renderCompleteSemaphores_)[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create renderComplete semaphore for image " + std::to_string(i));
        }
    }

    // Phase 0.7: Create per-IMAGE present fences (VK_KHR_swapchain_maintenance1) in URM-managed array
    // These track when the presentation engine has finished with each swapchain image
    VkFenceCreateInfo presentFenceInfo{};
    presentFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    presentFenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled (no wait on first use)

    for (uint32_t i = 0; i < imageCount; i++) {
        if (vkCreateFence(device->device, &presentFenceInfo, nullptr, &(*presentFences_)[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create present fence for image " + std::to_string(i));
        }
    }

    isCreated = true;
    currentFrameIndex = 0;

    // Set initial outputs (flight 0)
    ctx.Out(FrameSyncNodeConfig::CURRENT_FRAME_INDEX, currentFrameIndex);
    ctx.Out(FrameSyncNodeConfig::IN_FLIGHT_FENCE, frameSyncData[currentFrameIndex].inFlightFence);

    // Phase H: URM-managed arrays automatically tracked, just output from handles
    // Convert to vectors for interface compatibility
    std::vector<VkSemaphore> imageAvailableVec(imageAvailableSemaphores_->begin(), imageAvailableSemaphores_->begin() + flightCount_);
    std::vector<VkSemaphore> renderCompleteVec(renderCompleteSemaphores_->begin(), renderCompleteSemaphores_->begin() + imageCount_);
    std::vector<VkFence> presentFencesVec(presentFences_->begin(), presentFences_->begin() + imageCount_);

    ctx.Out(FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, imageAvailableVec);
    ctx.Out(FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, renderCompleteVec);
    ctx.Out(FrameSyncNodeConfig::PRESENT_FENCES_ARRAY, presentFencesVec);

    // Log allocation location for profiling
    NODE_LOG_INFO("Synchronization primitives created successfully (URM-managed)");
    NODE_LOG_INFO("  imageAvailable: " + std::to_string(flightCount_) + " semaphores (per-flight) - " +
                  (imageAvailableSemaphores_->isStack() ? "STACK" : "HEAP"));
    NODE_LOG_INFO("  renderComplete: " + std::to_string(imageCount_) + " semaphores (per-image) - " +
                  (renderCompleteSemaphores_->isStack() ? "STACK" : "HEAP"));
    NODE_LOG_INFO("  presentFences: " + std::to_string(imageCount_) + " fences (per-image) - " +
                  (presentFences_->isStack() ? "STACK" : "HEAP"));
}

void FrameSyncNode::ExecuteImpl(TypedExecuteContext& ctx) {
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

void FrameSyncNode::CleanupImpl(TypedCleanupContext& ctx) {
    if (isCreated && device != nullptr && device->device != VK_NULL_HANDLE) {
        NODE_LOG_INFO("Destroying frame synchronization primitives");

        // Phase H: Destroy using counts instead of range-based for
        // Destroy per-flight fences
        for (uint32_t i = 0; i < flightCount_; ++i) {
            if (frameSyncData[i].inFlightFence != VK_NULL_HANDLE) {
                vkDestroyFence(device->device, frameSyncData[i].inFlightFence, nullptr);
                frameSyncData[i].inFlightFence = VK_NULL_HANDLE;
            }
        }

        // Destroy per-flight semaphores (from URM-managed array)
        if (imageAvailableSemaphores_.has_value()) {
            for (uint32_t i = 0; i < flightCount_; ++i) {
                if ((*imageAvailableSemaphores_)[i] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device->device, (*imageAvailableSemaphores_)[i], nullptr);
                    (*imageAvailableSemaphores_)[i] = VK_NULL_HANDLE;
                }
            }
            imageAvailableSemaphores_.reset();  // Release handle, URM reclaims memory
        }

        // Destroy per-image semaphores (from URM-managed array)
        if (renderCompleteSemaphores_.has_value()) {
            for (uint32_t i = 0; i < imageCount_; ++i) {
                if ((*renderCompleteSemaphores_)[i] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device->device, (*renderCompleteSemaphores_)[i], nullptr);
                    (*renderCompleteSemaphores_)[i] = VK_NULL_HANDLE;
                }
            }
            renderCompleteSemaphores_.reset();  // Release handle, URM reclaims memory
        }

        // Destroy per-image present fences (from URM-managed array)
        if (presentFences_.has_value()) {
            for (uint32_t i = 0; i < imageCount_; ++i) {
                if ((*presentFences_)[i] != VK_NULL_HANDLE) {
                    vkDestroyFence(device->device, (*presentFences_)[i], nullptr);
                    (*presentFences_)[i] = VK_NULL_HANDLE;
                }
            }
            presentFences_.reset();  // Release handle, URM reclaims memory
        }

        // Phase H: Reset counts (URM handles released above)
        flightCount_ = 0;
        imageCount_ = 0;
        currentFrameIndex = 0;
        isCreated = false;

        NODE_LOG_INFO("Frame synchronization primitives destroyed");
    }
}

} // namespace Vixen::RenderGraph