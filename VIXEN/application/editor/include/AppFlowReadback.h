#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Vixen::Vulkan::Resources {
class VulkanDevice;
struct IRenderTarget;
}

// Bounded asynchronous GPU->host transfer ring for AppFlow visual readback. Enqueue records a
// same-queue copy and returns immediately; Poll maps pixels only after the slot fence signals.
class AppFlowReadbackRing {
public:
    struct Result {
        std::string key;
        uint64_t frame = 0;
        uint32_t x = 0, y = 0, width = 0, height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint64_t enqueueCpuUs = 0;
        uint64_t completionCpuUs = 0;
        uint64_t transferBytes = 0;
        bool fullImageTransferFallback = false;
        std::vector<uint8_t> pixels;
        std::string error;
    };

    AppFlowReadbackRing();
    ~AppFlowReadbackRing();
    AppFlowReadbackRing(const AppFlowReadbackRing&) = delete;
    AppFlowReadbackRing& operator=(const AppFlowReadbackRing&) = delete;

    bool Enqueue(std::string key, uint64_t frame,
                 Vixen::Vulkan::Resources::VulkanDevice* device, VkQueue queue,
                 uint32_t queueFamily, Vixen::Vulkan::Resources::IRenderTarget* target,
                 uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                 std::string& error);
    std::vector<Result> Poll();

private:
    struct Slot;
    void DestroySlot(Slot& slot);
    bool EnsureSlot(Slot& slot, VkDevice logicalDevice, uint32_t queueFamily,
                    Vixen::Vulkan::Resources::VulkanDevice* device, VkDeviceSize bytes,
                    std::string& error);

    std::vector<Slot> slots_;
    size_t nextSlot_ = 0;
};
