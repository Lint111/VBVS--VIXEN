#include "RenderGraph/Nodes/DepthBufferNode.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// ====== DepthBufferNodeType ======

DepthBufferNodeType::DepthBufferNodeType() {
    typeId = 101; // Unique ID
    typeName = "DepthBuffer";
    pipelineType = PipelineType::Transfer;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0;

    // No inputs

    // Output: Depth image
    ImageDescription depthOutput{};
    depthOutput.width = 1920; // Default, will be overridden
    depthOutput.height = 1080;
    depthOutput.depth = 1;
    depthOutput.mipLevels = 1;
    depthOutput.arrayLayers = 1;
    depthOutput.format = VK_FORMAT_D32_SFLOAT;
    depthOutput.samples = VK_SAMPLE_COUNT_1_BIT;
    depthOutput.usage = ResourceUsage::DepthStencilAttachment;
    depthOutput.tiling = VK_IMAGE_TILING_OPTIMAL;

    outputSchema.push_back(ResourceDescriptor(
        "depthImage",
        ResourceType::Image,
        ResourceLifetime::Transient, // Depth buffers are typically transient
        depthOutput
    ));

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 1920 * 1080 * 4; // ~8MB for D32
    workloadMetrics.estimatedComputeCost = 0.1f; // Just allocation
    workloadMetrics.estimatedBandwidthCost = 0.1f;
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> DepthBufferNodeType::CreateInstance(
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
) const {
    return std::make_unique<DepthBufferNode>(
        instanceName,
        const_cast<DepthBufferNodeType*>(this),
        device
    );
}

// ====== DepthBufferNode ======

DepthBufferNode::DepthBufferNode(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
)
    : NodeInstance(instanceName, nodeType, device)
{
}

DepthBufferNode::~DepthBufferNode() {
    Cleanup();
}

void DepthBufferNode::Setup() {
    // Create command pool for image layout transition
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device->graphicsQueueIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(device->device, &poolInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool for depth buffer");
    }
}

void DepthBufferNode::Compile() {
    // Get parameters
    uint32_t width = GetParameterValue<uint32_t>("width", 1920);
    uint32_t height = GetParameterValue<uint32_t>("height", 1080);
    std::string formatStr = GetParameterValue<std::string>("format", "D32");

    VkFormat format = GetFormatFromString(formatStr);

    // Create depth image and view
    CreateDepthImageAndView(width, height, format);

    // Transition to depth-stencil attachment optimal layout
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(device->device, &allocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    TransitionImageLayout(cmdBuffer, depthImage.image,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(device->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->queue);

    vkFreeCommandBuffers(device->device, commandPool, 1, &cmdBuffer);

    isCreated = true;
}

void DepthBufferNode::Execute(VkCommandBuffer commandBuffer) {
    // No-op - depth buffer is created in Compile phase
}

void DepthBufferNode::Cleanup() {
    if (isCreated) {
        VkDevice vkDevice = device->device;

        if (depthImage.view != VK_NULL_HANDLE) {
            vkDestroyImageView(vkDevice, depthImage.view, nullptr);
            depthImage.view = VK_NULL_HANDLE;
        }

        if (depthImage.image != VK_NULL_HANDLE) {
            vkDestroyImage(vkDevice, depthImage.image, nullptr);
            depthImage.image = VK_NULL_HANDLE;
        }

        if (depthImage.mem != VK_NULL_HANDLE) {
            vkFreeMemory(vkDevice, depthImage.mem, nullptr);
            depthImage.mem = VK_NULL_HANDLE;
        }

        isCreated = false;
    }

    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device->device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }
}

VkFormat DepthBufferNode::GetFormatFromString(const std::string& formatStr) {
    if (formatStr == "D32") {
        return VK_FORMAT_D32_SFLOAT;
    } else if (formatStr == "D24S8") {
        return VK_FORMAT_D24_UNORM_S8_UINT;
    } else if (formatStr == "D16") {
        return VK_FORMAT_D16_UNORM;
    }
    return VK_FORMAT_D32_SFLOAT; // Default
}

void DepthBufferNode::CreateDepthImageAndView(uint32_t width, uint32_t height, VkFormat format) {
    depthImage.format = format;

    // Create image
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

    VkResult result = vkCreateImage(device->device, &imageInfo, nullptr, &depthImage.image);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image");
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device->device, depthImage.image, &memRequirements);

    auto memoryTypeResult = device->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (!memoryTypeResult) {
        vkDestroyImage(device->device, depthImage.image, nullptr);
        throw std::runtime_error("Failed to find suitable memory type for depth image");
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeResult.value();

    result = vkAllocateMemory(device->device, &allocInfo, nullptr, &depthImage.mem);
    if (result != VK_SUCCESS) {
        vkDestroyImage(device->device, depthImage.image, nullptr);
        throw std::runtime_error("Failed to allocate depth image memory");
    }

    vkBindImageMemory(device->device, depthImage.image, depthImage.mem, 0);

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

    result = vkCreateImageView(device->device, &viewInfo, nullptr, &depthImage.view);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view");
    }
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
        throw std::runtime_error("Unsupported layout transition");
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
