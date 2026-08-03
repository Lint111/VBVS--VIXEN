// Copyright (C) 2026 Lior Yanai (eLiorg). Licensed under the MIT License.
// Raster-proxy B1 M4: depth ping-pong pair for the occlusion probe — march writes
// [frame&1], HiZ reduce reads [(frame+1)&1]; no sync edges by construction
// (distinct VkImage per slot, the shell double-buffer precedent).

#include "Nodes/DepthTargetNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include <mutex>
#include <stdexcept>
#include <string>

// Local helper — mirrors WorldPosHistoryNode.cpp's own local FindSuitableMemoryType (kept
// per-TU rather than shared, same inline-COMDAT rationale documented there).
static uint32_t FindSuitableMemoryType(
    const VkPhysicalDeviceMemoryProperties& memProps,
    uint32_t typeFilter,
    VkMemoryPropertyFlags required,
    const char* context)
{
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    throw std::runtime_error(
        std::string("[DepthTargetNode] No suitable memory type found for ") + context);
}

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ====== DepthTargetNodeType ======

std::unique_ptr<NodeInstance> DepthTargetNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<DepthTargetNode>(n, const_cast<DepthTargetNodeType*>(this));
}

// ====== DepthTargetNode ======

DepthTargetNode::DepthTargetNode(const std::string& n, NodeType* t)
    : TypedNode<DepthTargetNodeConfig>(n, t)
{
}

void DepthTargetNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[DepthTargetNode] Setup (graph-scope initialization)");
}

void DepthTargetNode::CompileImpl(TypedCompileContext& ctx) {
    SetDevice(ctx.In(DepthTargetNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[DepthTargetNode] VULKAN_DEVICE_IN is null");
    }
    VkCommandPool commandPool = ctx.In(DepthTargetNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[DepthTargetNode] COMMAND_POOL is null");
    }

    width_  = ctx.In(DepthTargetNodeConfig::WIDTH);
    height_ = ctx.In(DepthTargetNodeConfig::HEIGHT);
    if (width_ == 0 || height_ == 0) {
        throw std::runtime_error("[DepthTargetNode] WIDTH/HEIGHT must be > 0 (got " +
                                 std::to_string(width_) + "x" + std::to_string(height_) + ")");
    }

    if (images_[0] == VK_NULL_HANDLE) {
        CreateImages(GetDevice(), commandPool);
        createdWidth_  = width_;
        createdHeight_ = height_;
    } else if (width_ != createdWidth_ || height_ != createdHeight_) {
        NODE_LOG_INFO("[DepthTargetNode] Extent changed (" + std::to_string(createdWidth_) + "x" +
                      std::to_string(createdHeight_) + " -> " + std::to_string(width_) + "x" +
                      std::to_string(height_) + ") - recreating depth ping-pong pair");
        DestroyImages();
        CreateImages(GetDevice(), commandPool);
        createdWidth_  = width_;
        createdHeight_ = height_;
    } else {
        NODE_LOG_INFO("[DepthTargetNode] Reusing persistent depth ping-pong pair across recompile");
    }

    // Initial (frame-0 parity) emission; Execute re-emits per real frame index.
    ctx.Out(DepthTargetNodeConfig::DEPTH_WRITE_VIEW, views_[0]);
    ctx.Out(DepthTargetNodeConfig::DEPTH_READ_VIEW,  views_[1]);
}

void DepthTargetNode::ExecuteImpl(TypedExecuteContext& ctx) {
    const uint32_t frame = ctx.In(DepthTargetNodeConfig::CURRENT_FRAME_INDEX);
    const uint32_t writeSlot = frame & 1u;
    ctx.Out(DepthTargetNodeConfig::DEPTH_WRITE_VIEW, views_[writeSlot]);
    ctx.Out(DepthTargetNodeConfig::DEPTH_READ_VIEW,  views_[writeSlot ^ 1u]);
}

void DepthTargetNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Same persist-across-recompile / DeviceLost guard as WorldPosHistoryNode.
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[DepthTargetNode] Cleanup (recompile) - keeping depth ping-pong pair");
        return;
    }
    NODE_LOG_INFO("[DepthTargetNode] Cleanup (final teardown) - destroying depth ping-pong pair");
    DestroyImages();
}

