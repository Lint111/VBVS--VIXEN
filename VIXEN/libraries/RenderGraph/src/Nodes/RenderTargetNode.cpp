#include "Nodes/RenderTargetNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

// Default frames-in-flight when not specified by the user.
// Matches FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT (= 4).
static constexpr uint32_t DEFAULT_FRAMES_IN_FLIGHT = 4;

// Local helper — mirrors RenderGraph::NodeHelpers::FindMemoryType from BufferHelpers.h
// but defined here to avoid emitting inline-function COMDAT into this TU that conflicts
// when the same inline is instantiated in other TUs with different COMDAT selection.
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
        std::string("[RenderTargetNode] No suitable memory type found for ") + context);
}

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ====== RenderTargetNodeType ======

std::unique_ptr<NodeInstance> RenderTargetNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<RenderTargetNode>(n, const_cast<RenderTargetNodeType*>(this));
}

// ====== RenderTargetNode ======

RenderTargetNode::RenderTargetNode(const std::string& n, NodeType* t)
    : TypedNode<RenderTargetNodeConfig>(n, t)
{
}

void RenderTargetNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[RenderTargetNode] Setup START");

    width_  = GetParameterValue<uint32_t>(RenderTargetNodeConfig::PARAM_WIDTH,  512);
    height_ = GetParameterValue<uint32_t>(RenderTargetNodeConfig::PARAM_HEIGHT, 512);
    format_ = static_cast<VkFormat>(GetParameterValue<uint32_t>(
                  RenderTargetNodeConfig::PARAM_FORMAT,
                  static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_UNORM)));
    imageCount_ = GetParameterValue<uint32_t>(RenderTargetNodeConfig::PARAM_IMAGE_COUNT, 0u);
    usage_  = static_cast<VkImageUsageFlags>(GetParameterValue<uint32_t>(
                  RenderTargetNodeConfig::PARAM_USAGE,
                  static_cast<uint32_t>(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT)));
    scale_  = GetParameterValue<float>(RenderTargetNodeConfig::PARAM_SCALE, 1.0f);

    NODE_LOG_INFO("[RenderTargetNode] Setup complete: " +
                  std::to_string(width_) + "x" + std::to_string(height_));
}

VkExtent2D RenderTargetNode::ComputeFollowExtent(VkExtent2D source, float scale) {
    // Clamp scale to (0,1]: 0 or negative would produce a degenerate/zero-area target.
    float clampedScale = std::clamp(scale, std::numeric_limits<float>::min(), 1.0f);
    uint32_t w = static_cast<uint32_t>(std::ceil(static_cast<double>(source.width)  * clampedScale));
    uint32_t h = static_cast<uint32_t>(std::ceil(static_cast<double>(source.height) * clampedScale));
    return { std::max(w, 1u), std::max(h, 1u) };
}

void RenderTargetNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[RenderTargetNode] Compile START");

    device_ = ctx.In(RenderTargetNodeConfig::VULKAN_DEVICE_IN);
    if (!device_) {
        throw std::runtime_error("[RenderTargetNode] VULKAN_DEVICE_IN is null");
    }

    // Follow-swapchain mode: EXTENT_SOURCE connected means this node is a transitive dependent
    // of whatever publishes it (typically SwapChainNode) and is recompiled through the standard
    // resize cascade — re-derive width_/height_ here every Compile.
    IRenderTarget* extentSource = ctx.In(RenderTargetNodeConfig::EXTENT_SOURCE);
    bool extentChanged = false;
    if (extentSource) {
        VkExtent2D computed = ComputeFollowExtent(extentSource->GetExtent(), scale_);
        extentChanged = (computed.width != width_) || (computed.height != height_);
        width_  = computed.width;
        height_ = computed.height;
    }

    // imageCount_ == 0 means "derive from the engine default frames-in-flight"
    if (imageCount_ == 0) {
        imageCount_ = DEFAULT_FRAMES_IN_FLIGHT;
    }

    // FR-7: images are persistent across recompile — only (re)create when first-time or the
    // computed extent actually changed.
    if (target_.buffers.empty()) {
        CreateTarget(device_);
    } else if (extentChanged) {
        NODE_LOG_INFO("[RenderTargetNode] EXTENT_SOURCE extent changed — recreating offscreen target at " +
                      std::to_string(width_) + "x" + std::to_string(height_));
        DestroyTarget();
        CreateTarget(device_);
    } else {
        NODE_LOG_INFO("[RenderTargetNode] Reusing persistent offscreen target across recompile");
    }

    ctx.Out(RenderTargetNodeConfig::RENDER_TARGET, static_cast<IRenderTarget*>(&target_));
    ctx.Out(RenderTargetNodeConfig::CURRENT_VIEW,  target_.GetCurrentView());
    ctx.Out(RenderTargetNodeConfig::WIDTH_OUT,     width_);
    ctx.Out(RenderTargetNodeConfig::HEIGHT_OUT,    height_);

    NODE_LOG_INFO("[RenderTargetNode] Outputs published (width=" + std::to_string(width_) +
                  ", height=" + std::to_string(height_) + ")");
}

void RenderTargetNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Advance the in-flight index so consumers read/write the right buffer this frame.
    if (target_.GetImageCount() > 0) {
        target_.currentIndex = (target_.currentIndex + 1) % target_.GetImageCount();
    }

    // CompileImpl publishes CURRENT_VIEW once, frozen at whatever ring slot currentIndex was at
    // compile time. DescriptorSetNode binds binding 0 from that value every frame (it never
    // re-reads RENDER_TARGET/GetCurrentView() itself), while ComputeDispatchNode's barrier and
    // dispatch resolve the image via the live IRenderTarget*/GetCurrentImage() above — so without
    // republishing here the descriptor's bound view and the barrier/dispatch's actual image are
    // the same physical ring slot on at most one frame in every imageCount, and mismatched
    // (wrong layout at draw/dispatch time — VUID-vkCmdDraw-None-09600, visible as flicker,
    // KI-009) on every other frame.
    ctx.Out(RenderTargetNodeConfig::CURRENT_VIEW, target_.GetCurrentView());
}

void RenderTargetNode::CleanupImpl(TypedCleanupContext& ctx) {
    // FR-7: persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (the old '!= FinalTeardown' guard)
    // left stale handles that crashed the first post-recovery use/teardown (KI-004 class).
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[RenderTargetNode] Cleanup (recompile) - keeping persistent offscreen target");
        return;
    }

    NODE_LOG_INFO("[RenderTargetNode] Cleanup (final teardown) - destroying offscreen target");
    DestroyTarget();
}

void RenderTargetNode::CreateTarget(VulkanDevice* device) {
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    target_.format          = format_;
    target_.extent          = {width_, height_};
    target_.imageUsageFlags = usage_;
    target_.currentIndex    = 0;
    target_.buffers.resize(imageCount_);

    for (auto& b : target_.buffers) {
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
        imgInfo.usage         = usage_;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(vkDevice, &imgInfo, nullptr, &b.image) != VK_SUCCESS) {
            throw std::runtime_error("[RenderTargetNode] vkCreateImage failed");
        }

        // --- Allocate device-local memory via FindMemoryType (real, not a placeholder) ---
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(vkDevice, b.image, &req);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = req.size;
        allocInfo.memoryTypeIndex = FindSuitableMemoryType(
            memProps,
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "RenderTargetNode color image"
        );

        if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &b.memory) != VK_SUCCESS) {
            vkDestroyImage(vkDevice, b.image, nullptr);
            b.image = VK_NULL_HANDLE;
            throw std::runtime_error("[RenderTargetNode] vkAllocateMemory failed");
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
            throw std::runtime_error("[RenderTargetNode] vkCreateImageView failed");
        }
    }

    NODE_LOG_INFO("[RenderTargetNode] Created " + std::to_string(imageCount_) +
                  " offscreen color targets at " +
                  std::to_string(width_) + "x" + std::to_string(height_));
}

void RenderTargetNode::DestroyTarget() {
    if (!device_) return;
    VkDevice vkDevice = device_->device;

    for (auto& b : target_.buffers) {
        if (b.view   != VK_NULL_HANDLE) { vkDestroyImageView(vkDevice, b.view,   nullptr); b.view   = VK_NULL_HANDLE; }
        if (b.image  != VK_NULL_HANDLE) { vkDestroyImage    (vkDevice, b.image,  nullptr); b.image  = VK_NULL_HANDLE; }
        if (b.memory != VK_NULL_HANDLE) { vkFreeMemory      (vkDevice, b.memory, nullptr); b.memory = VK_NULL_HANDLE; }
    }
    target_.buffers.clear();

    NODE_LOG_INFO("[RenderTargetNode] Offscreen target destroyed");
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::RenderTargetNodeType);
