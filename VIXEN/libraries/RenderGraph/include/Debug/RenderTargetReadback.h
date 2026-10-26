#pragma once
// Inc-2b (AppFlow) — a minimal IRenderTarget -> host RGBA8 -> PNG readback, shared between the
// editor's VIXEN_EDITOR_CAPTURE_FRAMES harness and any future test that wants a same-shaped
// readback. Extracted from test_editor_document_render.cpp's device->host copy (the pipeline
// setup in that test is bespoke to its own from-scratch Vulkan fixture and is NOT reused here;
// only the generic "copy an already-rendered VkImage to a PNG" tail is generic enough to share).
//
// Header-only (mirrors IRenderTarget.h / stb's own header-only shape) so no new library/.cpp
// wiring is needed — callers just #include this + link the existing `stb` target for the
// STB_IMAGE_WRITE_IMPLEMENTATION TU (exactly one TU in the whole link must define it).
#include "IRenderTarget.h"
#include "VulkanDevice.h"
#include <stb_image_write.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Vixen::RenderGraph::Debug {

struct PendingRenderTargetReadback {
    struct State {
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        std::shared_ptr<Vixen::Vulkan::Resources::QueueSubmitCompletion> completion;
        bool retired = false;
        bool orphaned = false;

        void Cleanup() {
            if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, nullptr);
            if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
            if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
            buffer = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            commandPool = VK_NULL_HANDLE;
            command = VK_NULL_HANDLE;
        }

        void Retire() {
            retired = true;
            if (orphaned) Cleanup();
        }

        ~State() {
            orphaned = true;
            if (retired) Cleanup();
        }
    };

    std::shared_ptr<State> state;

    [[nodiscard]] bool IsReady() const {
        return state && state->completion && state->completion->IsRetired();
    }

    /// Consume the next-frame result. Returns false while the owner has not retired the copy.
    bool Poll(std::vector<uint8_t>& bytes, std::string& err) {
        if (!state || !state->completion || !state->completion->IsRetired()) return false;
        const auto current = std::move(state);
        if (current->completion->Result() != VK_SUCCESS) {
            err = "RenderTargetReadback queue-owner submit failed: " +
                  std::to_string(current->completion->Result());
            current->Cleanup();
            return true;
        }
        void* mapped = nullptr;
        if (vkMapMemory(current->device, current->memory, 0, current->size, 0, &mapped) != VK_SUCCESS) {
            err = "RenderTargetReadback: vkMapMemory failed";
            current->Cleanup();
            return true;
        }
        bytes.resize(static_cast<size_t>(current->size));
        std::memcpy(bytes.data(), mapped, bytes.size());
        vkUnmapMemory(current->device, current->memory);
        current->Cleanup();
        return true;
    }
};

