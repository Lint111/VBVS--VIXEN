#include "RenderGraph/Nodes/FramebufferNode.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// ====== FramebufferNodeType ======

FramebufferNodeType::FramebufferNodeType() {
    typeId = 105; // Unique ID
    typeName = "Framebuffer";
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited

    // Inputs are opaque references (set via Set methods)

    // Outputs are opaque (accessed via Get methods)

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 2048; // Minimal metadata
    workloadMetrics.estimatedComputeCost = 0.1f;
    workloadMetrics.estimatedBandwidthCost = 0.0f;
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> FramebufferNodeType::CreateInstance(
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
) const {
    return std::make_unique<FramebufferNode>(
        instanceName,
        const_cast<FramebufferNodeType*>(this),
        device
    );
}

// ====== FramebufferNode ======

FramebufferNode::FramebufferNode(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
)
    : NodeInstance(instanceName, nodeType, device)
{
}

FramebufferNode::~FramebufferNode() {
    Cleanup();
}

void FramebufferNode::Setup() {
    // No setup needed
}

void FramebufferNode::Compile() {
    // Get parameters
    width = GetParameterValue<uint32_t>("width", 0);
    height = GetParameterValue<uint32_t>("height", 0);
    
    if (width == 0 || height == 0) {
        throw std::runtime_error("FramebufferNode: width and height parameters are required");
    }

    layers = GetParameterValue<uint32_t>("layers", 1);
    includeDepth = GetParameterValue<bool>("includeDepth", true);
    framebufferCount = GetParameterValue<uint32_t>("framebufferCount", 1);

    // Validate inputs
    if (renderPass == VK_NULL_HANDLE) {
        throw std::runtime_error("FramebufferNode: render pass not set");
    }

    if (colorAttachments.empty()) {
        throw std::runtime_error("FramebufferNode: no color attachments set");
    }

    if (includeDepth && depthAttachment == VK_NULL_HANDLE) {
        throw std::runtime_error("FramebufferNode: depth attachment required but not set");
    }

    // Ensure we have the right number of color attachments
    if (colorAttachments.size() < framebufferCount) {
        throw std::runtime_error("FramebufferNode: not enough color attachments for framebuffer count");
    }

    // Create framebuffers
    CreateFramebuffers();
}

void FramebufferNode::Execute(VkCommandBuffer commandBuffer) {
    // Framebuffer creation happens in Compile phase
    // Execute is a no-op for this node
}

void FramebufferNode::Cleanup() {
    VkDevice vkDevice = device->device;

    for (VkFramebuffer framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(vkDevice, framebuffer, nullptr);
        }
    }
    framebuffers.clear();
}

void FramebufferNode::CreateFramebuffers() {
    VkDevice vkDevice = device->device;

    // Clear any existing framebuffers
    Cleanup();

    // Resize to hold all framebuffers
    framebuffers.resize(framebufferCount);

    // Create framebuffers
    for (uint32_t i = 0; i < framebufferCount; i++) {
        // Setup attachments array
        std::vector<VkImageView> attachments;
        
        // Add color attachment for this framebuffer
        attachments.push_back(colorAttachments[i]);
        
        // Add depth attachment if enabled
        if (includeDepth) {
            attachments.push_back(depthAttachment);
        }

        // Create framebuffer info
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.pNext = nullptr;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = layers;

        VkResult result = vkCreateFramebuffer(
            vkDevice,
            &framebufferInfo,
            nullptr,
            &framebuffers[i]
        );

        if (result != VK_SUCCESS) {
            // Clean up any framebuffers created so far
            for (uint32_t j = 0; j < i; j++) {
                if (framebuffers[j] != VK_NULL_HANDLE) {
                    vkDestroyFramebuffer(vkDevice, framebuffers[j], nullptr);
                }
            }
            framebuffers.clear();
            throw std::runtime_error("Failed to create framebuffer " + std::to_string(i));
        }
    }
}

// Setter methods for input references
void FramebufferNode::SetRenderPass(VkRenderPass pass) {
    renderPass = pass;
}

void FramebufferNode::SetColorAttachments(const std::vector<VkImageView>& colorViews) {
    colorAttachments = colorViews;
}

void FramebufferNode::SetDepthAttachment(VkImageView depthView) {
    depthAttachment = depthView;
}

} // namespace Vixen::RenderGraph
