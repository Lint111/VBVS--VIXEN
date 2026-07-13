// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc4 M2: persistent DDGI probe atlas image (irradiance or visibility),
// allocated + transitioned + wired, but not yet read/written by a shader (see ProbeAtlasNode.h).

#include "Nodes/ProbeAtlasNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include <mutex>
#include <stdexcept>

// Local helper — mirrors RenderGraph::NodeHelpers::FindMemoryType from BufferHelpers.h but defined
// here to avoid emitting an inline-function COMDAT into this TU that conflicts when the same inline
// is instantiated in other TUs with different COMDAT selection (see AccumulationHistoryNode.cpp).
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
        std::string("[ProbeAtlasNode] No suitable memory type found for ") + context);
}

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ====== ProbeAtlasNodeType ======

std::unique_ptr<NodeInstance> ProbeAtlasNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<ProbeAtlasNode>(n, const_cast<ProbeAtlasNodeType*>(this));
}

// ====== ProbeAtlasNode ======

ProbeAtlasNode::ProbeAtlasNode(const std::string& n, NodeType* t)
    : TypedNode<ProbeAtlasNodeConfig>(n, t)
{
}

void ProbeAtlasNode::SetupImpl(TypedSetupContext& ctx) {
    width_  = GetParameterValue<uint32_t>(ProbeAtlasNodeConfig::PARAM_WIDTH,  0u);
    height_ = GetParameterValue<uint32_t>(ProbeAtlasNodeConfig::PARAM_HEIGHT, 0u);
    format_ = static_cast<VkFormat>(GetParameterValue<uint32_t>(
                  ProbeAtlasNodeConfig::PARAM_FORMAT,
                  static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT)));

    NODE_LOG_DEBUG("[ProbeAtlasNode] Setup: " + std::to_string(width_) + "x" +
                   std::to_string(height_) + " format=" + std::to_string(static_cast<int>(format_)));
}

void ProbeAtlasNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[ProbeAtlasNode] Compile START");

    device_ = ctx.In(ProbeAtlasNodeConfig::VULKAN_DEVICE_IN);
    if (!device_) {
        throw std::runtime_error("[ProbeAtlasNode] VULKAN_DEVICE_IN is null");
    }

    VkCommandPool commandPool = ctx.In(ProbeAtlasNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[ProbeAtlasNode] COMMAND_POOL is null");
    }

    if (width_ == 0 || height_ == 0) {
        throw std::runtime_error("[ProbeAtlasNode] WIDTH/HEIGHT parameters must be > 0 (got " +
                                 std::to_string(width_) + "x" + std::to_string(height_) + ")");
    }

    // FR-7: persistent across recompile — atlas extent/format are build-time PARAMETERS (not a
    // live resize-cascade input, see ProbeAtlasNode.h's own scope note), so no "extent changed"
    // recreate path is needed this milestone: create once, keep forever until FinalTeardown.
    if (target_.buffers.empty()) {
        CreateImage(device_, commandPool);
    } else {
        NODE_LOG_INFO("[ProbeAtlasNode] Reusing persistent atlas image across recompile");
    }

    ctx.Out(ProbeAtlasNodeConfig::PROBE_ATLAS, static_cast<IRenderTarget*>(&target_));
    // CURRENT_VIEW: raw VkImageView, mirrors RenderTargetNode::CompileImpl's own CURRENT_VIEW
    // output. Descriptor-gatherer bindings must connect THIS, not PROBE_ATLAS -- IRenderTarget has
    // no conversion_type, so a Resource holding an IRenderTarget* can never extract a VkImageView
    // for vkUpdateDescriptorSets (see ProbeAtlasNodeConfig.h's CURRENT_VIEW doc comment).
    ctx.Out(ProbeAtlasNodeConfig::CURRENT_VIEW, target_.GetCurrentView());

    NODE_LOG_INFO("[ProbeAtlasNode] Outputs published (" + std::to_string(width_) + "x" +
                  std::to_string(height_) + ")");
}

void ProbeAtlasNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Not a ring — the same persistent image is re-emitted every frame (mirrors
    // AccumulationHistoryNode::ExecuteImpl's own rationale: DDGI's hysteresis blend needs last
    // frame's atlas content still present, so currentIndex never advances).
    ctx.Out(ProbeAtlasNodeConfig::PROBE_ATLAS, static_cast<IRenderTarget*>(&target_));
    ctx.Out(ProbeAtlasNodeConfig::CURRENT_VIEW, target_.GetCurrentView());
}

void ProbeAtlasNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (KI-004 class) left stale handles
    // that crashed the first post-recovery use/teardown. Mirrors AccumulationHistoryNode's guard.
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[ProbeAtlasNode] Cleanup (recompile) - keeping persistent atlas image");
        return;
    }

    NODE_LOG_INFO("[ProbeAtlasNode] Cleanup (final teardown) - destroying atlas image");
    DestroyImage();
}

void ProbeAtlasNode::CreateImage(VulkanDevice* device, VkCommandPool commandPool) {
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

    // --- Create image ---
    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = format_;
    imgInfo.extent        = {width_, height_, 1u};
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT;  // M2: storage only, no transfer needed yet
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(vkDevice, &imgInfo, nullptr, &b.image) != VK_SUCCESS) {
        throw std::runtime_error("[ProbeAtlasNode] vkCreateImage failed");
    }

    // --- Allocate device-local memory via real memory-type selection ---
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(vkDevice, b.image, &req);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = FindSuitableMemoryType(
        memProps,
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "ProbeAtlasNode storage image"
    );

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &b.memory) != VK_SUCCESS) {
        vkDestroyImage(vkDevice, b.image, nullptr);
        b.image = VK_NULL_HANDLE;
        throw std::runtime_error("[ProbeAtlasNode] vkAllocateMemory failed");
    }

    vkBindImageMemory(vkDevice, b.image, b.memory, 0);

    // --- Create image view ---
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image            = b.image;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = format_;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    if (vkCreateImageView(vkDevice, &viewInfo, nullptr, &b.view) != VK_SUCCESS) {
        throw std::runtime_error("[ProbeAtlasNode] vkCreateImageView failed");
    }

    // One-time UNDEFINED -> GENERAL transition. Storage images stay GENERAL across dispatches, so
    // this single transition makes the descriptor (always GENERAL) correct for every frame.
    TransitionToGeneral(commandPool);

    NODE_LOG_INFO("[ProbeAtlasNode] Created storage image at " +
                  std::to_string(width_) + "x" + std::to_string(height_) +
                  " format=" + std::to_string(static_cast<int>(format_)) +
                  " (transitioned UNDEFINED->GENERAL)");
}

void ProbeAtlasNode::TransitionToGeneral(VkCommandPool commandPool) {
    VkDevice vkDevice = device_->device;

    // One-shot command buffer for the layout transition.
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDevice, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("[ProbeAtlasNode] vkAllocateCommandBuffers (transition) failed");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
        throw std::runtime_error("[ProbeAtlasNode] vkBeginCommandBuffer (transition) failed");
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
        throw std::runtime_error("[ProbeAtlasNode] vkEndCommandBuffer (transition) failed");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    // Submit on the device queue and wait — this runs once at Compile, before any dispatch.
    // Externally synchronized per Vulkan spec (audit V-M11) regardless.
    {
        std::lock_guard<std::mutex> submitLock(device_->SubmitMutex(device_->queue));
        if (vkQueueSubmit(device_->queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
            throw std::runtime_error("[ProbeAtlasNode] vkQueueSubmit (transition) failed");
        }
        vkQueueWaitIdle(device_->queue);
    }

    vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
}

void ProbeAtlasNode::DestroyImage() {
    if (!device_) return;
    VkDevice vkDevice = device_->device;

    for (auto& b : target_.buffers) {
        if (b.view   != VK_NULL_HANDLE) { vkDestroyImageView(vkDevice, b.view,   nullptr); b.view   = VK_NULL_HANDLE; }
        if (b.image  != VK_NULL_HANDLE) { vkDestroyImage    (vkDevice, b.image,  nullptr); b.image  = VK_NULL_HANDLE; }
        if (b.memory != VK_NULL_HANDLE) { vkFreeMemory      (vkDevice, b.memory, nullptr); b.memory = VK_NULL_HANDLE; }
    }
    target_.buffers.clear();

    NODE_LOG_INFO("[ProbeAtlasNode] Atlas image destroyed");
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ProbeAtlasNodeType);
