#pragma once
#include "MessageBus.h"
#include "QueueSubmitRequest.h"
#include "VulkanDevice.h"
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

// A VulkanDevice shell for tests that create their own VkDevice: it carries everything production
// nodes read from the device (queue, graphics queue index, physical-device properties, memory
// properties and queue family properties). Set only `device` and a node's queue readback breaks.
// The caller must null `device` before destroying the shell, since the shell does not own it.
inline std::unique_ptr<Vixen::Vulkan::Resources::VulkanDevice> MakeTestDeviceShell(
    VkPhysicalDevice& physicalDevice, VkDevice device, VkQueue queue, uint32_t graphicsQueueIndex) {
    auto shell = std::make_unique<Vixen::Vulkan::Resources::VulkanDevice>(&physicalDevice);
    shell->device = device;
    shell->queue = queue;
    shell->graphicsQueueIndex = graphicsQueueIndex;
    vkGetPhysicalDeviceProperties(physicalDevice, &shell->gpuProperties);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &shell->gpuMemoryProperties);
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    shell->queueFamilyProperties.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             shell->queueFamilyProperties.data());
    return shell;
}

// Standalone Vulkan tests do not have a RenderGraph frame owner, but production producers still
// publish through VulkanDevice::PublishSubmitRequest. This small test-only owner gives those
// fixtures the same explicit ownership boundary: it drains the message bus, submits on the queue,
// waits for the test request, and retires its completion. It must never be used by product code.
class TestQueueOwner {
public:
    TestQueueOwner(VkDevice device, VkQueue queue) : device_(device), queue_(queue) {}

    void Attach(Vixen::Vulkan::Resources::VulkanDevice& shell) {
        shell.SetMessageBus(&bus_);
        bus_.Subscribe(
            Vixen::EventBus::QueueSubmitRequestMessage::TYPE,
            [this](const Vixen::EventBus::BaseEventMessage& base) {
                const auto& message = static_cast<const Vixen::EventBus::QueueSubmitRequestMessage&>(base);
                pending_.push_back(message.request);
                return true;
            });
    }

    void Drain() {
        bus_.ProcessMessages();
        for (auto& request : pending_) {
            (void)SubmitSynchronously(std::move(request));
        }
        pending_.clear();
    }

    std::shared_ptr<Vixen::Vulkan::Resources::QueueSubmitCompletion> SubmitSynchronously(
        Vixen::Vulkan::Resources::QueueSubmitRequest request) {
        using namespace Vixen::Vulkan::Resources;
        if (!request.completion) request.completion = std::make_shared<QueueSubmitCompletion>();
        const auto completion = request.completion;

        std::vector<VkSemaphore> waits;
        std::vector<VkPipelineStageFlags> waitStages;
        waits.reserve(request.waits.size());
        waitStages.reserve(request.waits.size());
        for (const auto& wait : request.waits) {
            waits.push_back(wait.semaphore);
            waitStages.push_back(static_cast<VkPipelineStageFlags>(wait.stageMask));
        }

        std::vector<VkCommandBuffer> commandBuffers;
        commandBuffers.reserve(request.commandBuffers.size());
        for (const auto& command : request.commandBuffers) commandBuffers.push_back(command.commandBuffer);

        std::vector<VkSemaphore> signals;
        signals.reserve(request.signals.size());
        for (const auto& signal : request.signals) signals.push_back(signal.semaphore);

        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = static_cast<uint32_t>(waits.size());
        submit.pWaitSemaphores = waits.data();
        submit.pWaitDstStageMask = waitStages.data();
        submit.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
        submit.pCommandBuffers = commandBuffers.data();
        submit.signalSemaphoreCount = static_cast<uint32_t>(signals.size());
        submit.pSignalSemaphores = signals.data();

        VkResult result = vkQueueSubmit(queue_, 1, &submit, request.fence);
        if (result == VK_SUCCESS) {
            completion->PublishSubmitted(0);
            result = vkQueueWaitIdle(queue_);
        }
        if (result == VK_SUCCESS) {
            if (request.onRetired) request.onRetired();
            completion->PublishRetired();
        } else {
            completion->PublishFailed(result);
            if (request.onRetired) request.onRetired();
        }
        return completion;
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    Vixen::EventBus::MessageBus bus_;
    std::vector<Vixen::Vulkan::Resources::QueueSubmitRequest> pending_;
};
