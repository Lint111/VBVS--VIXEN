#include "Nodes/RenderPassNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "VulkanResources/VulkanSwapChain.h"  // For SwapChainPublicVariables definition
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "CashSystem/RenderPassCacher.h"
#include "NodeHelpers/CacherHelpers.h"
#include "NodeHelpers/EnumParsers.h"

using namespace RenderGraph::NodeHelpers;

namespace Vixen::RenderGraph {

// ====== RenderPassNodeType ======

std::unique_ptr<NodeInstance> RenderPassNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<RenderPassNode>(
        instanceName,
        const_cast<RenderPassNodeType*>(this)
    );
}

// ====== RenderPassNode ======

RenderPassNode::RenderPassNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<RenderPassNodeConfig>(instanceName, nodeType)
{
}

void RenderPassNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("RenderPassNode: Setup (graph-scope initialization)");
}

void RenderPassNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("Compile: Getting or creating cached render pass");

    // Access device input (compile-time dependency)
    VulkanDevice* devicePtr = ctx.In(RenderPassNodeConfig::VULKAN_DEVICE_IN);

    if (devicePtr == nullptr) {
        std::string errorMsg = "RenderPassNode: VkDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    // Get swapchain info bundle and extract format
    SwapChainPublicVariables* swapchainInfo = ctx.In(RenderPassNodeConfig::SWAPCHAIN_INFO);
    if (!swapchainInfo) {
        throw std::runtime_error("RenderPassNode: swapchain info bundle is null");
    }
    VkFormat colorFormat = swapchainInfo->Format;

    // Get depth format directly
    VkFormat depthFormat = ctx.In(RenderPassNodeConfig::DEPTH_FORMAT);

    // Get typed enum parameters
    AttachmentLoadOp colorLoadOp = GetParameterValue<AttachmentLoadOp>(
        RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::Clear);
    AttachmentStoreOp colorStoreOp = GetParameterValue<AttachmentStoreOp>(
        RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::Store);
    AttachmentLoadOp depthLoadOp = GetParameterValue<AttachmentLoadOp>(
        RenderPassNodeConfig::PARAM_DEPTH_LOAD_OP, AttachmentLoadOp::Clear);
    AttachmentStoreOp depthStoreOp = GetParameterValue<AttachmentStoreOp>(
        RenderPassNodeConfig::PARAM_DEPTH_STORE_OP, AttachmentStoreOp::Store);
    ImageLayout initialLayout = GetParameterValue<ImageLayout>(
        RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::Undefined);
    ImageLayout finalLayout = GetParameterValue<ImageLayout>(
        RenderPassNodeConfig::PARAM_FINAL_LAYOUT, ImageLayout::PresentSrc);
    uint32_t sampleCount = GetParameterValue<uint32_t>(
        RenderPassNodeConfig::PARAM_SAMPLES, 1);

    hasDepth = (depthFormat != VK_FORMAT_UNDEFINED);
    NODE_LOG_DEBUG("Depth attachment: " + std::string(hasDepth ? "enabled" : "disabled"));

    // Build cache parameters
    CashSystem::RenderPassCreateParams cacheParams{};
    cacheParams.colorFormat = colorFormat;
    cacheParams.samples = ParseSampleCount(sampleCount);
    cacheParams.colorLoadOp = ConvertLoadOp(colorLoadOp);
    cacheParams.colorStoreOp = ConvertStoreOp(colorStoreOp);
    cacheParams.initialLayout = ConvertImageLayout(initialLayout);
    cacheParams.finalLayout = ConvertImageLayout(finalLayout);
    cacheParams.hasDepth = hasDepth;
    cacheParams.depthFormat = depthFormat;
    cacheParams.depthLoadOp = ConvertLoadOp(depthLoadOp);
    cacheParams.depthStoreOp = ConvertStoreOp(depthStoreOp);
    cacheParams.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    cacheParams.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    cacheParams.srcAccessMask = 0;
    cacheParams.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // Register and get cacher using helper
    auto* cacher = RegisterCacherIfNeeded<
        CashSystem::RenderPassCacher,
        CashSystem::RenderPassWrapper,
        CashSystem::RenderPassCreateParams
    >(GetOwningGraph(), device, "RenderPass", true);

    // Get or create cached render pass using helper
    cachedRenderPassWrapper = GetOrCreateCached<
        CashSystem::RenderPassCacher,
        CashSystem::RenderPassWrapper
    >(cacher, cacheParams, "render pass");

    // Validate cached handle
    ValidateCachedHandle(
        cachedRenderPassWrapper->renderPass,
        "VkRenderPass",
        "render pass"
    );

    renderPass = cachedRenderPassWrapper->renderPass;

    // Set typed outputs
    ctx.Out(RenderPassNodeConfig::RENDER_PASS, renderPass);
    ctx.Out(RenderPassNodeConfig::VULKAN_DEVICE_OUT, device);

    NODE_LOG_INFO("Compile complete: Render pass retrieved from cache");
}

void RenderPassNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // No-op - render pass is created in Compile phase
}

void RenderPassNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Release cached wrapper - cacher owns VkRenderPass and destroys when appropriate
    if (cachedRenderPassWrapper) {
        NODE_LOG_DEBUG("[RenderPassNode::CleanupImpl] Releasing cached render pass wrapper (cacher owns resource)");
        cachedRenderPassWrapper.reset();
        renderPass = VK_NULL_HANDLE;
    }
}

VkAttachmentLoadOp RenderPassNode::ConvertLoadOp(AttachmentLoadOp op) {
    switch (op) {
        case AttachmentLoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
        case AttachmentLoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case AttachmentLoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    }
}

VkAttachmentStoreOp RenderPassNode::ConvertStoreOp(AttachmentStoreOp op) {
    switch (op) {
        case AttachmentStoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
        case AttachmentStoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        default: return VK_ATTACHMENT_STORE_OP_STORE;
    }
}

VkImageLayout RenderPassNode::ConvertImageLayout(ImageLayout layout) {
    switch (layout) {
        case ImageLayout::Undefined: return VK_IMAGE_LAYOUT_UNDEFINED;
        case ImageLayout::ColorAttachment: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case ImageLayout::DepthStencilAttachment: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case ImageLayout::PresentSrc: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        case ImageLayout::TransferSrc: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case ImageLayout::TransferDst: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        default: return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

} // namespace Vixen::RenderGraph
