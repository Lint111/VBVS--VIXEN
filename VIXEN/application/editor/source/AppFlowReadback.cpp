#include "AppFlowReadback.h"

#include "VulkanDevice.h"
#include "IRenderTarget.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>

namespace {
using Clock = std::chrono::steady_clock;

uint64_t ElapsedUs(Clock::time_point start) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - start).count());
}

bool IsFourByteColor(VkFormat format) {
    return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ||
           format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}
} // namespace

struct AppFlowReadbackRing::Slot {
    VkDevice logicalDevice = VK_NULL_HANDLE;
    uint32_t queueFamily = UINT32_MAX;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize allocationSize = 0;
    VkDeviceSize capacity = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool inFlight = false;

    std::string key;
    uint64_t frame = 0;
    uint32_t x = 0, y = 0, width = 0, height = 0;
    uint32_t transferX = 0, transferY = 0, transferWidth = 0, transferHeight = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint64_t enqueueCpuUs = 0;
    uint64_t transferBytes = 0;
    bool fullImageTransferFallback = false;
};

AppFlowReadbackRing::AppFlowReadbackRing() : slots_(3) {}

AppFlowReadbackRing::~AppFlowReadbackRing() {
    for (auto& slot : slots_) DestroySlot(slot);
}

void AppFlowReadbackRing::DestroySlot(Slot& slot) {
    if (slot.logicalDevice == VK_NULL_HANDLE) return;
    if (slot.inFlight && slot.fence != VK_NULL_HANDLE) {
        // Shutdown only: the frame loop never waits for a readback fence.
        vkWaitForFences(slot.logicalDevice, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    }
    if (slot.fence != VK_NULL_HANDLE) vkDestroyFence(slot.logicalDevice, slot.fence, nullptr);
    if (slot.commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(slot.logicalDevice, slot.commandPool, nullptr);
    if (slot.buffer != VK_NULL_HANDLE) vkDestroyBuffer(slot.logicalDevice, slot.buffer, nullptr);
    if (slot.memory != VK_NULL_HANDLE) vkFreeMemory(slot.logicalDevice, slot.memory, nullptr);
    slot = Slot{};
}

bool AppFlowReadbackRing::EnsureSlot(Slot& slot, VkDevice logicalDevice, uint32_t queueFamily,
                                     Vixen::Vulkan::Resources::VulkanDevice* device,
                                     VkDeviceSize bytes, std::string& error) {
    if (slot.logicalDevice == logicalDevice && slot.queueFamily == queueFamily &&
        slot.capacity >= bytes && slot.buffer != VK_NULL_HANDLE) return true;
    DestroySlot(slot);

    slot.logicalDevice = logicalDevice;
    slot.queueFamily = queueFamily;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &slot.buffer) != VK_SUCCESS) {
        error = "vkCreateBuffer failed for AppFlow staging slot";
        DestroySlot(slot);
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(logicalDevice, slot.buffer, &requirements);
    uint32_t memoryType = UINT32_MAX;
    constexpr VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < device->gpuMemoryProperties.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (device->gpuMemoryProperties.memoryTypes[i].propertyFlags & required) == required) {
            memoryType = i;
            break;
        }
    }
    if (memoryType == UINT32_MAX) {
        error = "no host-visible/coherent memory type for AppFlow staging slot";
        DestroySlot(slot);
        return false;
    }
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(logicalDevice, &allocation, nullptr, &slot.memory) != VK_SUCCESS ||
        vkBindBufferMemory(logicalDevice, slot.buffer, slot.memory, 0) != VK_SUCCESS) {
        error = "staging buffer memory allocation/bind failed";
        DestroySlot(slot);
        return false;
    }
    slot.allocationSize = requirements.size;
    slot.capacity = bytes;

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &slot.commandPool) != VK_SUCCESS) {
        error = "vkCreateCommandPool failed for AppFlow staging slot";
        DestroySlot(slot);
        return false;
    }
    VkCommandBufferAllocateInfo commandAllocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandAllocation.commandPool = slot.commandPool;
    commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocation.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(logicalDevice, &commandAllocation, &slot.command) != VK_SUCCESS) {
        error = "vkAllocateCommandBuffers failed for AppFlow staging slot";
        DestroySlot(slot);
        return false;
    }
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(logicalDevice, &fenceInfo, nullptr, &slot.fence) != VK_SUCCESS) {
        error = "vkCreateFence failed for AppFlow staging slot";
        DestroySlot(slot);
        return false;
    }
    return true;
}

