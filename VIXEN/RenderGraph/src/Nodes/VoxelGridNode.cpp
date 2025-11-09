#include "Nodes/VoxelGridNode.h"
#include "Data/SceneGenerator.h"
#include "Data/VoxelOctree.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include <cmath>
#include <cstring>

using VIXEN::RenderGraph::VoxelGrid;
using VIXEN::RenderGraph::SparseVoxelOctree;
using VIXEN::RenderGraph::CornellBoxGenerator;
using VIXEN::RenderGraph::CaveSystemGenerator;
using VIXEN::RenderGraph::UrbanGridGenerator;
using VIXEN::RenderGraph::OctreeNode;
using VIXEN::RenderGraph::VoxelBrick;

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> VoxelGridNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::unique_ptr<NodeInstance>(new VoxelGridNode(instanceName, const_cast<VoxelGridNodeType*>(this)));
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

void VoxelGridNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[VoxelGridNode::SetupImpl] ENTERED with taskIndex=" + std::to_string(ctx.taskIndex));
    NODE_LOG_INFO("VoxelGridNode setup");

    // Read parameters
    resolution = GetParameterValue<uint32_t>(VoxelGridNodeConfig::PARAM_RESOLUTION, 128u);
    sceneType = GetParameterValue<std::string>(VoxelGridNodeConfig::PARAM_SCENE_TYPE, std::string("test"));

    NODE_LOG_INFO("Voxel grid: " + std::to_string(resolution) + "^3, scene=" + sceneType);
    NODE_LOG_DEBUG("[VoxelGridNode::SetupImpl] COMPLETED");
}

void VoxelGridNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] ENTERED with taskIndex=" + std::to_string(ctx.taskIndex));
    NODE_LOG_INFO("=== VoxelGridNode::CompileImpl START ===");

    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] Getting device...");
    // Get device
    VulkanDevicePtr devicePtr = ctx.In(VoxelGridNodeConfig::VULKAN_DEVICE_IN);
    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] Device ptr: " + std::to_string(reinterpret_cast<uint64_t>(devicePtr)));
    if (!devicePtr) {
        NODE_LOG_ERROR("[VoxelGridNode::CompileImpl] ERROR: Device is null!");
        throw std::runtime_error("[VoxelGridNode] VULKAN_DEVICE_IN is null");
    }

    SetDevice(devicePtr);
    vulkanDevice = devicePtr;

    // Get command pool
    commandPool = ctx.In(VoxelGridNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[VoxelGridNode] COMMAND_POOL is null");
    }

    // Generate procedural voxel scene
    VoxelGrid grid(resolution);
    GenerateProceduralScene(grid);

    size_t voxelCount = resolution * resolution * resolution;
    NODE_LOG_INFO("Generated " + std::to_string(voxelCount) + " voxels, density=" +
                  std::to_string(grid.GetDensityPercent()) + "%");

    // Build sparse voxel octree
    SparseVoxelOctree octree;
    octree.BuildFromGrid(grid.GetData(), resolution);

    NODE_LOG_INFO("Built octree: " +
                  std::to_string(octree.GetNodes().size()) + " nodes, " +
                  std::to_string(octree.GetBricks().size()) + " bricks, " +
                  "compression=" + std::to_string(octree.GetCompressionRatio(resolution)) + ":1");

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

    // Use cached memory properties from VulkanDevice
    const VkPhysicalDeviceMemoryProperties& memProperties = vulkanDevice->gpuMemoryProperties;

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

    // Upload voxel data (legacy 3D texture)
    UploadVoxelData(grid.GetData());

    // Upload octree buffers (new SSBO path)
    UploadOctreeBuffers(octree);

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
    std::cout << "!!!! [VoxelGridNode::CompileImpl] OUTPUTTING NEW RESOURCES !!!!" << std::endl;
    std::cout << "  NEW voxelImage=" << voxelImage << ", voxelImageView=" << voxelImageView << ", voxelSampler=" << voxelSampler << std::endl;
    std::cout << "  NEW octreeNodesBuffer=" << octreeNodesBuffer << ", octreeBricksBuffer=" << octreeBricksBuffer << std::endl;

    ctx.Out(VoxelGridNodeConfig::VOXEL_IMAGE, voxelImage);

    // Create combined image/sampler pair
    ImageSamplerPair combinedSampler(voxelImageView, voxelSampler);
    ctx.Out(VoxelGridNodeConfig::VOXEL_COMBINED_SAMPLER, combinedSampler);

    // Output octree buffers
    ctx.Out(VoxelGridNodeConfig::OCTREE_NODES_BUFFER, octreeNodesBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_BRICKS_BUFFER, octreeBricksBuffer);

    std::cout << "!!!! [VoxelGridNode::CompileImpl] OUTPUTS SET !!!!" << std::endl;

    NODE_LOG_INFO("Created 3D voxel texture successfully");
    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] COMPLETED");
}

