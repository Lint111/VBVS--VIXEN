#include "Nodes/VoxelAABBConverterNode.h"
#include "Data/SceneGenerator.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include <cstring>

using VIXEN::RenderGraph::VoxelGrid;
using VIXEN::RenderGraph::VoxelDataCache;
using VIXEN::RenderGraph::SceneGeneratorParams;

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> VoxelAABBConverterNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::unique_ptr<NodeInstance>(
        new VoxelAABBConverterNode(instanceName, const_cast<VoxelAABBConverterNodeType*>(this))
    );
}

// ============================================================================
// VOXEL AABB CONVERTER NODE IMPLEMENTATION
// ============================================================================

VoxelAABBConverterNode::VoxelAABBConverterNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<VoxelAABBConverterNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("VoxelAABBConverterNode constructor (Phase K)");
}

void VoxelAABBConverterNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[VoxelAABBConverterNode::SetupImpl] ENTERED");

    // Read parameters
    gridResolution_ = GetParameterValue<uint32_t>(
        VoxelAABBConverterNodeConfig::PARAM_GRID_RESOLUTION, 128u);
    voxelSize_ = GetParameterValue<float>(
        VoxelAABBConverterNodeConfig::PARAM_VOXEL_SIZE, 1.0f);

    // Scene type should match VoxelGridNode's scene type for consistent data
    sceneType_ = GetParameterValue<std::string>("scene_type", std::string("cornell"));

    NODE_LOG_INFO("VoxelAABBConverter setup: resolution=" + std::to_string(gridResolution_) +
                  ", voxelSize=" + std::to_string(voxelSize_) +
                  ", scene=" + sceneType_);
    NODE_LOG_DEBUG("[VoxelAABBConverterNode::SetupImpl] COMPLETED");
}

void VoxelAABBConverterNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[VoxelAABBConverterNode::CompileImpl] ENTERED");
    NODE_LOG_INFO("=== VoxelAABBConverterNode::CompileImpl START ===");

    // Get device
    VulkanDevice* devicePtr = ctx.In(VoxelAABBConverterNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[VoxelAABBConverterNode] VULKAN_DEVICE_IN is null");
    }
    SetDevice(devicePtr);
    vulkanDevice_ = devicePtr;

    // Get command pool
    commandPool_ = ctx.In(VoxelAABBConverterNodeConfig::COMMAND_POOL);
    if (commandPool_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[VoxelAABBConverterNode] COMMAND_POOL is null");
    }

    // We also have OCTREE_NODES_BUFFER but for AABB extraction we use the cached grid
    // (GPU buffer readback would be slow and unnecessary since we have VoxelDataCache)

    // Extract AABBs from cached voxel grid
    std::vector<VoxelAABB> aabbs = ExtractAABBsFromGrid();

    if (aabbs.empty()) {
        NODE_LOG_WARNING("No solid voxels found - AABB buffer will be empty");
        // Create minimal valid buffer anyway
        aabbData_.aabbCount = 0;
        aabbData_.gridResolution = gridResolution_;
        aabbData_.voxelSize = voxelSize_;
    } else {
        NODE_LOG_INFO("Extracted " + std::to_string(aabbs.size()) + " AABBs from voxel grid");

        // Create and upload AABB buffer
        CreateAABBBuffer(aabbs);
        UploadAABBData(aabbs);
    }

    // Output the AABB data struct (pointer to member)
    ctx.Out(VoxelAABBConverterNodeConfig::AABB_DATA, &aabbData_);

    NODE_LOG_INFO("=== VoxelAABBConverterNode::CompileImpl COMPLETE ===");
    NODE_LOG_DEBUG("[VoxelAABBConverterNode::CompileImpl] COMPLETED");
}

void VoxelAABBConverterNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // AABB data is static (created during compile)
    // Just pass through the cached data pointer
    ctx.Out(VoxelAABBConverterNodeConfig::AABB_DATA, &aabbData_);
}

void VoxelAABBConverterNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("VoxelAABBConverterNode cleanup");
    DestroyAABBBuffer();
}

// ============================================================================
// AABB EXTRACTION
// ============================================================================