void DepthTargetNode::CreateImages(VulkanDevice* device, VkCommandPool commandPool) {
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    for (uint32_t slot = 0; slot < 2u; ++slot) {
        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = kFormat;
        imgInfo.extent        = {width_, height_, 1u};
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_DST solely for the one-time sentinel clear below.
        imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(vkDevice, &imgInfo, nullptr, &images_[slot]) != VK_SUCCESS) {
            throw std::runtime_error("[DepthTargetNode] vkCreateImage failed");
        }

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(vkDevice, images_[slot], &req);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = req.size;
        allocInfo.memoryTypeIndex = FindSuitableMemoryType(
            memProps, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "DepthTargetNode R32_SFLOAT image");
        if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memories_[slot]) != VK_SUCCESS) {
            vkDestroyImage(vkDevice, images_[slot], nullptr);
            images_[slot] = VK_NULL_HANDLE;
            throw std::runtime_error("[DepthTargetNode] vkAllocateMemory failed");
        }
        vkBindImageMemory(vkDevice, images_[slot], memories_[slot], 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image            = images_[slot];
        viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format           = kFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(vkDevice, &viewInfo, nullptr, &views_[slot]) != VK_SUCCESS) {
            throw std::runtime_error("[DepthTargetNode] vkCreateImageView failed");
        }
    }

    // One-shot: UNDEFINED -> TRANSFER_DST, clear both slots to the miss sentinel,
    // then -> GENERAL (storage images stay GENERAL across dispatches). Frame-0
    // validity: the reduce sees "sky everywhere" and the cull skips nothing.
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDevice, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("[DepthTargetNode] vkAllocateCommandBuffers (clear) failed");
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
        throw std::runtime_error("[DepthTargetNode] vkBeginCommandBuffer (clear) failed");
    }

    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (uint32_t slot = 0; slot < 2u; ++slot) {
        VkImageMemoryBarrier toDst{};
        toDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.srcAccessMask       = 0;
        toDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        toDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image               = images_[slot];
        toDst.subresourceRange    = range;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toDst);

        VkClearColorValue sentinel{};
        sentinel.float32[0] = kMissSentinel;
        vkCmdClearColorImage(cmd, images_[slot], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &sentinel, 1, &range);

        VkImageMemoryBarrier toGeneral = toDst;
        toGeneral.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        toGeneral.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toGeneral.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toGeneral);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
        throw std::runtime_error("[DepthTargetNode] vkEndCommandBuffer (clear) failed");
    }

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    {
        std::lock_guard<std::mutex> submitLock(device->SubmitMutex(device->queue));
        if (vkQueueSubmit(device->queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
            throw std::runtime_error("[DepthTargetNode] vkQueueSubmit (clear) failed");
        }
        vkQueueWaitIdle(device->queue);
    }
    vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);

    NODE_LOG_INFO("[DepthTargetNode] Created R32_SFLOAT ping-pong pair at " +
                  std::to_string(width_) + "x" + std::to_string(height_) +
                  " (both slots cleared to 1e30, GENERAL)");
}

void DepthTargetNode::DestroyImages() {
    if (!GetDevice()) return;
    VkDevice vkDevice = GetDevice()->device;
    for (uint32_t slot = 0; slot < 2u; ++slot) {
        if (views_[slot] != VK_NULL_HANDLE) {
            vkDestroyImageView(vkDevice, views_[slot], nullptr);
            views_[slot] = VK_NULL_HANDLE;
        }
        if (images_[slot] != VK_NULL_HANDLE) {
            vkDestroyImage(vkDevice, images_[slot], nullptr);
            images_[slot] = VK_NULL_HANDLE;
        }
        if (memories_[slot] != VK_NULL_HANDLE) {
            vkFreeMemory(vkDevice, memories_[slot], nullptr);
            memories_[slot] = VK_NULL_HANDLE;
        }
    }
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::DepthTargetNodeType);