void VoxelGridNode::ExecuteImpl(TypedExecuteContext& ctx) {
    NODE_LOG_DEBUG("[VoxelGridNode::ExecuteImpl] ENTERED with taskIndex=" + std::to_string(ctx.taskIndex));
    // Re-output persistent resources every frame for variadic connections
    // When swapchain recompiles, descriptor gatherer re-queries these outputs

    NODE_LOG_INFO("=== VoxelGridNode::ExecuteImpl START ===");
    NODE_LOG_INFO("  voxelImage handle: " + std::to_string(reinterpret_cast<uint64_t>(voxelImage)));
    NODE_LOG_INFO("  voxelImageView handle: " + std::to_string(reinterpret_cast<uint64_t>(voxelImageView)));
    NODE_LOG_INFO("  voxelSampler handle: " + std::to_string(reinterpret_cast<uint64_t>(voxelSampler)));
    NODE_LOG_INFO("  octreeNodesBuffer handle: " + std::to_string(reinterpret_cast<uint64_t>(octreeNodesBuffer)));
    NODE_LOG_INFO("  octreeBricksBuffer handle: " + std::to_string(reinterpret_cast<uint64_t>(octreeBricksBuffer)));

    ctx.Out(VoxelGridNodeConfig::VOXEL_IMAGE, voxelImage);

    ImageSamplerPair combinedSampler(voxelImageView, voxelSampler);
    ctx.Out(VoxelGridNodeConfig::VOXEL_COMBINED_SAMPLER, combinedSampler);

    ctx.Out(VoxelGridNodeConfig::OCTREE_NODES_BUFFER, octreeNodesBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_BRICKS_BUFFER, octreeBricksBuffer);

    NODE_LOG_INFO("=== VoxelGridNode::ExecuteImpl END ===");
    NODE_LOG_DEBUG("[VoxelGridNode::ExecuteImpl] COMPLETED");
}

void VoxelGridNode::GenerateTestPattern(std::vector<uint8_t>& voxelData) {
    // DEBUG: Fill entire grid with solid voxels to test upload
    for (size_t i = 0; i < voxelData.size(); ++i) {
        voxelData[i] = 255;  // All voxels solid
    }

    NODE_LOG_INFO("Generated FULL SOLID test pattern (all voxels = 255)");
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

    // Use cached memory properties from VulkanDevice
    const VkPhysicalDeviceMemoryProperties& memProperties = vulkanDevice->gpuMemoryProperties;

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

    vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanDevice->queue);

    // Cleanup staging resources
    vkFreeCommandBuffers(vulkanDevice->device, commandPool, 1, &cmdBuffer);
    vkDestroyBuffer(vulkanDevice->device, stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice->device, stagingMemory, nullptr);

    stagingBuffer = VK_NULL_HANDLE;
    stagingMemory = VK_NULL_HANDLE;

    NODE_LOG_INFO("Uploaded voxel data to GPU");
}