// Queue a raw readback of a graph-produced image. Restores the recorded layout in the submitted
// command; the bytes become available from PendingRenderTargetReadback::Poll on a later frame.
// Float guides retain their original bytes and precision.
inline bool ReadRenderTargetBytes(Vixen::Vulkan::Resources::VulkanDevice* device,
                                 Vixen::Vulkan::Resources::IRenderTarget* target,
                                 VkQueue queue, uint32_t queueFamilyIndex,
                                 std::vector<uint8_t>& bytes, std::string& err,
                                 std::shared_ptr<PendingRenderTargetReadback>* pendingOut = nullptr) {
    if (!device || !target || queue == VK_NULL_HANDLE) {
        err = "ReadRenderTargetBytes: null device/target/queue";
        return false;
    }
    uint32_t bytesPerTexel = 0;
    switch (target->GetFormat()) {
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_SNORM:
        case VK_FORMAT_R8G8B8A8_UINT:
        case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_SNORM:
        case VK_FORMAT_B8G8R8A8_UINT:
        case VK_FORMAT_B8G8R8A8_SINT:
            bytesPerTexel = 4;
            break;
        case VK_FORMAT_R32G32_UINT:
        case VK_FORMAT_R32G32_SINT:
        case VK_FORMAT_R32G32_SFLOAT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            bytesPerTexel = 8;
            break;
        default:
            err = "ReadRenderTargetBytes: unsupported image format";
            return false;
    }
    const VkDevice vkDevice = device->device;
    const VkImage image = target->GetCurrentImage();
    if (image == VK_NULL_HANDLE) {
        err = "ReadRenderTargetBytes: target's current image is VK_NULL_HANDLE";
        return false;
    }
    const VkImageLayout currentLayout = target->GetCurrentLayout();
    if (currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        err = "ReadRenderTargetBytes: current image has no graph-produced layout";
        return false;
    }
    if (!(target->GetImageUsageFlags() & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        err = "ReadRenderTargetBytes: target was not created with VK_IMAGE_USAGE_TRANSFER_SRC_BIT";
        return false;
    }

    const VkExtent2D extent = target->GetExtent();
    const uint32_t w = extent.width, h = extent.height;
    if (w == 0 || h == 0) {
        err = "ReadRenderTargetBytes: target has zero extent";
        return false;
    }

    // --- one-shot command pool + buffer (not the graph's pre-allocated pool -- this is an
    // out-of-band, low-frequency capture, not a per-frame render path) ---
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    if (vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &cmdPool) != VK_SUCCESS) {
        err = "ReadRenderTargetBytes: vkCreateCommandPool failed";
        return false;
    }

    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool = cmdPool;
    cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDevice, &cbAlloc, &cmd) != VK_SUCCESS) {
        err = "ReadRenderTargetBytes: vkAllocateCommandBuffers failed";
        vkDestroyCommandPool(vkDevice, cmdPool, nullptr);
        return false;
    }

    // --- host-visible readback buffer ---
    const VkDeviceSize bufSize = VkDeviceSize(w) * h * bytesPerTexel;
    VkBuffer hostBuf = VK_NULL_HANDLE;
    VkDeviceMemory hostMem = VK_NULL_HANDLE;
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = bufSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vkDevice, &bufInfo, nullptr, &hostBuf) != VK_SUCCESS) {
        err = "ReadRenderTargetBytes: vkCreateBuffer failed";
        vkDestroyCommandPool(vkDevice, cmdPool, nullptr);
        return false;
    }
    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(vkDevice, hostBuf, &memReq);
    uint32_t memTypeIndex = UINT32_MAX;
    const VkMemoryPropertyFlags hostFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < device->gpuMemoryProperties.memoryTypeCount; ++i) {
        if ((memReq.memoryTypeBits & (1u << i)) &&
            (device->gpuMemoryProperties.memoryTypes[i].propertyFlags & hostFlags) == hostFlags) {
            memTypeIndex = i;
            break;
        }
    }
    if (memTypeIndex == UINT32_MAX) {
        err = "ReadRenderTargetBytes: no host-visible/coherent memory type found";
        vkDestroyBuffer(vkDevice, hostBuf, nullptr);
        vkDestroyCommandPool(vkDevice, cmdPool, nullptr);
        return false;
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &hostMem) != VK_SUCCESS) {
        err = "ReadRenderTargetBytes: vkAllocateMemory failed";
        vkDestroyBuffer(vkDevice, hostBuf, nullptr);
        vkDestroyCommandPool(vkDevice, cmdPool, nullptr);
        return false;
    }
    vkBindBufferMemory(vkDevice, hostBuf, hostMem, 0);

    // --- record: transition (if needed), copy, restore (if needed) ---
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = currentLayout;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toTransfer.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, hostBuf, 1, &region);

    if (currentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        VkImageMemoryBarrier restore{};
        restore.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        restore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        restore.newLayout = currentLayout;
        restore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        restore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        restore.image = image;
        restore.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        restore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        restore.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &restore);
    }
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    // R739: the helper is a producer, not a queue owner. The completion callback owns cleanup;
    // the caller observes the result on a later frame through the explicit readback handle.
    auto state = std::make_shared<PendingRenderTargetReadback::State>();
    state->device = vkDevice;
    state->commandPool = cmdPool;
    state->command = cmd;
    state->buffer = hostBuf;
    state->memory = hostMem;
    state->size = bufSize;
    auto request = Vixen::Vulkan::Resources::QueueSubmitRequest::FromSubmitInfo(
        device, queue, submitInfo);
    request.onRetired = [state] { state->Retire(); };
    const auto completion = device->PublishSubmitRequest(std::move(request));
    if (!completion || (completion->Result() != VK_NOT_READY &&
                        completion->Result() != VK_SUCCESS)) {
        err = "ReadRenderTargetBytes: queue owner rejected the readback";
        return false;
    }
    state->completion = completion;
    if (pendingOut) {
        auto pending = std::make_shared<PendingRenderTargetReadback>();
        pending->state = std::move(state);
        *pendingOut = std::move(pending);
    }
    err = "ReadRenderTargetBytes: result pending until the next frame";
    (void)bytes;
    return false;
}

