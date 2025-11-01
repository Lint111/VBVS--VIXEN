#include "Nodes/RenderPassNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables definition
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "CashSystem/RenderPassCacher.h"

namespace Vixen::RenderGraph {

// ====== RenderPassNodeType ======

RenderPassNodeType::RenderPassNodeType(const std::string& typeName) : NodeType(typeName) {
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0;

    // Populate schemas from Config
    RenderPassNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 1024; // Minimal
    workloadMetrics.estimatedComputeCost = 0.05f;
    workloadMetrics.estimatedBandwidthCost = 0.0f;
    workloadMetrics.canRunInParallel = true;
}

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

RenderPassNode::~RenderPassNode() {
    Cleanup();
}

void RenderPassNode::SetupImpl() {
    VulkanDevicePtr devicePtr = In(RenderPassNodeConfig::VULKAN_DEVICE_IN);

    if (devicePtr == nullptr) {
        std::string errorMsg = "RenderPassNode: VkDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    NODE_LOG_INFO("Setup: Render pass node ready");
}

void RenderPassNode::CompileImpl() {
    NODE_LOG_INFO("Compile: Getting or creating cached render pass");

    // Get swapchain info bundle and extract format
    SwapChainPublicVariables* swapchainInfo = In(RenderPassNodeConfig::SWAPCHAIN_INFO);
    if (!swapchainInfo) {
        throw std::runtime_error("RenderPassNode: swapchain info bundle is null");
    }
    VkFormat colorFormat = swapchainInfo->Format;

    // Get depth format directly
    VkFormat depthFormat = In(RenderPassNodeConfig::DEPTH_FORMAT);

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
    cacheParams.samples = GetSampleCount(sampleCount);
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

    // Register RenderPassCacher if not already registered
    auto& mainCacher = GetOwningGraph()->GetMainCacher();
    if (!mainCacher.IsRegistered(std::type_index(typeid(CashSystem::RenderPassWrapper)))) {
        NODE_LOG_INFO("Registering RenderPassCacher with MainCacher");
        mainCacher.RegisterCacher<
            CashSystem::RenderPassCacher,
            CashSystem::RenderPassWrapper,
            CashSystem::RenderPassCreateParams
        >(
            std::type_index(typeid(CashSystem::RenderPassWrapper)),
            "RenderPass",
            true  // device-dependent
        );
    }

    // Get or create cached render pass
    auto* cacher = mainCacher.GetCacher<
        CashSystem::RenderPassCacher,
        CashSystem::RenderPassWrapper,
        CashSystem::RenderPassCreateParams
    >(std::type_index(typeid(CashSystem::RenderPassWrapper)), device);

    if (!cacher) {
        throw std::runtime_error("RenderPassNode: Failed to get RenderPassCacher from MainCacher");
    }

    cachedRenderPassWrapper = cacher->GetOrCreate(cacheParams);

    if (!cachedRenderPassWrapper || cachedRenderPassWrapper->renderPass == VK_NULL_HANDLE) {
        throw std::runtime_error("RenderPassNode: Failed to get or create render pass from cache");
    }

    renderPass = cachedRenderPassWrapper->renderPass;

    // Set typed outputs
    Out(RenderPassNodeConfig::RENDER_PASS, renderPass);
    Out(RenderPassNodeConfig::VULKAN_DEVICE_OUT, device);

    NODE_LOG_INFO("Compile complete: Render pass retrieved from cache");
}

void RenderPassNode::ExecuteImpl() {
    // No-op - render pass is created in Compile phase
}

void RenderPassNode::CleanupImpl() {
    // Release cached wrapper - cacher owns VkRenderPass and destroys when appropriate
    if (cachedRenderPassWrapper) {
        std::cout << "[RenderPassNode::CleanupImpl] Releasing cached render pass wrapper (cacher owns resource)" << std::endl;
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

VkSampleCountFlagBits RenderPassNode::GetSampleCount(uint32_t samples) {
    switch (samples) {
        case 1: return VK_SAMPLE_COUNT_1_BIT;
        case 2: return VK_SAMPLE_COUNT_2_BIT;
        case 4: return VK_SAMPLE_COUNT_4_BIT;
        case 8: return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;
        default: return VK_SAMPLE_COUNT_1_BIT;
    }
}

} // namespace Vixen::RenderGraph