bool AppFlowReadbackRing::Enqueue(std::string key, uint64_t frame,
                                  Vixen::Vulkan::Resources::VulkanDevice* device, VkQueue queue,
                                  uint32_t queueFamily, Vixen::Vulkan::Resources::IRenderTarget* target,
                                  uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                  std::string& error) {
    const auto started = Clock::now();
    if (!device || device->device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE || !target) {
        error = "AppFlow readback source/device is unavailable";
        return false;
    }
    const VkExtent2D extent = target->GetExtent();
    const VkFormat format = target->GetFormat();
    if (!IsFourByteColor(format)) {
        error = "AppFlow readback currently supports native RGBA8/BGRA8 targets";
        return false;
    }
    if (width == 0 || height == 0 || x >= extent.width || y >= extent.height ||
        width > extent.width - x || height > extent.height - y) {
        error = "readback rectangle is outside the native render extent";
        return false;
    }
    const uint32_t imageIndex = target->GetCurrentIndex();
    const VkImage image = target->GetImage(imageIndex);
    const VkImageLayout oldLayout = target->GetImageLayout(imageIndex);
    if (image == VK_NULL_HANDLE || oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        error = "render target has no completed image to read";
        return false;
    }

    Slot* selected = nullptr;
    for (size_t offset = 0; offset < slots_.size(); ++offset) {
        Slot& candidate = slots_[(nextSlot_ + offset) % slots_.size()];
        if (!candidate.inFlight) {
            selected = &candidate;
            nextSlot_ = (nextSlot_ + offset + 1) % slots_.size();
            break;
        }
    }
    if (!selected) {
        error = "all asynchronous readback ring slots are still in flight";
        return false;
    }

    uint32_t copyX = x, copyY = y, copyWidth = width, copyHeight = height;
    const bool fullImageTransferFallback = device->RequiresFullImageTransfers() &&
        (x != 0 || y != 0 || width != extent.width || height != extent.height);
    if (fullImageTransferFallback) {
        // Some software queues expose zero transfer granularity and only allow full-image copies.
        copyX = 0;
        copyY = 0;
        copyWidth = extent.width;
        copyHeight = extent.height;
    }
    if (copyHeight != 0 && static_cast<VkDeviceSize>(copyWidth) >
        std::numeric_limits<VkDeviceSize>::max() / copyHeight / 4) {
        error = "AppFlow staging copy size overflow";
        return false;
    }
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(copyWidth) * copyHeight * 4;
    if (!EnsureSlot(*selected, device->device, queueFamily, device, byteCount, error)) return false;
    if (vkResetCommandPool(device->device, selected->commandPool, 0) != VK_SUCCESS ||
        vkResetFences(device->device, 1, &selected->fence) != VK_SUCCESS) {
        error = "could not reset AppFlow staging slot";
        return false;
    }

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(selected->command, &begin) != VK_SUCCESS) {
        error = "vkBeginCommandBuffer failed for AppFlow staging copy";
        return false;
    }
    if (oldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        VkImageMemoryBarrier toCopy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toCopy.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 :
            (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
        toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toCopy.oldLayout = oldLayout;
        toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toCopy.image = image;
        toCopy.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        const VkPipelineStageFlags sourceStage = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        vkCmdPipelineBarrier(selected->command, sourceStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toCopy);
    }
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageOffset = {static_cast<int32_t>(copyX), static_cast<int32_t>(copyY), 0};
    copy.imageExtent = {copyWidth, copyHeight, 1};
    vkCmdCopyImageToBuffer(selected->command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           selected->buffer, 1, &copy);
    if (oldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        VkImageMemoryBarrier restore{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        restore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        restore.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        restore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        restore.newLayout = oldLayout;
        restore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        restore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        restore.image = image;
        restore.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(selected->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &restore);
    }
    VkBufferMemoryBarrier toHost{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.buffer = selected->buffer;
    toHost.offset = 0;
    toHost.size = byteCount;
    vkCmdPipelineBarrier(selected->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0,
                         0, nullptr, 1, &toHost, 0, nullptr);
    if (vkEndCommandBuffer(selected->command) != VK_SUCCESS) {
        error = "vkEndCommandBuffer failed for AppFlow staging copy";
        return false;
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &selected->command;
    VkResult submitted;
    {
        // Borrow VulkanDevice's per-queue host-synchronization lock only around vkQueueSubmit.
        std::lock_guard submitLock(device->SubmitMutex(queue));
        submitted = vkQueueSubmit(queue, 1, &submit, selected->fence);
    }
    if (submitted != VK_SUCCESS) {
        error = "vkQueueSubmit failed for AppFlow staging copy: " + std::to_string(submitted);
        return false;
    }

    selected->key = std::move(key);
    selected->frame = frame;
    selected->x = x; selected->y = y; selected->width = width; selected->height = height;
    selected->transferX = copyX; selected->transferY = copyY;
    selected->transferWidth = copyWidth; selected->transferHeight = copyHeight;
    selected->format = format;
    selected->enqueueCpuUs = ElapsedUs(started);
    selected->transferBytes = byteCount;
    selected->fullImageTransferFallback = fullImageTransferFallback;
    selected->inFlight = true;
    return true;
}

std::vector<AppFlowReadbackRing::Result> AppFlowReadbackRing::Poll() {
    std::vector<Result> complete;
    for (auto& slot : slots_) {
        if (!slot.inFlight) continue;
        const auto completionStarted = Clock::now();
        const VkResult status = vkGetFenceStatus(slot.logicalDevice, slot.fence);
        if (status == VK_NOT_READY) continue;
        Result result;
        result.key = slot.key;
        result.frame = slot.frame;
        result.x = slot.x; result.y = slot.y; result.width = slot.width; result.height = slot.height;
        result.format = slot.format;
        result.enqueueCpuUs = slot.enqueueCpuUs;
        result.transferBytes = slot.transferBytes;
        result.fullImageTransferFallback = slot.fullImageTransferFallback;
        if (status != VK_SUCCESS) {
            result.error = "vkGetFenceStatus failed for AppFlow staging copy: " + std::to_string(status);
            result.completionCpuUs = ElapsedUs(completionStarted);
            slot.inFlight = false;
            complete.push_back(std::move(result));
            continue;
        }
        void* mapped = nullptr;
        const VkDeviceSize copiedBytes = static_cast<VkDeviceSize>(slot.transferWidth) * slot.transferHeight * 4;
        if (vkMapMemory(slot.logicalDevice, slot.memory, 0, copiedBytes, 0, &mapped) != VK_SUCCESS) {
            result.error = "vkMapMemory failed for completed AppFlow staging copy";
        } else {
            const auto* source = static_cast<const uint8_t*>(mapped);
            result.pixels.resize(static_cast<size_t>(slot.width) * slot.height * 4);
            const uint32_t dx = slot.x - slot.transferX;
            const uint32_t dy = slot.y - slot.transferY;
            for (uint32_t row = 0; row < slot.height; ++row) {
                const size_t sourceOffset = (static_cast<size_t>(dy + row) * slot.transferWidth + dx) * 4;
                const size_t targetOffset = static_cast<size_t>(row) * slot.width * 4;
                std::memcpy(result.pixels.data() + targetOffset, source + sourceOffset,
                            static_cast<size_t>(slot.width) * 4);
            }
            vkUnmapMemory(slot.logicalDevice, slot.memory);
        }
        result.completionCpuUs = ElapsedUs(completionStarted);
        slot.inFlight = false;
        complete.push_back(std::move(result));
    }
    return complete;
}
