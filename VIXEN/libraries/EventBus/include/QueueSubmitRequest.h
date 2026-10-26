#pragma once

// R723: off-graph Vulkan work crosses the lock-free EventBus as an immutable request. The
// RenderGraph owns the only queue-drain/issue point; producers retain only this completion record
// and never wait for the queue from the publishing thread.

#include "Message.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

namespace Vixen::Vulkan::Resources {

class VulkanDevice;

struct QueueSubmitCompletion {
    // VK_NOT_READY means the owner has not issued the request. Once submitted, the owner stores
    // the queue's timeline value before publishing VK_SUCCESS. A failed request is terminal.
    std::atomic<int32_t> result{VK_NOT_READY};
    std::atomic<uint64_t> timelineValue{0};
    std::atomic<bool> retired{false};

    void PublishSubmitted(uint64_t value) noexcept {
        timelineValue.store(value, std::memory_order_relaxed);
        result.store(VK_SUCCESS, std::memory_order_release);
    }

    void PublishFailed(VkResult value) noexcept {
        result.store(static_cast<int32_t>(value), std::memory_order_release);
        retired.store(true, std::memory_order_release);
    }

    void PublishRetired() noexcept { retired.store(true, std::memory_order_release); }

    [[nodiscard]] VkResult Result() const noexcept {
        return static_cast<VkResult>(result.load(std::memory_order_acquire));
    }

    [[nodiscard]] uint64_t TimelineValue() const noexcept {
        return timelineValue.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool IsSubmitted() const noexcept { return Result() == VK_SUCCESS; }
    [[nodiscard]] bool IsRetired() const noexcept { return retired.load(std::memory_order_acquire); }
};

struct QueueSubmitRequest {
    VulkanDevice* device = nullptr;
    VkQueue queue = VK_NULL_HANDLE;
    std::vector<VkSemaphoreSubmitInfo> waits;
    std::vector<VkCommandBufferSubmitInfo> commandBuffers;
    std::vector<VkSemaphoreSubmitInfo> signals;
    std::vector<uint64_t> commandCostUpperBoundsNs;
    uint64_t submissionBudgetNs = 2'000'000;
    VkFence fence = VK_NULL_HANDLE;
    std::shared_ptr<QueueSubmitCompletion> completion = std::make_shared<QueueSubmitCompletion>();
    std::function<void()> onRetired;

    static QueueSubmitRequest FromSubmitInfo(VulkanDevice* device,
                                              VkQueue queue,
                                              const VkSubmitInfo& submit,
                                              VkFence fence = VK_NULL_HANDLE) {
        QueueSubmitRequest request;
        request.device = device;
        request.queue = queue;
        request.fence = fence;
        const auto* values = static_cast<const VkTimelineSemaphoreSubmitInfo*>(submit.pNext);
        if (values && values->sType != VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) values = nullptr;
        for (uint32_t i = 0; i < submit.waitSemaphoreCount; ++i) {
            VkSemaphoreSubmitInfo wait{};
            wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            wait.semaphore = submit.pWaitSemaphores[i];
            wait.value = values && i < values->waitSemaphoreValueCount ? values->pWaitSemaphoreValues[i] : 0;
            wait.stageMask = submit.pWaitDstStageMask
                ? static_cast<VkPipelineStageFlags2>(submit.pWaitDstStageMask[i])
                : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            request.waits.push_back(wait);
        }
        for (uint32_t i = 0; i < submit.commandBufferCount; ++i) {
            VkCommandBufferSubmitInfo command{};
            command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            command.commandBuffer = submit.pCommandBuffers[i];
            request.commandBuffers.push_back(command);
        }
        for (uint32_t i = 0; i < submit.signalSemaphoreCount; ++i) {
            VkSemaphoreSubmitInfo signal{};
            signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signal.semaphore = submit.pSignalSemaphores[i];
            signal.value = values && i < values->signalSemaphoreValueCount ? values->pSignalSemaphoreValues[i] : 0;
            signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            request.signals.push_back(signal);
        }
        return request;
    }

    static QueueSubmitRequest FromSubmitInfo2(VulkanDevice* device,
                                               VkQueue queue,
                                               const VkSubmitInfo2& submit,
                                               VkFence fence = VK_NULL_HANDLE) {
        QueueSubmitRequest request;
        request.device = device;
        request.queue = queue;
        request.fence = fence;
        if (submit.waitSemaphoreInfoCount)
            request.waits.assign(submit.pWaitSemaphoreInfos,
                                 submit.pWaitSemaphoreInfos + submit.waitSemaphoreInfoCount);
        if (submit.commandBufferInfoCount)
            request.commandBuffers.assign(submit.pCommandBufferInfos,
                                          submit.pCommandBufferInfos + submit.commandBufferInfoCount);
        if (submit.signalSemaphoreInfoCount)
            request.signals.assign(submit.pSignalSemaphoreInfos,
                                   submit.pSignalSemaphoreInfos + submit.signalSemaphoreInfoCount);
        return request;
    }
};

} // namespace Vixen::Vulkan::Resources

namespace Vixen::EventBus {

struct QueueSubmitRequestMessage final : BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();

    Vixen::Vulkan::Resources::QueueSubmitRequest request;

    explicit QueueSubmitRequestMessage(Vixen::Vulkan::Resources::QueueSubmitRequest&& value)
        : BaseEventMessage(EventCategory::FrameLifecycle, TYPE, 0)
        , request(std::move(value)) {}
};

} // namespace Vixen::EventBus