void VoxelGridNode::CleanupImpl(TypedCleanupContext& ctx) {
    std::cout << "!!!! [VoxelGridNode::CleanupImpl] DESTROYING RESOURCES !!!!" << std::endl;
    std::cout << "  voxelImage=" << voxelImage << ", voxelImageView=" << voxelImageView << ", voxelSampler=" << voxelSampler << std::endl;
    std::cout << "  octreeNodesBuffer=" << octreeNodesBuffer << ", octreeBricksBuffer=" << octreeBricksBuffer << std::endl;
    NODE_LOG_INFO("VoxelGridNode cleanup");

    if (!vulkanDevice) {
        return;
    }

    vkDeviceWaitIdle(vulkanDevice->device);

    // Cleanup octree buffers
    if (octreeBricksBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeBricksBuffer, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed octreeBricksBuffer" << std::endl;
        octreeBricksBuffer = VK_NULL_HANDLE;
    }

    if (octreeBricksMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeBricksMemory, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed octreeBricksMemory" << std::endl;
        octreeBricksMemory = VK_NULL_HANDLE;
    }

    if (octreeNodesBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeNodesBuffer, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed octreeNodesBuffer" << std::endl;
        octreeNodesBuffer = VK_NULL_HANDLE;
    }

    if (octreeNodesMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeNodesMemory, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed octreeNodesMemory" << std::endl;
        octreeNodesMemory = VK_NULL_HANDLE;
    }

    // Cleanup voxel texture
    if (voxelSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkanDevice->device, voxelSampler, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed voxelSampler" << std::endl;
        voxelSampler = VK_NULL_HANDLE;
    }

    if (voxelImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkanDevice->device, voxelImageView, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed voxelImageView" << std::endl;
        voxelImageView = VK_NULL_HANDLE;
    }

    if (voxelImage != VK_NULL_HANDLE) {
        vkDestroyImage(vulkanDevice->device, voxelImage, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed voxelImage" << std::endl;
        voxelImage = VK_NULL_HANDLE;
    }

    if (voxelMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, voxelMemory, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed voxelMemory" << std::endl;
        voxelMemory = VK_NULL_HANDLE;
    }
    std::cout << "!!!! [VoxelGridNode::CleanupImpl] CLEANUP COMPLETE !!!!" << std::endl;
}

void VoxelGridNode::GenerateProceduralScene(VoxelGrid& grid) {
    NODE_LOG_INFO("Generating procedural scene: " + sceneType);

    if (sceneType == "cornell") {
        CornellBoxGenerator::Generate(grid);
    } else if (sceneType == "cave") {
        CaveSystemGenerator::Generate(grid);
    } else if (sceneType == "urban") {
        UrbanGridGenerator::Generate(grid);
    } else {
        // Default: test pattern (all solid)
        for (uint32_t z = 0; z < resolution; ++z) {
            for (uint32_t y = 0; y < resolution; ++y) {
                for (uint32_t x = 0; x < resolution; ++x) {
                    grid.Set(x, y, z, 255);
                }
            }
        }
        NODE_LOG_INFO("Generated test pattern (all solid voxels)");
    }
}

void VoxelGridNode::UploadOctreeBuffers(const SparseVoxelOctree& octree) {
    const auto& nodes = octree.GetNodes();
    const auto& bricks = octree.GetBricks();

    VkDeviceSize nodesBufferSize = nodes.size() * sizeof(OctreeNode);
    VkDeviceSize bricksBufferSize = bricks.size() * sizeof(VoxelBrick);

    NODE_LOG_INFO("Uploading octree: " +
                  std::to_string(nodesBufferSize) + " bytes (nodes), " +
                  std::to_string(bricksBufferSize) + " bytes (bricks)");

    // === CREATE NODES BUFFER ===
    VkBufferCreateInfo nodesBufferInfo{};
    nodesBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    nodesBufferInfo.size = nodesBufferSize;
    nodesBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    nodesBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(vulkanDevice->device, &nodesBufferInfo, nullptr, &octreeNodesBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create octree nodes buffer: " + std::to_string(result));
    }

    // Allocate device-local memory for nodes
    VkMemoryRequirements nodesMemReq;
    vkGetBufferMemoryRequirements(vulkanDevice->device, octreeNodesBuffer, &nodesMemReq);

    VkMemoryAllocateInfo nodesAllocInfo{};
    nodesAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    nodesAllocInfo.allocationSize = nodesMemReq.size;

    const VkPhysicalDeviceMemoryProperties& memProperties = vulkanDevice->gpuMemoryProperties;
    uint32_t memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((nodesMemReq.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("[VoxelGridNode] Failed to find device-local memory for octree nodes");
    }

    nodesAllocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(vulkanDevice->device, &nodesAllocInfo, nullptr, &octreeNodesMemory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to allocate octree nodes memory: " + std::to_string(result));
    }

    vkBindBufferMemory(vulkanDevice->device, octreeNodesBuffer, octreeNodesMemory, 0);

    // === CREATE BRICKS BUFFER ===
    VkBufferCreateInfo bricksBufferInfo{};
    bricksBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bricksBufferInfo.size = bricksBufferSize;
    bricksBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bricksBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = vkCreateBuffer(vulkanDevice->device, &bricksBufferInfo, nullptr, &octreeBricksBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create octree bricks buffer: " + std::to_string(result));
    }

    // Allocate device-local memory for bricks
    VkMemoryRequirements bricksMemReq;
    vkGetBufferMemoryRequirements(vulkanDevice->device, octreeBricksBuffer, &bricksMemReq);

    VkMemoryAllocateInfo bricksAllocInfo{};
    bricksAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bricksAllocInfo.allocationSize = bricksMemReq.size;

    memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((bricksMemReq.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("[VoxelGridNode] Failed to find device-local memory for octree bricks");
    }

    bricksAllocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(vulkanDevice->device, &bricksAllocInfo, nullptr, &octreeBricksMemory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to allocate octree bricks memory: " + std::to_string(result));
    }

    vkBindBufferMemory(vulkanDevice->device, octreeBricksBuffer, octreeBricksMemory, 0);

    // === UPLOAD DATA VIA STAGING BUFFER ===

    // Upload nodes
    if (nodesBufferSize > 0) {
        // Create staging buffer for nodes
        VkBufferCreateInfo nodesStagingInfo{};
        nodesStagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        nodesStagingInfo.size = nodesBufferSize;
        nodesStagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        nodesStagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer nodesStagingBuffer;
        vkCreateBuffer(vulkanDevice->device, &nodesStagingInfo, nullptr, &nodesStagingBuffer);

        VkMemoryRequirements nodesStagingMemReq;
        vkGetBufferMemoryRequirements(vulkanDevice->device, nodesStagingBuffer, &nodesStagingMemReq);

        VkMemoryAllocateInfo nodesStagingAllocInfo{};
        nodesStagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        nodesStagingAllocInfo.allocationSize = nodesStagingMemReq.size;

        memoryTypeIndex = UINT32_MAX;
        VkMemoryPropertyFlags stagingProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((nodesStagingMemReq.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & stagingProps) == stagingProps) {
                memoryTypeIndex = i;
                break;
            }
        }

        nodesStagingAllocInfo.memoryTypeIndex = memoryTypeIndex;

        VkDeviceMemory nodesStagingMemory;
        vkAllocateMemory(vulkanDevice->device, &nodesStagingAllocInfo, nullptr, &nodesStagingMemory);
        vkBindBufferMemory(vulkanDevice->device, nodesStagingBuffer, nodesStagingMemory, 0);

        // Copy data to staging buffer
        void* data;
        vkMapMemory(vulkanDevice->device, nodesStagingMemory, 0, nodesBufferSize, 0, &data);
        std::memcpy(data, nodes.data(), nodesBufferSize);
        vkUnmapMemory(vulkanDevice->device, nodesStagingMemory);

        // Copy from staging to device buffer
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

        VkBufferCopy copyRegion{};
        copyRegion.size = nodesBufferSize;
        vkCmdCopyBuffer(cmdBuffer, nodesStagingBuffer, octreeNodesBuffer, 1, &copyRegion);

        vkEndCommandBuffer(cmdBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vulkanDevice->queue);

        vkFreeCommandBuffers(vulkanDevice->device, commandPool, 1, &cmdBuffer);
        vkDestroyBuffer(vulkanDevice->device, nodesStagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice->device, nodesStagingMemory, nullptr);
    }

    // Upload bricks
    if (bricksBufferSize > 0) {
        // Create staging buffer for bricks
        VkBufferCreateInfo bricksStagingInfo{};
        bricksStagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bricksStagingInfo.size = bricksBufferSize;
        bricksStagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bricksStagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer bricksStagingBuffer;
        vkCreateBuffer(vulkanDevice->device, &bricksStagingInfo, nullptr, &bricksStagingBuffer);

        VkMemoryRequirements bricksStagingMemReq;
        vkGetBufferMemoryRequirements(vulkanDevice->device, bricksStagingBuffer, &bricksStagingMemReq);

        VkMemoryAllocateInfo bricksStagingAllocInfo{};
        bricksStagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        bricksStagingAllocInfo.allocationSize = bricksStagingMemReq.size;

        memoryTypeIndex = UINT32_MAX;
        VkMemoryPropertyFlags stagingProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((bricksStagingMemReq.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & stagingProps) == stagingProps) {
                memoryTypeIndex = i;
                break;
            }
        }

        bricksStagingAllocInfo.memoryTypeIndex = memoryTypeIndex;

        VkDeviceMemory bricksStagingMemory;
        vkAllocateMemory(vulkanDevice->device, &bricksStagingAllocInfo, nullptr, &bricksStagingMemory);
        vkBindBufferMemory(vulkanDevice->device, bricksStagingBuffer, bricksStagingMemory, 0);

        // Copy data to staging buffer
        void* data;
        vkMapMemory(vulkanDevice->device, bricksStagingMemory, 0, bricksBufferSize, 0, &data);
        std::memcpy(data, bricks.data(), bricksBufferSize);
        vkUnmapMemory(vulkanDevice->device, bricksStagingMemory);

        // Copy from staging to device buffer
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

        VkBufferCopy copyRegion{};
        copyRegion.size = bricksBufferSize;
        vkCmdCopyBuffer(cmdBuffer, bricksStagingBuffer, octreeBricksBuffer, 1, &copyRegion);

        vkEndCommandBuffer(cmdBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vulkanDevice->queue);

        vkFreeCommandBuffers(vulkanDevice->device, commandPool, 1, &cmdBuffer);
        vkDestroyBuffer(vulkanDevice->device, bricksStagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice->device, bricksStagingMemory, nullptr);
    }

    NODE_LOG_INFO("Uploaded octree buffers to GPU");
}

} // namespace Vixen::RenderGraph
