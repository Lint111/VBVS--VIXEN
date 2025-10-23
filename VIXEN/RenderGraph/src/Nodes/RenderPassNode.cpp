#include "Nodes/RenderPassNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"

namespace Vixen::RenderGraph {

// ====== RenderPassNodeType ======

RenderPassNodeType::RenderPassNodeType(const std::string& typeName) : NodeType(typeName) {
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0;

    // No inputs

    // Output is opaque (render pass accessed via GetRenderPass())

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

void RenderPassNode::Setup() {
    NODE_LOG_INFO("Setup: Render pass node ready");
}

void RenderPassNode::Compile() {
    NODE_LOG_INFO("Compile: Creating render pass");

    // Get typed inputs
    VkFormat colorFormat = In(RenderPassNodeConfig::COLOR_FORMAT);
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

    // Setup attachments
    VkAttachmentDescription attachments[2];
    
    // Color attachment
    attachments[0] = {};
    attachments[0].format = colorFormat;
    attachments[0].samples = GetSampleCount(sampleCount);
    attachments[0].loadOp = ConvertLoadOp(colorLoadOp);
    attachments[0].storeOp = ConvertStoreOp(colorStoreOp);
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = ConvertImageLayout(initialLayout);
    attachments[0].finalLayout = ConvertImageLayout(finalLayout);
    attachments[0].flags = 0;

    // Depth attachment (if enabled)
    if (hasDepth) {
        attachments[1] = {};
        attachments[1].format = depthFormat;
        attachments[1].samples = GetSampleCount(sampleCount);
        attachments[1].loadOp = ConvertLoadOp(depthLoadOp);
        attachments[1].storeOp = ConvertStoreOp(depthStoreOp);
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[1].flags = 0;
    }

    // Attachment references
    VkAttachmentReference colorReference = {};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference = {};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.flags = 0;
    subpass.inputAttachmentCount = 0;
    subpass.pInputAttachments = nullptr;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pResolveAttachments = nullptr;
    subpass.pDepthStencilAttachment = hasDepth ? &depthReference : nullptr;
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments = nullptr;

    // Subpass dependency for layout transition
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = 0;

    // Create render pass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.pNext = nullptr;
    renderPassInfo.attachmentCount = hasDepth ? 2 : 1;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(device->device, &renderPassInfo, nullptr, &renderPass);
    if (result != VK_SUCCESS) {
        VulkanError error{result, "Failed to create render pass"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    // Set typed output (NEW VARIANT API)
    Out(RenderPassNodeConfig::RENDER_PASS, renderPass);

    NODE_LOG_INFO("Compile complete: Render pass created successfully");
}

void RenderPassNode::Execute(VkCommandBuffer commandBuffer) {
    // No-op - render pass is created in Compile phase
}

void RenderPassNode::Cleanup() {
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device->device, renderPass, nullptr);
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
