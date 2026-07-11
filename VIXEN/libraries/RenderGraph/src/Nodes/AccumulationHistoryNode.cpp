// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc2 M1: persistent temporal-accumulation history image, allocated +
// transitioned + wired, but not yet read/written by the shader (see AccumulationHistoryNode.h).

#include "Nodes/AccumulationHistoryNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include <mutex>
#include <stdexcept>

// Local helper — mirrors RenderGraph::NodeHelpers::FindMemoryType from BufferHelpers.h but defined
// here to avoid emitting an inline-function COMDAT into this TU that conflicts when the same inline
// is instantiated in other TUs with different COMDAT selection (see PickIdTargetNode.cpp).
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
        std::string("[AccumulationHistoryNode] No suitable memory type found for ") + context);
}

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ====== AccumulationHistoryNodeType ======

std::unique_ptr<NodeInstance> AccumulationHistoryNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<AccumulationHistoryNode>(n, const_cast<AccumulationHistoryNodeType*>(this));
}

// ====== AccumulationHistoryNode ======

AccumulationHistoryNode::AccumulationHistoryNode(const std::string& n, NodeType* t)
    : TypedNode<AccumulationHistoryNodeConfig>(n, t)
{
}

void AccumulationHistoryNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[AccumulationHistoryNode] Setup (graph-scope initialization)");
}

void AccumulationHistoryNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[AccumulationHistoryNode] Compile START");

    SetDevice(ctx.In(AccumulationHistoryNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[AccumulationHistoryNode] VULKAN_DEVICE_IN is null");
    }

    VkCommandPool commandPool = ctx.In(AccumulationHistoryNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[AccumulationHistoryNode] COMMAND_POOL is null");
    }

    width_  = ctx.In(AccumulationHistoryNodeConfig::WIDTH);
    height_ = ctx.In(AccumulationHistoryNodeConfig::HEIGHT);
    if (width_ == 0 || height_ == 0) {
        throw std::runtime_error("[AccumulationHistoryNode] WIDTH/HEIGHT must be > 0 (got " +
                                 std::to_string(width_) + "x" + std::to_string(height_) + ")");
    }

    // Image is persistent across a same-size recompile, but MUST follow the render extent --
    // a stale image left at the startup size would be out of bounds after a resize (mirrors
    // PickIdTargetNode's own followSwapchainExtent discipline).
    if (image_ == VK_NULL_HANDLE) {
        CreateImage(GetDevice(), commandPool);
        createdWidth_  = width_;
        createdHeight_ = height_;
    } else if (width_ != createdWidth_ || height_ != createdHeight_) {
        NODE_LOG_INFO("[AccumulationHistoryNode] Extent changed (" + std::to_string(createdWidth_) + "x" +
                      std::to_string(createdHeight_) + " -> " + std::to_string(width_) + "x" +
                      std::to_string(height_) + ") - recreating history image");
        DestroyImage();
        CreateImage(GetDevice(), commandPool);
        createdWidth_  = width_;
        createdHeight_ = height_;
        NODE_LOG_INFO("[AccumulationHistoryNode] history image recreated " + std::to_string(createdWidth_) +
                      "x" + std::to_string(createdHeight_));
    } else {
        NODE_LOG_INFO("[AccumulationHistoryNode] Reusing persistent history image across recompile");
    }

    ctx.Out(AccumulationHistoryNodeConfig::HISTORY_IMAGE_VIEW, view_);
    ctx.Out(AccumulationHistoryNodeConfig::HISTORY_IMAGE,      image_);

    NODE_LOG_INFO("[AccumulationHistoryNode] Outputs published (" + std::to_string(width_) + "x" +
                  std::to_string(height_) + ", R8G8B8A8_UNORM storage image)");
}

void AccumulationHistoryNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Not a ring -- the same persistent image/view is re-emitted every frame (the whole point of
    // "history": last frame's write must still be here for this frame to read).
    ctx.Out(AccumulationHistoryNodeConfig::HISTORY_IMAGE_VIEW, view_);
    ctx.Out(AccumulationHistoryNodeConfig::HISTORY_IMAGE,      image_);
}

void AccumulationHistoryNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (KI-004 class) left stale handles
    // that crashed the first post-recovery use/teardown. Mirrors PickIdTargetNode's guard.
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[AccumulationHistoryNode] Cleanup (recompile) - keeping persistent history image");
        return;
    }

    NODE_LOG_INFO("[AccumulationHistoryNode] Cleanup (final teardown) - destroying history image");
    DestroyImage();
}

void AccumulationHistoryNode::CreateImage(VulkanDevice* device, VkCommandPool commandPool) {
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    // --- Create R8G8B8A8_UNORM storage image (matches outputImage's format) ---
    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = kFormat;
    imgInfo.extent        = {width_, height_, 1u};
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT;  // M1: storage only, no transfer needed yet
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(vkDevice, &imgInfo, nullptr, &image_) != VK_SUCCESS) {
        throw std::runtime_error("[AccumulationHistoryNode] vkCreateImage failed");
    }

    // --- Allocate device-local memory via real memory-type selection ---
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(vkDevice, image_, &req);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = FindSuitableMemoryType(
        memProps,
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "AccumulationHistoryNode R8G8B8A8_UNORM image"
    );

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
        vkDestroyImage(vkDevice, image_, nullptr);
        image_ = VK_NULL_HANDLE;
        throw std::runtime_error("[AccumulationHistoryNode] vkAllocateMemory failed");
    }

    vkBindImageMemory(vkDevice, image_, memory_, 0);

    // --- Create image view ---
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image            = image_;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = kFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    if (vkCreateImageView(vkDevice, &viewInfo, nullptr, &view_) != VK_SUCCESS) {
        throw std::runtime_error("[AccumulationHistoryNode] vkCreateImageView failed");
    }

    // One-time UNDEFINED -> GENERAL transition. Storage images stay GENERAL across dispatches, so
    // this single transition makes the descriptor (always GENERAL) correct for every frame.
    TransitionToGeneral(commandPool);

    NODE_LOG_INFO("[AccumulationHistoryNode] Created R8G8B8A8_UNORM storage image at " +
                  std::to_string(width_) + "x" + std::to_string(height_) +
                  " (transitioned UNDEFINED->GENERAL)");
}

void AccumulationHistoryNode::TransitionToGeneral(VkCommandPool commandPool) {
    VkDevice vkDevice = GetDevice()->device;

    // One-shot command buffer for the layout transition.
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDevice, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("[AccumulationHistoryNode] vkAllocateCommandBuffers (transition) failed");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
        throw std::runtime_error("[AccumulationHistoryNode] vkBeginCommandBuffer (transition) failed");
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image_;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
        throw std::runtime_error("[AccumulationHistoryNode] vkEndCommandBuffer (transition) failed");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    // Submit on the device queue and wait — this runs once at Compile, before any dispatch.
    // Externally synchronized per Vulkan spec (audit V-M11) regardless.
    {
        std::lock_guard<std::mutex> submitLock(GetDevice()->SubmitMutex(GetDevice()->queue));
        if (vkQueueSubmit(GetDevice()->queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
            throw std::runtime_error("[AccumulationHistoryNode] vkQueueSubmit (transition) failed");
        }
        vkQueueWaitIdle(GetDevice()->queue);
    }

    vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
}

void AccumulationHistoryNode::DestroyImage() {
    if (!GetDevice()) return;
    VkDevice vkDevice = GetDevice()->device;

    if (view_   != VK_NULL_HANDLE) { vkDestroyImageView(vkDevice, view_,   nullptr); view_   = VK_NULL_HANDLE; }
    if (image_  != VK_NULL_HANDLE) { vkDestroyImage    (vkDevice, image_,  nullptr); image_  = VK_NULL_HANDLE; }
    if (memory_ != VK_NULL_HANDLE) { vkFreeMemory      (vkDevice, memory_, nullptr); memory_ = VK_NULL_HANDLE; }

    NODE_LOG_INFO("[AccumulationHistoryNode] History image destroyed");
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::AccumulationHistoryNodeType);
