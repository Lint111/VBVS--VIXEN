// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Deep-Field Mip-Accessor Policy, batch 29 stream C: sky-sphere cache scaffold (UNWIRED, see
// SkySphereNode.h for full scope). Mechanically mirrors ProbeAtlasNode.cpp's allocate/transition/
// publish/cleanup shape — same persistence discipline, same one-shot GENERAL transition.

#include "Nodes/SkySphereNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include <mutex>
#include <stdexcept>

// Local helper — mirrors ProbeAtlasNode.cpp's own local FindSuitableMemoryType (kept per-TU
// rather than shared, same COMDAT-conflict rationale that file documents).
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
        std::string("[SkySphereNode] No suitable memory type found for ") + context);
}

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ====== SkySphereNodeType ======

std::unique_ptr<NodeInstance> SkySphereNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<SkySphereNode>(n, const_cast<SkySphereNodeType*>(this));
}

// ====== SkySphereNode ======

SkySphereNode::SkySphereNode(const std::string& n, NodeType* t)
    : TypedNode<SkySphereNodeConfig>(n, t)
{
}

void SkySphereNode::SetupImpl(TypedSetupContext& ctx) {
    width_  = GetParameterValue<uint32_t>(SkySphereNodeConfig::PARAM_WIDTH,  0u);
    height_ = GetParameterValue<uint32_t>(SkySphereNodeConfig::PARAM_HEIGHT, 0u);
    format_ = static_cast<VkFormat>(GetParameterValue<uint32_t>(
                  SkySphereNodeConfig::PARAM_FORMAT,
                  static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT)));
    refreshCadenceFrames_ = GetParameterValue<uint32_t>(
                  SkySphereNodeConfig::PARAM_REFRESH_CADENCE_FRAMES, 0u);

    NODE_LOG_DEBUG("[SkySphereNode] Setup: " + std::to_string(width_) + "x" +
                   std::to_string(height_) + " format=" + std::to_string(static_cast<int>(format_)) +
                   " refreshCadenceFrames=" + std::to_string(refreshCadenceFrames_));
}

void SkySphereNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[SkySphereNode] Compile START");

    device_ = ctx.In(SkySphereNodeConfig::VULKAN_DEVICE_IN);
    if (!device_) {
        throw std::runtime_error("[SkySphereNode] VULKAN_DEVICE_IN is null");
    }

    VkCommandPool commandPool = ctx.In(SkySphereNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[SkySphereNode] COMMAND_POOL is null");
    }

    if (width_ == 0 || height_ == 0) {
        throw std::runtime_error("[SkySphereNode] WIDTH/HEIGHT parameters must be > 0 (got " +
                                 std::to_string(width_) + "x" + std::to_string(height_) + ")");
    }

    // Persistent across recompile — same build-time-parameter rationale ProbeAtlasNode.h
    // documents (no live per-frame resize cascade to subscribe to).
    if (target_.buffers.empty()) {
        CreateImage(device_, commandPool);
    } else {
        NODE_LOG_INFO("[SkySphereNode] Reusing persistent sky-sphere image across recompile");
    }

    ctx.Out(SkySphereNodeConfig::SKY_SPHERE, static_cast<IRenderTarget*>(&target_));
    // CURRENT_VIEW: raw VkImageView passthrough — mirrors ProbeAtlasNode's own CURRENT_VIEW
    // output; a descriptor-gatherer binding must connect this, not SKY_SPHERE (see
    // SkySphereNodeConfig.h's doc comment for the conversion_type reason).
    ctx.Out(SkySphereNodeConfig::CURRENT_VIEW, target_.GetCurrentView());

    NODE_LOG_INFO("[SkySphereNode] Outputs published (" + std::to_string(width_) + "x" +
                  std::to_string(height_) + ")");
}

void SkySphereNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Scaffold: pure passthrough, no accumulate-pass dispatch this slice (see file header —
    // wiring is next batch). dirty_/refreshCadenceFrames_ are carried but not consulted yet.
    ctx.Out(SkySphereNodeConfig::SKY_SPHERE, static_cast<IRenderTarget*>(&target_));
    ctx.Out(SkySphereNodeConfig::CURRENT_VIEW, target_.GetCurrentView());
}

void SkySphereNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final teardown — mirrors ProbeAtlasNode's own
    // KI-004-class guard (stale handles after DeviceLost must not be kept).
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[SkySphereNode] Cleanup (recompile) - keeping persistent sky-sphere image");
        return;
    }

    NODE_LOG_INFO("[SkySphereNode] Cleanup (final teardown) - destroying sky-sphere image");
    DestroyImage();
}

void SkySphereNode::CreateImage(VulkanDevice* device, VkCommandPool commandPool) {
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    target_.format          = format_;
    target_.extent          = {width_, height_};
    target_.imageUsageFlags = VK_IMAGE_USAGE_STORAGE_BIT;
    target_.currentIndex    = 0;
    target_.buffers.resize(1);

    RenderTargetBuffer& b = target_.buffers[0];

    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = format_;
    imgInfo.extent        = {width_, height_, 1u};
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT;  // scaffold: storage only, no transfer needed yet
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(vkDevice, &imgInfo, nullptr, &b.image) != VK_SUCCESS) {
        throw std::runtime_error("[SkySphereNode] vkCreateImage failed");
    }

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(vkDevice, b.image, &req);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = FindSuitableMemoryType(
        memProps,
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "SkySphereNode storage image"
    );

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &b.memory) != VK_SUCCESS) {
        vkDestroyImage(vkDevice, b.image, nullptr);
        b.image = VK_NULL_HANDLE;
        throw std::runtime_error("[SkySphereNode] vkAllocateMemory failed");
    }

    vkBindImageMemory(vkDevice, b.image, b.memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image            = b.image;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = format_;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    if (vkCreateImageView(vkDevice, &viewInfo, nullptr, &b.view) != VK_SUCCESS) {
        throw std::runtime_error("[SkySphereNode] vkCreateImageView failed");
    }

    // One-time UNDEFINED -> GENERAL transition; storage images stay GENERAL thereafter.
    TransitionToGeneral(commandPool);

    NODE_LOG_INFO("[SkySphereNode] Created octahedral sky image at " +
                  std::to_string(width_) + "x" + std::to_string(height_) +
                  " format=" + std::to_string(static_cast<int>(format_)) +
                  " (transitioned UNDEFINED->GENERAL)");
}

void SkySphereNode::TransitionToGeneral(VkCommandPool commandPool) {
    VkDevice vkDevice = device_->device;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDevice, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("[SkySphereNode] vkAllocateCommandBuffers (transition) failed");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
        throw std::runtime_error("[SkySphereNode] vkBeginCommandBuffer (transition) failed");
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = target_.buffers[0].image;
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
        throw std::runtime_error("[SkySphereNode] vkEndCommandBuffer (transition) failed");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    {
        std::lock_guard<std::mutex> submitLock(device_->SubmitMutex(device_->queue));
        if (vkQueueSubmit(device_->queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
            throw std::runtime_error("[SkySphereNode] vkQueueSubmit (transition) failed");
        }
        vkQueueWaitIdle(device_->queue);
    }

    vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
}

void SkySphereNode::DestroyImage() {
    if (!device_) return;
    VkDevice vkDevice = device_->device;

    for (auto& b : target_.buffers) {
        if (b.view   != VK_NULL_HANDLE) { vkDestroyImageView(vkDevice, b.view,   nullptr); b.view   = VK_NULL_HANDLE; }
        if (b.image  != VK_NULL_HANDLE) { vkDestroyImage    (vkDevice, b.image,  nullptr); b.image  = VK_NULL_HANDLE; }
        if (b.memory != VK_NULL_HANDLE) { vkFreeMemory      (vkDevice, b.memory, nullptr); b.memory = VK_NULL_HANDLE; }
    }
    target_.buffers.clear();

    NODE_LOG_INFO("[SkySphereNode] Sky-sphere image destroyed");
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::SkySphereNodeType);
