#include "Nodes/FramebufferNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables definition

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

    // Check for depth attachment
    VkImageView depthView = In(FramebufferNodeConfig::DEPTH_ATTACHMENT);
    hasDepth = (depthView != VK_NULL_HANDLE);

    NODE_LOG_DEBUG("Depth attachment: " + std::string(hasDepth ? "enabled" : "disabled"));

    // Get typed parameter
    uint32_t layers = GetParameterValue<uint32_t>(
        FramebufferNodeConfig::PARAM_LAYERS, 1);

    // Get swapchain public variables to access ALL color buffers
    SwapChainPublicVariables* swapchainInfo = In(FramebufferNodeConfig::SWAPCHAIN_INFO);
    if (!swapchainInfo) {
        throw std::runtime_error("FramebufferNode: SwapChain info is null");
    }

    size_t colorAttachmentCount = swapchainInfo->colorBuffers.size();
    if (colorAttachmentCount == 0) {
        VulkanError error{VK_ERROR_INITIALIZATION_FAILED, "No color buffers in swapchain"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    NODE_LOG_DEBUG("Creating " + std::to_string(colorAttachmentCount) + " framebuffers");
    std::cout << "[FramebufferNode::Compile] Creating " << colorAttachmentCount << " framebuffers from swapchain" << std::endl;

    // Clear any existing framebuffers
    Cleanup();

    // Resize framebuffer array
    framebuffers.resize(colorAttachmentCount);

    // Create one framebuffer per color attachment (swapchain image)
    for (size_t i = 0; i < colorAttachmentCount; i++) {
        // Get color attachment from swapchain public variables
        VkImageView colorView = swapchainInfo->colorBuffers[i].view;
        std::cout << "[FramebufferNode::Compile] Processing attachment " << i << ", view=" << colorView << std::endl;

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
        framebufferInfo.width = swapchainInfo->Extent.width;
        framebufferInfo.height = swapchainInfo->Extent.height;
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

        std::cout << "[FramebufferNode::Compile] Created framebuffer[" << i << "]=" << framebuffers[i] << std::endl;
        NODE_LOG_DEBUG("Created framebuffer " + std::to_string(i) + ": " +
                      std::to_string(reinterpret_cast<uint64_t>(framebuffers[i])));
    }

    // Output all framebuffers as a vector in ONE bundle
    Out(FramebufferNodeConfig::FRAMEBUFFERS, framebuffers);
    std::cout << "[FramebufferNode::Compile] Output " << framebuffers.size() << " framebuffers as vector" << std::endl;

    Out(FramebufferNodeConfig::VULKAN_DEVICE_OUT, vulkanDevice);

    NODE_LOG_INFO("Compile complete: Created " + std::to_string(framebuffers.size()) + " framebuffers");

    // === REGISTER CLEANUP ===
    NodeInstance::RegisterCleanup();
}

void FramebufferNode::Execute(VkCommandBuffer commandBuffer) {
    // No-op - framebuffers are created in Compile phase
}

void FramebufferNode::CleanupImpl() {
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