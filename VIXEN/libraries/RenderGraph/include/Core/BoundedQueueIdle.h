#pragma once

// R516 (gpuharden): a vkQueueWaitIdle replacement bounded under the platform watchdog.
//
// vkQueueWaitIdle/vkDeviceWaitIdle take no timeout, so a wedged GPU blocks the host forever. An empty
// submit with a fence on the same queue completes only after every earlier submission on that queue,
// which is the same guarantee, and the fence wait can carry a timeout (Core/GpuFault.h).

#include "Core/GpuFault.h"
#include "VulkanDevice.h"

#include <mutex>

namespace Vixen::RenderGraph {

class BoundedQueueIdle {
public:
    BoundedQueueIdle() = default;
    BoundedQueueIdle(const BoundedQueueIdle&) = delete;
    BoundedQueueIdle& operator=(const BoundedQueueIdle&) = delete;

    /// Wait until all prior submissions on `dev.queue` retire, or the bound expires.
    [[nodiscard]] GpuWaitResult Wait(Vixen::Vulkan::Resources::VulkanDevice& dev,
                                     uint64_t boundNs = GpuWaitBoundNs()) {
        if (fence_ == VK_NULL_HANDLE) {
            VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            const VkResult created = vkCreateFence(dev.device, &info, nullptr, &fence_);
            if (created != VK_SUCCESS) {
                fence_ = VK_NULL_HANDLE;
                return MakeWaitResult(created);
            }
        }
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        VkResult result;
        {
            std::lock_guard lock(dev.SubmitMutex(dev.queue));
            result = vkQueueSubmit(dev.queue, 1, &submit, fence_);
        }
        if (result != VK_SUCCESS) return MakeWaitResult(result);
        const GpuWaitResult waited = BoundedWaitFence(dev.device, fence_, boundNs);
        if (waited.result == VK_SUCCESS) {
            vkResetFences(dev.device, 1, &fence_);
        } else {
            // Still pending (timeout) or the device is gone: never reuse or destroy it under a live submit.
            fence_ = VK_NULL_HANDLE;
        }
        return waited;
    }

    void Destroy(VkDevice device) {
        if (fence_ != VK_NULL_HANDLE && device != VK_NULL_HANDLE) vkDestroyFence(device, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }

private:
    VkFence fence_ = VK_NULL_HANDLE;
};

}  // namespace Vixen::RenderGraph