std::vector<VoxelAABB> VoxelAABBConverterNode::ExtractAABBsFromGrid() {
    NODE_LOG_INFO("Extracting AABBs from grid: resolution=" + std::to_string(gridResolution_) +
                  ", scene=" + sceneType_);

    // Get cached voxel grid (same data as VoxelGridNode)
    SceneGeneratorParams params;
    params.resolution = gridResolution_;
    params.seed = 42;  // Fixed seed for reproducibility

    const VoxelGrid* cachedGrid = VoxelDataCache::GetOrGenerate(sceneType_, gridResolution_, params);

    if (!cachedGrid) {
        NODE_LOG_ERROR("Failed to get cached voxel grid for scene: " + sceneType_);
        return {};
    }

    const VoxelGrid& grid = *cachedGrid;
    std::vector<VoxelAABB> aabbs;
    aabbs.reserve(grid.CountSolidVoxels());

    // Iterate through all voxels and collect solid ones
    const uint32_t res = grid.GetResolution();
    for (uint32_t z = 0; z < res; ++z) {
        for (uint32_t y = 0; y < res; ++y) {
            for (uint32_t x = 0; x < res; ++x) {
                if (grid.Get(x, y, z) != 0) {
                    // Create AABB for this voxel
                    VoxelAABB aabb;
                    aabb.min = glm::vec3(
                        static_cast<float>(x) * voxelSize_,
                        static_cast<float>(y) * voxelSize_,
                        static_cast<float>(z) * voxelSize_
                    );
                    aabb.max = glm::vec3(
                        static_cast<float>(x + 1) * voxelSize_,
                        static_cast<float>(y + 1) * voxelSize_,
                        static_cast<float>(z + 1) * voxelSize_
                    );
                    aabbs.push_back(aabb);
                }
            }
        }
    }

    NODE_LOG_INFO("Extracted " + std::to_string(aabbs.size()) + " AABBs (" +
                  std::to_string(aabbs.size() * 100 / (res * res * res)) + "% density)");

    return aabbs;
}

// ============================================================================
// BUFFER CREATION
// ============================================================================

void VoxelAABBConverterNode::CreateAABBBuffer(const std::vector<VoxelAABB>& aabbs) {
    if (!vulkanDevice_ || aabbs.empty()) {
        return;
    }

    VkDevice device = vulkanDevice_->device;
    VkDeviceSize bufferSize = aabbs.size() * sizeof(VoxelAABB);

    // Create buffer with acceleration structure build input flag
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &aabbData_.aabbBuffer) != VK_SUCCESS) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to create AABB buffer");
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, aabbData_.aabbBuffer, &memRequirements);

    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &allocFlagsInfo;
    allocInfo.allocationSize = memRequirements.size;

    // Find device-local memory type
    auto memTypeResult = vulkanDevice_->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (!memTypeResult.has_value()) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to find suitable memory type");
    }
    allocInfo.memoryTypeIndex = memTypeResult.value();

    if (vkAllocateMemory(device, &allocInfo, nullptr, &aabbData_.aabbBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to allocate AABB buffer memory");
    }

    vkBindBufferMemory(device, aabbData_.aabbBuffer, aabbData_.aabbBufferMemory, 0);

    // Update output data
    aabbData_.aabbCount = static_cast<uint32_t>(aabbs.size());
    aabbData_.aabbBufferSize = bufferSize;
    aabbData_.gridResolution = gridResolution_;
    aabbData_.voxelSize = voxelSize_;

    NODE_LOG_INFO("Created AABB buffer: " + std::to_string(bufferSize) + " bytes for " +
                  std::to_string(aabbs.size()) + " AABBs");
}

void VoxelAABBConverterNode::UploadAABBData(const std::vector<VoxelAABB>& aabbs) {
    if (!vulkanDevice_ || aabbs.empty() || aabbData_.aabbBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkDevice device = vulkanDevice_->device;
    VkDeviceSize bufferSize = aabbs.size() * sizeof(VoxelAABB);

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &stagingBufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to create staging buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    auto memTypeResult = vulkanDevice_->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (!memTypeResult.has_value()) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to find staging memory type");
    }
    allocInfo.memoryTypeIndex = memTypeResult.value();

    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to allocate staging memory");
    }

    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    // Map and copy data
    void* data;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, aabbs.data(), bufferSize);
    vkUnmapMemory(device, stagingMemory);

    // Copy from staging to device-local buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, aabbData_.aabbBuffer, 1, &copyRegion);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(vulkanDevice_->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanDevice_->queue);

    // Cleanup staging resources
    vkFreeCommandBuffers(device, commandPool_, 1, &cmdBuffer);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    NODE_LOG_INFO("Uploaded " + std::to_string(bufferSize) + " bytes of AABB data to GPU");
}

void VoxelAABBConverterNode::DestroyAABBBuffer() {
    if (!vulkanDevice_) {
        return;
    }

    VkDevice device = vulkanDevice_->device;

    if (aabbData_.aabbBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, aabbData_.aabbBuffer, nullptr);
        aabbData_.aabbBuffer = VK_NULL_HANDLE;
    }

    if (aabbData_.aabbBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, aabbData_.aabbBufferMemory, nullptr);
        aabbData_.aabbBufferMemory = VK_NULL_HANDLE;
    }

    aabbData_.aabbCount = 0;
    aabbData_.aabbBufferSize = 0;

    NODE_LOG_INFO("Destroyed AABB buffer resources");
}

} // namespace Vixen::RenderGraph
