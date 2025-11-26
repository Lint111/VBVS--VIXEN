#include "Nodes/FrameSyncNode.h"
#include "Core/RenderGraph.h"
#include "Core/VulkanLimits.h"
#include "Core/ResourceManagerBase.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include <stdexcept>

namespace Vixen::RenderGraph {

std::unique_ptr<NodeInstance> FrameSyncNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<FrameSyncNode>(
        instanceName,
        const_cast<NodeType*>(static_cast<const NodeType*>(this))
    );
}

FrameSyncNode::FrameSyncNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<FrameSyncNodeConfig>(instanceName, nodeType)
{
}

void FrameSyncNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("FrameSyncNode: Setup (graph-scope initialization)");
}

void FrameSyncNode::CompileImpl(TypedCompileContext& ctx) {
    VulkanDevice* devicePtr = ctx.In(FrameSyncNodeConfig::VULKAN_DEVICE);
    if (devicePtr == nullptr) {
        std::string errorMsg = "FrameSyncNode: VulkanDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    SetDevice(devicePtr);
    flightCount_ = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    imageCount_ = static_cast<uint32_t>(MAX_SWAPCHAIN_IMAGES);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < flightCount_; i++) {
        if (vkCreateFence(device->device, &fenceInfo, nullptr, &frameSyncData_[i].inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create in-flight fence");
        }
    }

    // Phase H: Use RequestAllocation API for automatic tracking
    auto* rm = GetResourceManager();

    if (rm) {
        imageAvailableSemaphores_ = rm->RequestAllocation<VkSemaphore, MAX_FRAMES_IN_FLIGHT>(
            AllocationConfig()
                .WithStrategy(ResourceAllocStrategy::Stack)
                .WithLifetime(ResourceLifetime::GraphLocal)
                .WithName("imageAvailableSemaphores")
                .WithOwner(GetInstanceId())
                .WithHeapFallback(true)
        );
        if (imageAvailableSemaphores_.IsError()) {
            throw std::runtime_error("Failed to allocate imageAvailableSemaphores: " +
                std::string(imageAvailableSemaphores_.GetErrorString()));
        }
    } else {
        imageAvailableSemaphores_ = ImageAvailableSemaphoreAllocation(
            StackAllocationResult<VkSemaphore, MAX_FRAMES_IN_FLIGHT>{});
    }

    if (rm) {
        renderCompleteSemaphores_ = rm->RequestAllocation<VkSemaphore, MAX_SWAPCHAIN_IMAGES>(
            AllocationConfig()
                .WithStrategy(ResourceAllocStrategy::Stack)
                .WithLifetime(ResourceLifetime::GraphLocal)
                .WithName("renderCompleteSemaphores")
                .WithOwner(GetInstanceId())
                .WithHeapFallback(true)
        );
        if (renderCompleteSemaphores_.IsError()) {
            throw std::runtime_error("Failed to allocate renderCompleteSemaphores: " +
                std::string(renderCompleteSemaphores_.GetErrorString()));
        }
    } else {
        renderCompleteSemaphores_ = RenderCompleteSemaphoreAllocation(
            StackAllocationResult<VkSemaphore, MAX_SWAPCHAIN_IMAGES>{});
    }

    if (rm) {
        presentFences_ = rm->RequestAllocation<VkFence, MAX_SWAPCHAIN_IMAGES>(
            AllocationConfig()
                .WithStrategy(ResourceAllocStrategy::Stack)
                .WithLifetime(ResourceLifetime::GraphLocal)
                .WithName("presentFences")
                .WithOwner(GetInstanceId())
                .WithHeapFallback(true)
        );
        if (presentFences_.IsError()) {
            throw std::runtime_error("Failed to allocate presentFences: " +
                std::string(presentFences_.GetErrorString()));
        }
    } else {
        presentFences_ = PresentFenceAllocation(
            StackAllocationResult<VkFence, MAX_SWAPCHAIN_IMAGES>{});
    }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < flightCount_; i++) {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create imageAvailable semaphore");
        }
        imageAvailableSemaphores_.Add(semaphore);
    }

    for (uint32_t i = 0; i < imageCount_; i++) {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create renderComplete semaphore");
        }
        renderCompleteSemaphores_.Add(semaphore);
    }

    VkFenceCreateInfo presentFenceInfo{};
    presentFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    presentFenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < imageCount_; i++) {
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(device->device, &presentFenceInfo, nullptr, &fence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create present fence");
        }
        presentFences_.Add(fence);
    }

    isCreated_ = true;
    currentFrameIndex_ = 0;

    if (!imageAvailableSemaphores_.IsStack() || !renderCompleteSemaphores_.IsStack() || !presentFences_.IsStack()) {
        throw std::runtime_error("FrameSyncNode: Heap fallback not supported for output slot types");
    }

    ctx.Out(FrameSyncNodeConfig::CURRENT_FRAME_INDEX, currentFrameIndex_);
    ctx.Out(FrameSyncNodeConfig::IN_FLIGHT_FENCE, frameSyncData_[currentFrameIndex_].inFlightFence);
    ctx.Out(FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, imageAvailableSemaphores_.GetStack().data);
    ctx.Out(FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, renderCompleteSemaphores_.GetStack().data);
    ctx.Out(FrameSyncNodeConfig::PRESENT_FENCES_ARRAY, presentFences_.GetStack().data);
}

