#include "Nodes/DepthBufferNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "Data/Core/ResourceV3.h"
#include "NodeHelpers/VulkanStructHelpers.h"
#include "NodeHelpers/ValidationHelpers.h"
#include "VulkanSwapChain.h"
#include <sstream>

using namespace RenderGraph::NodeHelpers;


namespace Vixen::RenderGraph {

// ====== DepthBufferNodeType ======

std::unique_ptr<NodeInstance> DepthBufferNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<DepthBufferNode>(
        instanceName,
        const_cast<NodeType*>(static_cast<const NodeType*>(this))
    );
}

// ====== DepthBufferNode ======

DepthBufferNode::DepthBufferNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<DepthBufferNodeConfig>(instanceName, nodeType)
{
}

void DepthBufferNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("DepthBufferNode: Setup (graph-scope initialization)");
}

void DepthBufferNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("Compile: Creating depth buffer");

    // Validate and set device using helper
    vulkanDevice = ValidateInput<VulkanDevicePtr>(ctx, "VulkanDevice", DepthBufferNodeConfig::VULKAN_DEVICE_IN);
    deviceHandle = vulkanDevice;  // Keep for legacy code

    // Helper macro for VkDevice
    #define VK_DEVICE (vulkanDevice->device)

    // Validate swapchain variables and command pool
    SwapChainPublicVariablesPtr swapChainVars = ValidateInput<SwapChainPublicVariablesPtr>(
        ctx, "SwapChainPublicVars", DepthBufferNodeConfig::SWAPCHAIN_PUBLIC_VARS);
    VkCommandPool cmdPool = ValidateInput<VkCommandPool>(
        ctx, "CommandPool", DepthBufferNodeConfig::COMMAND_POOL);

    uint32_t width = swapChainVars->Extent.width;
    uint32_t height = swapChainVars->Extent.height;

    NODE_LOG_DEBUG("Input dimensions: " + std::to_string(width) + "x" + std::to_string(height));

    // Validate dimensions
    if (width == 0 || height == 0) {
        throw std::runtime_error("Invalid dimensions: width and height must be greater than 0");
    }
    if (cmdPool == VK_NULL_HANDLE) {
        throw std::runtime_error("Command pool is null");
    }

    // Get format from typed parameter (defaults to D32)
    DepthFormat depthFmt = GetParameterValue<DepthFormat>(
        DepthBufferNodeConfig::PARAM_FORMAT,
        DepthFormat::D32
    );
    VkFormat format = ConvertDepthFormat(depthFmt);

    NODE_LOG_DEBUG("Using depth format: " + std::to_string(static_cast<int>(depthFmt)));

    // Create depth image and view
    CreateDepthImageAndView(width, height, format);

    // Transition depth image to optimal layout
    TransitionDepthImageLayout(cmdPool);

    #undef VK_DEVICE

    // Set typed outputs (NEW VARIANT API)
    ctx.Out(DepthBufferNodeConfig::DEPTH_IMAGE, depthImage.image);
    ctx.Out(DepthBufferNodeConfig::DEPTH_IMAGE_VIEW, depthImage.view);
    ctx.Out(DepthBufferNodeConfig::DEPTH_FORMAT, depthImage.format);

    isCreated = true;
    NODE_LOG_INFO("Compile complete: Depth buffer created successfully");
}

void DepthBufferNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // No-op - depth buffer is created in Compile phase
}

void DepthBufferNode::CleanupImpl(TypedCleanupContext& ctx) {
    if (isCreated) {
        VkDevice device = vulkanDevice ? vulkanDevice->device : VK_NULL_HANDLE;
        if (!device) return;
        
        if (depthImage.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, depthImage.view, nullptr);
        }

        if (depthImage.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, depthImage.image, nullptr);
        }

        if (depthImage.mem != VK_NULL_HANDLE) {
            vkFreeMemory(device, depthImage.mem, nullptr);
        }

        isCreated = false;
    }
}

VkFormat DepthBufferNode::ConvertDepthFormat(DepthFormat format) {
    switch (format) {
        case DepthFormat::D16:
            return VK_FORMAT_D16_UNORM;
        case DepthFormat::D24S8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case DepthFormat::D32:
            return VK_FORMAT_D32_SFLOAT;
        default:
            return VK_FORMAT_D32_SFLOAT;
    }
}

void DepthBufferNode::CreateDepthImageAndView(uint32_t width, uint32_t height, VkFormat format) {

    depthImage.format = format;
    VkDevice device = vulkanDevice->device;

    // Create image using helper
    VkImageCreateInfo imageInfo = CreateImageInfo(
        VK_IMAGE_TYPE_2D,
        format,
        VkExtent3D{width, height, 1},
        1,  // mipLevels
        1,  // arrayLayers
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
    );

    VkResult result = vkCreateImage(device, &imageInfo, nullptr, &depthImage.image);
    ValidateVulkanResult(result, "Depth image creation");
    NODE_LOG_DEBUG("Depth image created");

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, depthImage.image, &memRequirements);

    // TODO: Memory type selection should come from device node or helper
    uint32_t memoryTypeIndex = 0; // Placeholder
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(device, &allocInfo, nullptr, &depthImage.mem);
    if (result != VK_SUCCESS) {
        vkDestroyImage(device, depthImage.image, nullptr);
        VulkanError error{result, "Failed to allocate depth image memory"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }
    NODE_LOG_DEBUG("Depth image memory allocated");

    vkBindImageMemory(device, depthImage.image, depthImage.mem, 0);

    // Create image view using helper
    VkImageViewCreateInfo viewInfo = CreateImageViewInfo(
        depthImage.image,
        VK_IMAGE_VIEW_TYPE_2D,
        format,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        1,  // mipLevels
        1   // arrayLayers
    );

    result = vkCreateImageView(device, &viewInfo, nullptr, &depthImage.view);
    ValidateVulkanResult(result, "Depth image view creation");
    NODE_LOG_DEBUG("Depth image view created");
}

void DepthBufferNode::TransitionDepthImageLayout(VkCommandPool cmdPool) {
    VkDevice device = vulkanDevice->device;

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &cmdBuffer);

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Transition layout
    TransitionImageLayout(
        cmdBuffer,
        depthImage.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    );

    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanDevice->queue);

    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
}

void DepthBufferNode::TransitionImageLayout(
    VkCommandBuffer cmdBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        std::string errorMsg = "Unsupported layout transition";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    vkCmdPipelineBarrier(
        cmdBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

} // namespace Vixen::RenderGraph