inline std::shared_ptr<PendingRenderTargetReadback> BeginReadRenderTargetBytes(
    Vixen::Vulkan::Resources::VulkanDevice* device,
    Vixen::Vulkan::Resources::IRenderTarget* target,
    VkQueue queue, uint32_t queueFamilyIndex,
    std::string& err) {
    auto pending = std::make_shared<PendingRenderTargetReadback>();
    std::vector<uint8_t> ignored;
    if (ReadRenderTargetBytes(device, target, queue, queueFamilyIndex, ignored, err, &pending)) return pending;
    if (pending->state) return pending;
    return nullptr;
}

// Existing RGBA8/BGRA8 captures keep their channel order and PNG encoding. The returned handle
// owns the next-frame completion; callers that need a bool completion result should retain it and
// call Poll rather than treating the legacy bool wrapper as synchronous.
struct PendingRenderTargetPngCapture {
    std::shared_ptr<PendingRenderTargetReadback> readback;
    std::string path;
    uint32_t width = 0;
    uint32_t height = 0;
    bool swapRB = false;

    /// Encode the next-frame bytes once the queue-owner completion is retired.
    bool Poll(std::string& err) {
        if (!readback) return true;
        std::vector<uint8_t> bytes;
        std::string readbackError;
        if (!readback->Poll(bytes, readbackError)) return false;
        if (!readbackError.empty()) {
            err = std::move(readbackError);
            return true;
        }
        err.clear();
        std::vector<uint8_t> rgb(size_t(width) * height * 3);
        for (size_t i = 0; i < size_t(width) * height; ++i) {
            rgb[i * 3] = bytes[i * 4 + (swapRB ? 2u : 0u)];
            rgb[i * 3 + 1] = bytes[i * 4 + 1];
            rgb[i * 3 + 2] = bytes[i * 4 + (swapRB ? 0u : 2u)];
        }
        if (stbi_write_png(path.c_str(), int(width), int(height), 3, rgb.data(), int(width) * 3) == 0) {
            err = "CaptureRenderTargetToPng: stbi_write_png failed for " + path;
        }
        return true;
    }
};

inline std::shared_ptr<PendingRenderTargetPngCapture> BeginCaptureRenderTargetToPng(
    Vixen::Vulkan::Resources::VulkanDevice* device,
    Vixen::Vulkan::Resources::IRenderTarget* target,
    VkQueue queue, uint32_t queueFamilyIndex,
    const std::string& path, std::string& err) {
    if (!target) {
        err = "CaptureRenderTargetToPng: null target";
        return nullptr;
    }
    const VkFormat fmt = target->GetFormat();
    const bool swapRB = fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB ||
                        fmt == VK_FORMAT_B8G8R8A8_SNORM || fmt == VK_FORMAT_B8G8R8A8_UINT ||
                        fmt == VK_FORMAT_B8G8R8A8_SINT;
    const bool rgba = fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_SRGB ||
                      fmt == VK_FORMAT_R8G8B8A8_SNORM || fmt == VK_FORMAT_R8G8B8A8_UINT ||
                      fmt == VK_FORMAT_R8G8B8A8_SINT;
    if (!rgba && !swapRB) {
        err = "CaptureRenderTargetToPng: expected an RGBA8 or BGRA8 image";
        return nullptr;
    }
    auto pending = std::make_shared<PendingRenderTargetPngCapture>();
    pending->readback = BeginReadRenderTargetBytes(device, target, queue, queueFamilyIndex, err);
    if (!pending->readback) return nullptr;
    const auto extent = target->GetExtent();
    pending->path = path;
    pending->width = extent.width;
    pending->height = extent.height;
    pending->swapRB = swapRB;
    err = "CaptureRenderTargetToPng: result pending until the next frame";
    return pending;
}

inline bool CaptureRenderTargetToPng(Vixen::Vulkan::Resources::VulkanDevice* device,
                                    Vixen::Vulkan::Resources::IRenderTarget* target,
                                    VkQueue queue, uint32_t queueFamilyIndex,
                                    const std::string& path, std::string& err,
                                    std::shared_ptr<PendingRenderTargetPngCapture>* pendingOut = nullptr) {
    auto pending = BeginCaptureRenderTargetToPng(device, target, queue, queueFamilyIndex, path, err);
    if (!pending) return false;
    if (pendingOut) {
        *pendingOut = std::move(pending);
        return false;
    }
    return false;
}

} // namespace Vixen::RenderGraph::Debug