void FrameSyncNode::ExecuteImpl(TypedExecuteContext& ctx) {
    currentFrameIndex_ = (currentFrameIndex_ + 1) % flightCount_;
    VkFence currentFence = frameSyncData_[currentFrameIndex_].inFlightFence;
    vkWaitForFences(device->device, 1, &currentFence, VK_TRUE, UINT64_MAX);

    ctx.Out(FrameSyncNodeConfig::CURRENT_FRAME_INDEX, currentFrameIndex_);
    ctx.Out(FrameSyncNodeConfig::IN_FLIGHT_FENCE, currentFence);
    ctx.Out(FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, imageAvailableSemaphores_.GetStack().data);
    ctx.Out(FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, renderCompleteSemaphores_.GetStack().data);
    ctx.Out(FrameSyncNodeConfig::PRESENT_FENCES_ARRAY, presentFences_.GetStack().data);
}

void FrameSyncNode::CleanupImpl(TypedCleanupContext& ctx) {
    if (isCreated_ && device != nullptr && device->device != VK_NULL_HANDLE) {
        for (size_t i = 0; i < flightCount_; i++) {
            if (frameSyncData_[i].inFlightFence != VK_NULL_HANDLE) {
                vkDestroyFence(device->device, frameSyncData_[i].inFlightFence, nullptr);
                frameSyncData_[i].inFlightFence = VK_NULL_HANDLE;
            }
        }

        if (imageAvailableSemaphores_.IsSuccess()) {
            for (auto& semaphore : imageAvailableSemaphores_) {
                if (semaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device->device, semaphore, nullptr);
                }
            }
            imageAvailableSemaphores_.Clear();
        }

        if (renderCompleteSemaphores_.IsSuccess()) {
            for (auto& semaphore : renderCompleteSemaphores_) {
                if (semaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device->device, semaphore, nullptr);
                }
            }
            renderCompleteSemaphores_.Clear();
        }

        if (presentFences_.IsSuccess()) {
            for (auto& fence : presentFences_) {
                if (fence != VK_NULL_HANDLE) {
                    vkDestroyFence(device->device, fence, nullptr);
                }
            }
            presentFences_.Clear();
        }

        currentFrameIndex_ = 0;
        flightCount_ = 0;
        imageCount_ = 0;
        isCreated_ = false;
    }
}

} // namespace Vixen::RenderGraph
