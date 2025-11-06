#include "Nodes/VoxelGridNode.h"
#include "VulkanResources/VulkanDevice.h"
#include <iostream>
#include <cmath>
#include <cstring>

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> VoxelGridNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<VoxelGridNode>(instanceName, const_cast<VoxelGridNodeType*>(this));
}

// ============================================================================
// VOXEL GRID NODE IMPLEMENTATION
// ============================================================================

VoxelGridNode::VoxelGridNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<VoxelGridNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("VoxelGridNode constructor");
}

void VoxelGridNode::SetupImpl(Context& ctx) {
    NODE_LOG_INFO("VoxelGridNode setup");

    // Read parameters
    resolution = GetParameterValue<uint32_t>(VoxelGridNodeConfig::PARAM_RESOLUTION, 128u);
    sceneType = GetParameterValue<std::string>(VoxelGridNodeConfig::PARAM_SCENE_TYPE, std::string("test"));

    NODE_LOG_INFO("Voxel grid: " + std::to_string(resolution) + "^3, scene=" + sceneType);
}

void VoxelGridNode::CompileImpl(Context& ctx) {
    NODE_LOG_INFO("VoxelGridNode compile");

    // Get device
    VulkanDevicePtr devicePtr = In(VoxelGridNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[VoxelGridNode] VULKAN_DEVICE_IN is null");
    }

    SetDevice(devicePtr);
    vulkanDevice = devicePtr;

    // Get command pool
    commandPool = In(VoxelGridNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[VoxelGridNode] COMMAND_POOL is null");
    }

    // Generate voxel data
    size_t voxelCount = resolution * resolution * resolution;
    std::vector<uint8_t> voxelData(voxelCount);
    GenerateTestPattern(voxelData);

    NODE_LOG_INFO("Generated " + std::to_string(voxelCount) + " voxels");

    // Create 3D image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = VK_FORMAT_R8_UNORM;  // 8-bit grayscale
    imageInfo.extent = {resolution, resolution, resolution};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vkCreateImage(vulkanDevice->device, &imageInfo, nullptr, &voxelImage);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create 3D image: " + std::to_string(result));
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(vulkanDevice->device, voxelImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    // Find device-local memory
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(vulkanDevice->physicalDevice, &memProperties);

    uint32_t memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }

    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("[VoxelGridNode] Failed to find device-local memory");
    }

    allocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(vulkanDevice->device, &allocInfo, nullptr, &voxelMemory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to allocate image memory: " + std::to_string(result));
    }

    vkBindImageMemory(vulkanDevice->device, voxelImage, voxelMemory, 0);

    // Upload voxel data
    UploadVoxelData(voxelData);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = voxelImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(vulkanDevice->device, &viewInfo, nullptr, &voxelImageView);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create image view: " + std::to_string(result));
    }

    // Create sampler (linear filtering)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    result = vkCreateSampler(vulkanDevice->device, &samplerInfo, nullptr, &voxelSampler);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create sampler: " + std::to_string(result));
    }

    // Output resources
    Out(VoxelGridNodeConfig::VOXEL_IMAGE, voxelImage);
    Out(VoxelGridNodeConfig::VOXEL_IMAGE_VIEW, voxelImageView);
    Out(VoxelGridNodeConfig::VOXEL_SAMPLER, voxelSampler);

    NODE_LOG_INFO("Created 3D voxel texture successfully");
}

void VoxelGridNode::ExecuteImpl(Context& ctx) {
    // Static resource - no per-frame updates needed
}

void VoxelGridNode::GenerateTestPattern(std::vector<uint8_t>& voxelData) {
    // Generate simple test pattern: sphere at center
    int32_t center = resolution / 2;
    float radius = resolution * 0.3f;

    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                size_t index = x + y * resolution + z * resolution * resolution;

                // Calculate distance from center
                float dx = static_cast<float>(x) - center;
                float dy = static_cast<float>(y) - center;
                float dz = static_cast<float>(z) - center;
                float distance = std::sqrt(dx*dx + dy*dy + dz*dz);

                // Solid if inside sphere
                voxelData[index] = (distance < radius) ? 255 : 0;
            }
        }
    }

    NODE_LOG_INFO("Generated test sphere pattern");
}

void VoxelGridNode::UploadVoxelData(const std::vector<uint8_t>& voxelData) {
    VkDeviceSize bufferSize = voxelData.size();

    // Create staging buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(vulkanDevice->device, &bufferInfo, nullptr, &stagingBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create staging buffer: " + std::to_string(result));
    }

    // Allocate host-visible memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice->device, stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(vulkanDevice->physicalDevice, &memProperties);

    uint32_t memoryTypeIndex = UINT32_MAX;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            memoryTypeIndex = i;
            break;
        }
    }

    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("[VoxelGridNode] Failed to find host-visible memory");
    }

    allocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(vulkanDevice->device, &allocInfo, nullptr, &stagingMemory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to allocate staging memory: " + std::to_string(result));
    }

    vkBindBufferMemory(vulkanDevice->device, stagingBuffer, stagingMemory, 0);

    // Copy data to staging buffer
    void* data;
    vkMapMemory(vulkanDevice->device, stagingMemory, 0, bufferSize, 0, &data);
    std::memcpy(data, voxelData.data(), bufferSize);
    vkUnmapMemory(vulkanDevice->device, stagingMemory);

    // Copy from staging buffer to image using command buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(vulkanDevice->device, &cmdAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Transition image to TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = voxelImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {resolution, resolution, resolution};

    vkCmdCopyBufferToImage(
        cmdBuffer,
        stagingBuffer,
        voxelImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    // Transition image to SHADER_READ_ONLY_OPTIMAL
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(vulkanDevice->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanDevice->graphicsQueue);

    // Cleanup staging resources
    vkFreeCommandBuffers(vulkanDevice->device, commandPool, 1, &cmdBuffer);
    vkDestroyBuffer(vulkanDevice->device, stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice->device, stagingMemory, nullptr);

    stagingBuffer = VK_NULL_HANDLE;
    stagingMemory = VK_NULL_HANDLE;

    NODE_LOG_INFO("Uploaded voxel data to GPU");
}

void VoxelGridNode::CleanupImpl() {
    NODE_LOG_INFO("VoxelGridNode cleanup");

    if (!vulkanDevice) {
        return;
    }

    vkDeviceWaitIdle(vulkanDevice->device);

    if (voxelSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkanDevice->device, voxelSampler, nullptr);
        voxelSampler = VK_NULL_HANDLE;
    }

    if (voxelImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkanDevice->device, voxelImageView, nullptr);
        voxelImageView = VK_NULL_HANDLE;
    }

    if (voxelImage != VK_NULL_HANDLE) {
        vkDestroyImage(vulkanDevice->device, voxelImage, nullptr);
        voxelImage = VK_NULL_HANDLE;
    }

    if (voxelMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, voxelMemory, nullptr);
        voxelMemory = VK_NULL_HANDLE;
    }
}

} // namespace Vixen::RenderGraph
