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
#include <string>
#include <vector>

namespace Vixen::RenderGraph::Debug {

// Copies `target`'s CURRENT image (GetCurrentImage()) to a host buffer and writes it as an RGB
// PNG at `path`. The target's image MUST have been created with VK_IMAGE_USAGE_TRANSFER_SRC_BIT
// (RenderTargetNode's PARAM_USAGE) -- vkCmdCopyImageToBuffer is a spec violation otherwise, and
// this returns false + sets err rather than letting the driver fault.
//
// PRECONDITION: the image's CURRENT layout must already be VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL.
// This is deliberate, not a shortcut: ComputeDispatchNode tracks each render-target image's real
// last-recorded layout itself (renderTargetImageLayouts_, see BlitRenderTargetToSwapchain/
// DecideRenderTargetPriorLayoutAndUpdate — the fix for KI-007) so its NEXT frame's compute-write
// barrier can declare the correct oldLayout. If this helper transitioned the image to/from
// TRANSFER_SRC_OPTIMAL itself, that private tracking would go stale and reintroduce KI-007 on the
// very next frame. For the editor's compute_render_target, the blit-to-swapchain step leaves the
// image in TRANSFER_SRC_OPTIMAL every frame (see ComputeDispatchNode::BlitRenderTargetToSwapchain's
// header comment) — exactly the state EditorApplication::Update() finds it in (Update() runs
// before the current frame's Render(), so it observes the PREVIOUS frame's already-blitted
// result). So: no layout transition here at all — just a copy from the layout it's already in,
// and the image is left completely undisturbed for the render loop to keep using.
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

    // --- record: a memory-only barrier (NO layout change -- see precondition above), copy, wait ---
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Same-layout barrier: only orders this read-back after the blit's write to the image
    // (VK_ACCESS_TRANSFER_WRITE_BIT from BlitRenderTargetToSwapchain -> VK_ACCESS_TRANSFER_READ_BIT
    // here) on a fresh queue submission. oldLayout == newLayout == TRANSFER_SRC_OPTIMAL, so this
    // never touches (or needs to update) ComputeDispatchNode's renderTargetImageLayouts_ tracking.
    VkImageMemoryBarrier syncOnly{};
    syncOnly.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    syncOnly.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    syncOnly.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    syncOnly.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    syncOnly.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    syncOnly.image = image;
    syncOnly.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    syncOnly.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    syncOnly.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &syncOnly);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, hostBuf, 1, &region);

    // No layout restore needed -- the image is left exactly as found (TRANSFER_SRC_OPTIMAL),
    // matching what ComputeDispatchNode's own tracking already expects for the next frame.
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    const bool submitOk = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS;
    if (submitOk) {
        vkQueueWaitIdle(queue);
    }

    bool ok = submitOk;
    if (ok) {
        void* mapped = nullptr;
        if (vkMapMemory(vkDevice, hostMem, 0, bufSize, 0, &mapped) == VK_SUCCESS) {
            std::vector<uint8_t> rgb(size_t(w) * h * 3);
            const auto* rgba = static_cast<const uint8_t*>(mapped);
            for (uint32_t i = 0; i < w * h; ++i) {
                rgb[i * 3 + 0] = rgba[i * 4 + 0];
                rgb[i * 3 + 1] = rgba[i * 4 + 1];
                rgb[i * 3 + 2] = rgba[i * 4 + 2];
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
