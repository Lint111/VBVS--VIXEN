#include "Nodes/DepthBufferNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include <stdexcept>

namespace Vixen::RenderGraph {

// ====== DepthBufferNodeType ======

DepthBufferNodeType::DepthBufferNodeType(const std::string& typeName)
    : NodeType(typeName)
{
    requiredCapabilities = DeviceCapability::None;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited depth buffers

    // Populate schemas from Config
    DepthBufferNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 8 * 1024 * 1024; // ~8MB typical depth buffer
    workloadMetrics.estimatedComputeCost = 0.3f;
    workloadMetrics.estimatedBandwidthCost = 0.0f;
    workloadMetrics.canRunInParallel = true;
}

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

DepthBufferNode::~DepthBufferNode() {
    Cleanup();
}

void DepthBufferNode::Setup() {
    vulkanDevice = In(DepthBufferNodeConfig::VULKAN_DEVICE_IN);
    commandPool = In(DepthBufferNodeConfig::COMMAND_POOL);

    if (!vulkanDevice || vulkanDevice->device == VK_NULL_HANDLE) {
        std::string errorMsg = "DepthBufferNode: VulkanDevice input is null or invalid";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    if (commandPool == VK_NULL_HANDLE) {
        std::string errorMsg = "DepthBufferNode: VkCommandPool input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    NODE_LOG_INFO("DepthBufferNode setup complete");
}

void DepthBufferNode::Compile() {
    // Get inputs
    uint32_t width = In(DepthBufferNodeConfig::WIDTH);
    uint32_t height = In(DepthBufferNodeConfig::HEIGHT);

    // Get parameter for format
    DepthFormat depthFormat = GetParameterValue<DepthFormat>(
        DepthBufferNodeConfig::PARAM_FORMAT,
        DepthFormat::D32
    );

    VkFormat vkFormat = ConvertDepthFormat(depthFormat);

    // Create depth image and view
    CreateDepthImageAndView(width, height, vkFormat);

    isCreated = true;

    // Set outputs
    Out(DepthBufferNodeConfig::DEPTH_IMAGE_VIEW, depthImage.view);
    Out(DepthBufferNodeConfig::DEPTH_FORMAT, depthImage.format);

    NODE_LOG_INFO("Created depth buffer: " + std::to_string(width) + "x" + std::to_string(height));

    // Register cleanup
    if (GetOwningGraph()) {
        GetOwningGraph()->GetCleanupStack().Register(
            GetInstanceName() + "_Cleanup",
            [this]() { this->Cleanup(); },
            { "DeviceNode_Cleanup" }
        );
    }
}

void DepthBufferNode::Execute(VkCommandBuffer commandBuffer) {
    // Depth buffer creation happens in Compile phase
    // Execute is a no-op
}

void DepthBufferNode::Cleanup() {
    if (isCreated && vulkanDevice && vulkanDevice->device != VK_NULL_HANDLE) {
        if (depthImage.view != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkanDevice->device, depthImage.view, nullptr);
            depthImage.view = VK_NULL_HANDLE;
        }
        if (depthImage.image != VK_NULL_HANDLE) {
            vkDestroyImage(vulkanDevice->device, depthImage.image, nullptr);
            depthImage.image = VK_NULL_HANDLE;
        }
        if (depthImage.mem != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice->device, depthImage.mem, nullptr);
            depthImage.mem = VK_NULL_HANDLE;
        }

        isCreated = false;
        NODE_LOG_INFO("Destroyed depth buffer");
    }
}

VkFormat DepthBufferNode::ConvertDepthFormat(DepthFormat format) {
    switch (format) {
        case DepthFormat::D16:
            return VK_FORMAT_D16_UNORM;
        case DepthFormat::D32:
            return VK_FORMAT_D32_SFLOAT;
        case DepthFormat::D24S8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        default:
            return VK_FORMAT_D32_SFLOAT;
    }
}

void DepthBufferNode::CreateDepthImageAndView(uint32_t width, uint32_t height, VkFormat format) {
    depthImage.format = format;

    // Create depth image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateImage(vulkanDevice->device, &imageInfo, nullptr, &depthImage.image);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image");
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(vulkanDevice->device, depthImage.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    // Use VulkanDevice helper to find suitable memory type
    auto memTypeResult = vulkanDevice->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (!memTypeResult.has_value()) {
        vkDestroyImage(vulkanDevice->device, depthImage.image, nullptr);
        throw std::runtime_error("Failed to find suitable memory type for depth buffer");
    }

    allocInfo.memoryTypeIndex = memTypeResult.value();

    result = vkAllocateMemory(vulkanDevice->device, &allocInfo, nullptr, &depthImage.mem);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate depth image memory");
    }

    vkBindImageMemory(vulkanDevice->device, depthImage.image, depthImage.mem, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(vulkanDevice->device, &viewInfo, nullptr, &depthImage.view);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view");
    }

    // Transition to depth-stencil attachment optimal layout
    VkCommandBufferAllocateInfo cmdBufAllocInfo{};
    cmdBufAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocInfo.commandPool = commandPool;
    cmdBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(vulkanDevice->device, &cmdBufAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    TransitionImageLayout(cmdBuffer, depthImage.image,
                         VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    // Use queue from VulkanDevice
    vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanDevice->queue);

    vkFreeCommandBuffers(vulkanDevice->device, commandPool, 1, &cmdBuffer);
}

void DepthBufferNode::TransitionImageLayout(VkCommandBuffer cmdBuffer, VkImage image,
                                            VkImageLayout oldLayout, VkImageLayout newLayout) {
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

    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        throw std::runtime_error("Unsupported layout transition");
    }

    vkCmdPipelineBarrier(
        cmdBuffer,
        srcStage, dstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

} // namespace Vixen::RenderGraph
