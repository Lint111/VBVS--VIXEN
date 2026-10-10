// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Persistent worldPos/depth companion history pair, with immutable previous-frame input and
// separate current-frame output roles matching AccumulationHistoryNode.

#include "Nodes/WorldPosHistoryNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include <mutex>
#include <stdexcept>

// Local helper — mirrors AccumulationHistoryNode.cpp's own local FindSuitableMemoryType (kept
// per-TU rather than shared, to avoid the inline-COMDAT conflict documented there).
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
        std::string("[WorldPosHistoryNode] No suitable memory type found for ") + context);
}

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ====== WorldPosHistoryNodeType ======

std::unique_ptr<NodeInstance> WorldPosHistoryNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<WorldPosHistoryNode>(n, const_cast<WorldPosHistoryNodeType*>(this));
}

// ====== WorldPosHistoryNode ======

WorldPosHistoryNode::WorldPosHistoryNode(const std::string& n, NodeType* t)
    : TypedNode<WorldPosHistoryNodeConfig>(n, t)
{
}

void WorldPosHistoryNode::TypedSetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[WorldPosHistoryNode] Setup (graph-scope initialization)");
}

void WorldPosHistoryNode::TypedCompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[WorldPosHistoryNode] Compile START");

    SetDevice(ctx.In(WorldPosHistoryNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[WorldPosHistoryNode] VULKAN_DEVICE_IN is null");
    }

    VkCommandPool commandPool = ctx.In(WorldPosHistoryNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[WorldPosHistoryNode] COMMAND_POOL is null");
    }

    width_  = ctx.In(WorldPosHistoryNodeConfig::WIDTH);
    height_ = ctx.In(WorldPosHistoryNodeConfig::HEIGHT);
    if (width_ == 0 || height_ == 0) {
        throw std::runtime_error("[WorldPosHistoryNode] WIDTH/HEIGHT must be > 0 (got " +
                                 std::to_string(width_) + "x" + std::to_string(height_) + ")");
    }

    if (images_[0] == VK_NULL_HANDLE || images_[1] == VK_NULL_HANDLE) {
        DestroyImages();
        CreateImage(GetDevice(), commandPool, 0);
        CreateImage(GetDevice(), commandPool, 1);
        nextWriteImageIndex_ = 1;
        createdWidth_  = width_;
        createdHeight_ = height_;
    } else if (width_ != createdWidth_ || height_ != createdHeight_) {
        NODE_LOG_INFO("[WorldPosHistoryNode] Extent changed (" + std::to_string(createdWidth_) + "x" +
                      std::to_string(createdHeight_) + " -> " + std::to_string(width_) + "x" +
                      std::to_string(height_) + ") - recreating worldPos history image");
        DestroyImages();
        CreateImage(GetDevice(), commandPool, 0);
        CreateImage(GetDevice(), commandPool, 1);
        nextWriteImageIndex_ = 1;
        createdWidth_  = width_;
        createdHeight_ = height_;
        NODE_LOG_INFO("[WorldPosHistoryNode] worldPos history image recreated " + std::to_string(createdWidth_) +
                      "x" + std::to_string(createdHeight_));
    } else {
        NODE_LOG_INFO("[WorldPosHistoryNode] Reusing persistent worldPos history image pair across recompile");
    }

    const uint32_t currentIndex = nextWriteImageIndex_;
    const uint32_t previousIndex = 1u - currentIndex;
    ctx.Out(WorldPosHistoryNodeConfig::WORLDPOS_IMAGE_VIEW, views_[previousIndex]);
    ctx.Out(WorldPosHistoryNodeConfig::WORLDPOS_IMAGE,      images_[previousIndex]);
    ctx.Out(WorldPosHistoryNodeConfig::CURRENT_WORLDPOS_IMAGE_VIEW, views_[currentIndex]);
    ctx.Out(WorldPosHistoryNodeConfig::CURRENT_WORLDPOS_IMAGE,      images_[currentIndex]);

    NODE_LOG_INFO("[WorldPosHistoryNode] World-position history image pair published (" +
                  std::to_string(width_) + "x" + std::to_string(height_) +
                  ", R32G32B32A32_SFLOAT storage images)");
}

void WorldPosHistoryNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    const uint32_t currentIndex = nextWriteImageIndex_;
    const uint32_t previousIndex = 1u - currentIndex;
    ctx.Out(WorldPosHistoryNodeConfig::WORLDPOS_IMAGE_VIEW, views_[previousIndex]);
    ctx.Out(WorldPosHistoryNodeConfig::WORLDPOS_IMAGE,      images_[previousIndex]);
    ctx.Out(WorldPosHistoryNodeConfig::CURRENT_WORLDPOS_IMAGE_VIEW, views_[currentIndex]);
    ctx.Out(WorldPosHistoryNodeConfig::CURRENT_WORLDPOS_IMAGE,      images_[currentIndex]);
    nextWriteImageIndex_ = previousIndex;
}

void WorldPosHistoryNode::TypedCleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown. Keep persistent
    // resources ONLY across a Recompile (the device survives) -- on DeviceLost the device and
    // every child object are gone (KI-004 class), mirrors AccumulationHistoryNode's guard.
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[WorldPosHistoryNode] Cleanup (recompile) - keeping persistent worldPos history image");
        return;
    }

    NODE_LOG_INFO("[WorldPosHistoryNode] Cleanup (final teardown) - destroying worldPos history image pair");
    DestroyImages();
}

void WorldPosHistoryNode::CreateImage(VulkanDevice* device, VkCommandPool commandPool,
                                      uint32_t imageIndex) {
    VkImage& image = images_[imageIndex];
    VkDeviceMemory& memory = memories_[imageIndex];
    VkImageView& view = views_[imageIndex];
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    // --- Create R32G32B32A32_SFLOAT storage image (worldPos.xyz + hitT.w) ---
    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = kFormat;
    imgInfo.extent        = {width_, height_, 1u};
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT;  // storage only, no transfer needed
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(vkDevice, &imgInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("[WorldPosHistoryNode] vkCreateImage failed");
    }

    // --- Allocate device-local memory via real memory-type selection ---
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(vkDevice, image, &req);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = FindSuitableMemoryType(
        memProps,
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "WorldPosHistoryNode R32G32B32A32_SFLOAT image"
    );

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyImage(vkDevice, image, nullptr);
        image = VK_NULL_HANDLE;
        throw std::runtime_error("[WorldPosHistoryNode] vkAllocateMemory failed");
    }

    vkBindImageMemory(vkDevice, image, memory, 0);

    // --- Create image view ---
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image            = image;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = kFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    if (vkCreateImageView(vkDevice, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("[WorldPosHistoryNode] vkCreateImageView failed");
    }

    // One-time UNDEFINED -> GENERAL transition. Storage images stay GENERAL across dispatches.
    TransitionToGeneral(commandPool, image);

    NODE_LOG_INFO("[WorldPosHistoryNode] Created R32G32B32A32_SFLOAT storage image at " +
                  std::to_string(width_) + "x" + std::to_string(height_) +
                  " (transitioned UNDEFINED->GENERAL)");
}

void WorldPosHistoryNode::TransitionToGeneral(VkCommandPool commandPool, VkImage image) {
    VkDevice vkDevice = GetDevice()->device;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDevice, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("[WorldPosHistoryNode] vkAllocateCommandBuffers (transition) failed");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
        throw std::runtime_error("[WorldPosHistoryNode] vkBeginCommandBuffer (transition) failed");
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
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
        throw std::runtime_error("[WorldPosHistoryNode] vkEndCommandBuffer (transition) failed");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    {
        std::lock_guard submitLock(GetDevice()->SubmitMutex(GetDevice()->queue));
        if (vkQueueSubmit(GetDevice()->queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
            throw std::runtime_error("[WorldPosHistoryNode] vkQueueSubmit (transition) failed");
        }
        vkQueueWaitIdle(GetDevice()->queue);
    }

    vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
}

void WorldPosHistoryNode::DestroyImages() {
    if (!GetDevice()) return;
    VkDevice vkDevice = GetDevice()->device;

    for (uint32_t i = 0; i < 2; ++i) {
        if (views_[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(vkDevice, views_[i], nullptr);
            views_[i] = VK_NULL_HANDLE;
        }
        if (images_[i] != VK_NULL_HANDLE) {
            vkDestroyImage(vkDevice, images_[i], nullptr);
            images_[i] = VK_NULL_HANDLE;
        }
        if (memories_[i] != VK_NULL_HANDLE) {
            vkFreeMemory(vkDevice, memories_[i], nullptr);
            memories_[i] = VK_NULL_HANDLE;
        }
    }

    NODE_LOG_INFO("[WorldPosHistoryNode] worldPos history image pair destroyed");
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::WorldPosHistoryNodeType);
