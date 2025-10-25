#include "Nodes/FramebufferNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"

namespace Vixen::RenderGraph {

// ====== FramebufferNodeType ======

FramebufferNodeType::FramebufferNodeType(const std::string& typeName) : NodeType(typeName) {
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited

    // Populate schemas from Config
    FramebufferNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 2048; // Minimal metadata
    workloadMetrics.estimatedComputeCost = 0.1f;
    workloadMetrics.estimatedBandwidthCost = 0.0f;
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> FramebufferNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<FramebufferNode>(instanceName, const_cast<FramebufferNodeType*>(this));
}

// ====== FramebufferNode ======

FramebufferNode::FramebufferNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<FramebufferNodeConfig>(instanceName, nodeType)
{
}

FramebufferNode::~FramebufferNode() {
    Cleanup();
}

void FramebufferNode::Setup() {
    NODE_LOG_DEBUG("Setup: Reading device input");
    
    vulkanDevice = In(FramebufferNodeConfig::VULKAN_DEVICE_IN);
    
    if (vulkanDevice == VK_NULL_HANDLE) {
        std::string errorMsg = "FramebufferNode: VkDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    
    NODE_LOG_INFO("Setup: Framebuffer node ready");
}

void FramebufferNode::Compile() {
    NODE_LOG_INFO("Compile: Creating framebuffers");

    // Get typed inputs
    VkRenderPass renderPass = In(FramebufferNodeConfig::RENDER_PASS);
    uint32_t width = In(FramebufferNodeConfig::WIDTH);
    uint32_t height = In(FramebufferNodeConfig::HEIGHT);

    // Check for depth attachment
    VkImageView depthView = In(FramebufferNodeConfig::DEPTH_ATTACHMENT);
    hasDepth = (depthView != VK_NULL_HANDLE);

    NODE_LOG_DEBUG("Depth attachment: " + std::string(hasDepth ? "enabled" : "disabled"));
    NODE_LOG_DEBUG("Framebuffer dimensions: " + std::to_string(width) + "x" + std::to_string(height));

    // Get typed parameter
    uint32_t layers = GetParameterValue<uint32_t>(
        FramebufferNodeConfig::PARAM_LAYERS, 1);

    // Get number of color attachments (array size) using base class method
    size_t colorAttachmentCount = NodeInstance::GetInputCount(FramebufferNodeConfig::COLOR_ATTACHMENTS_Slot::index);

    if (colorAttachmentCount == 0) {
        VulkanError error{VK_ERROR_INITIALIZATION_FAILED, "No color attachments provided"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    NODE_LOG_DEBUG("Creating " + std::to_string(colorAttachmentCount) + " framebuffers");

    // Clear any existing framebuffers
    Cleanup();

    // Resize framebuffer array
    framebuffers.resize(colorAttachmentCount);

    // Create one framebuffer per color attachment (swapchain image)
    for (size_t i = 0; i < colorAttachmentCount; i++) {
        // Get color attachment for this framebuffer using typed In() API
        VkImageView colorView = In(FramebufferNodeConfig::COLOR_ATTACHMENTS, i);

        // Setup attachments array
        std::vector<VkImageView> attachments;
        attachments.push_back(colorView);

        // Add depth attachment if present
        if (hasDepth) {
            attachments.push_back(depthView);
        }

        // Create framebuffer info
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.pNext = nullptr;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = layers;
        framebufferInfo.flags = 0;

        VkResult result = vkCreateFramebuffer(
            vulkanDevice->device,
            &framebufferInfo,
            nullptr,
            &framebuffers[i]
        );

        if (result != VK_SUCCESS) {
            // Clean up already created framebuffers
            for (size_t j = 0; j < i; j++) {
                if (framebuffers[j] != VK_NULL_HANDLE) {
                    vkDestroyFramebuffer(vulkanDevice->device, framebuffers[j], nullptr);
                }
            }
            framebuffers.clear();

            VulkanError error{result, "Failed to create framebuffer " + std::to_string(i)};
            NODE_LOG_ERROR(error.toString());
            throw std::runtime_error(error.toString());
        }

        // Set output - for now, store framebuffer directly
        // TODO: Implement proper typed SetOutput in TypedNode
        // For now, framebuffers are stored in the local vector and accessed via GetFramebuffer()
    }
    
    Out(FramebufferNodeConfig::VULKAN_DEVICE_OUT, vulkanDevice);

    NODE_LOG_INFO("Compile complete: Created " + std::to_string(framebuffers.size()) + " framebuffers");

    // === REGISTER CLEANUP ===
    if (GetOwningGraph()) {
        GetOwningGraph()->GetCleanupStack().Register(
            GetInstanceName() + "_Cleanup",
            [this]() { this->Cleanup(); },
            { "DeviceNode_Cleanup" }
        );
    }
}

void FramebufferNode::Execute(VkCommandBuffer commandBuffer) {
    // No-op - framebuffers are created in Compile phase
}

void FramebufferNode::Cleanup() {
    if (!framebuffers.empty() && vulkanDevice != VK_NULL_HANDLE) {
        NODE_LOG_DEBUG("Cleanup: Destroying " + std::to_string(framebuffers.size()) + " framebuffers");

        for (VkFramebuffer framebuffer : framebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(vulkanDevice->device, framebuffer, nullptr);
            }
        }
        framebuffers.clear();
    }
}

} // namespace Vixen::RenderGraph