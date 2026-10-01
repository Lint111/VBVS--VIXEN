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
#include <mutex>
#include <string>
#include <vector>

namespace Vixen::RenderGraph::Debug {

// Copies the current graph-produced image to RGB PNG. The target owns its last recorded layout;
// this blocking copy restores it before returning, so capture can repeat or rendering can resume.
// Undefined images have no produced contents and cannot be captured.
//
// Blocking: submits a one-shot command buffer on `queue` and waits for it (vkQueueWaitIdle) --
// fine for an unattended capture harness, not for a per-frame hot path.
inline bool CaptureRenderTargetToPng(Vixen::Vulkan::Resources::VulkanDevice* device,
                                      Vixen::Vulkan::Resources::IRenderTarget* target,
                                      VkQueue queue,
                                      uint32_t queueFamilyIndex,
                                      const std::string& path,
                                      std::string& err) {
    if (!device || !target || queue == VK_NULL_HANDLE) {
        err = "CaptureRenderTargetToPng: null device/target/queue";
        return false;
    }
    const VkDevice vkDevice = device->device;
    const VkImage image = target->GetCurrentImage();
    if (image == VK_NULL_HANDLE) {
        err = "CaptureRenderTargetToPng: target's current image is VK_NULL_HANDLE";
        return false;
    }
    const VkImageLayout currentLayout = target->GetCurrentLayout();
    if (currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        err = "CaptureRenderTargetToPng: current image has no graph-produced layout";
        return false;
    }
    if (!(target->GetImageUsageFlags() & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        err = "CaptureRenderTargetToPng: target was not created with VK_IMAGE_USAGE_TRANSFER_SRC_BIT";
        return false;
    }

    const VkExtent2D extent = target->GetExtent();
    const uint32_t w = extent.width, h = extent.height;
    if (w == 0 || h == 0) {
        err = "CaptureRenderTargetToPng: target has zero extent";
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
        err = "CaptureRenderTargetToPng: vkCreateCommandPool failed";
        return false;
    }

    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool = cmdPool;
    cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDevice, &cbAlloc, &cmd) != VK_SUCCESS) {
        err = "CaptureRenderTargetToPng: vkAllocateCommandBuffers failed";
        vkDestroyCommandPool(vkDevice, cmdPool, nullptr);
        return false;
    }

    // --- host-visible readback buffer ---
    const VkDeviceSize bufSize = VkDeviceSize(w) * h * 4;
    VkBuffer hostBuf = VK_NULL_HANDLE;
    VkDeviceMemory hostMem = VK_NULL_HANDLE;
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = bufSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vkDevice, &bufInfo, nullptr, &hostBuf) != VK_SUCCESS) {
        err = "CaptureRenderTargetToPng: vkCreateBuffer failed";
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
        err = "CaptureRenderTargetToPng: no host-visible/coherent memory type found";
        vkDestroyBuffer(vkDevice, hostBuf, nullptr);
        vkDestroyCommandPool(vkDevice, cmdPool, nullptr);
        return false;
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &hostMem) != VK_SUCCESS) {
        err = "CaptureRenderTargetToPng: vkAllocateMemory failed";
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
    bool submitOk;
    {
        // Externally synchronized per Vulkan spec (audit V-M11).
        std::lock_guard submitLock(device->SubmitMutex(queue));
        submitOk = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS;
        if (submitOk) {
            vkQueueWaitIdle(queue);
        }
    }

    bool ok = submitOk;
    if (ok) {
        void* mapped = nullptr;
        if (vkMapMemory(vkDevice, hostMem, 0, bufSize, 0, &mapped) == VK_SUCCESS) {
            // Swap R and B for BGRA-family formats so the PNG matches on-screen colors (see
            // the swapchain channel-order contract). No-op for this helper's
            // usual RGBA offscreen targets (VK_FORMAT_R8G8B8A8_*).
            const VkFormat fmt = target->GetFormat();
            const bool swapRB = (fmt == VK_FORMAT_B8G8R8A8_UNORM ||
                                 fmt == VK_FORMAT_B8G8R8A8_SRGB  ||
                                 fmt == VK_FORMAT_B8G8R8A8_SNORM ||
                                 fmt == VK_FORMAT_B8G8R8A8_UINT  ||
                                 fmt == VK_FORMAT_B8G8R8A8_SINT);
            const uint32_t rIdx = swapRB ? 2u : 0u;
            const uint32_t bIdx = swapRB ? 0u : 2u;
            std::vector<uint8_t> rgb(size_t(w) * h * 3);
            const auto* rgba = static_cast<const uint8_t*>(mapped);
            for (uint32_t i = 0; i < w * h; ++i) {
                rgb[i * 3 + 0] = rgba[i * 4 + rIdx];
                rgb[i * 3 + 1] = rgba[i * 4 + 1];
                rgb[i * 3 + 2] = rgba[i * 4 + bIdx];
            }
            vkUnmapMemory(vkDevice, hostMem);
            ok = stbi_write_png(path.c_str(), int(w), int(h), 3, rgb.data(), int(w) * 3) != 0;
            if (!ok) err = "CaptureRenderTargetToPng: stbi_write_png failed for " + path;
        } else {
            ok = false;
            err = "CaptureRenderTargetToPng: vkMapMemory failed";
        }
    } else {
        err = "CaptureRenderTargetToPng: vkQueueSubmit failed";
    }

    vkDestroyBuffer(vkDevice, hostBuf, nullptr);
    vkFreeMemory(vkDevice, hostMem, nullptr);
    vkDestroyCommandPool(vkDevice, cmdPool, nullptr);
    return ok;
}

} // namespace Vixen::RenderGraph::Debug
