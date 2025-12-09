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

    // Enable terminal output for debugging RT sparse issue
    if (nodeLogger) {
        nodeLogger->SetEnabled(true);
        nodeLogger->SetTerminalOutput(true);
    }

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

    // ========================================================================
    // Read brick grid lookup buffer from VoxelGridNode (if available)
    // ========================================================================
    // This buffer maps (brickX, brickY, brickZ) -> Morton-sorted brick index
    // We need this to generate correct brick mappings for compressed RTX shaders
    std::vector<uint32_t> brickGridLookup;
    VkBuffer lookupBuffer = ctx.In(VoxelAABBConverterNodeConfig::BRICK_GRID_LOOKUP_BUFFER);

    if (lookupBuffer != VK_NULL_HANDLE) {
        // Calculate expected buffer size based on grid resolution
        constexpr uint32_t BRICK_SIZE = 8;
        const uint32_t bricksPerAxis = (gridResolution_ + BRICK_SIZE - 1) / BRICK_SIZE;
        const uint32_t totalSlots = bricksPerAxis * bricksPerAxis * bricksPerAxis;
        const VkDeviceSize bufferSize = totalSlots * sizeof(uint32_t);

        // Download GPU buffer to CPU for brick mapping lookup
        brickGridLookup = DownloadBufferToHost(lookupBuffer, bufferSize);
        NODE_LOG_INFO("Downloaded brick grid lookup buffer: " + std::to_string(brickGridLookup.size()) +
                      " slots (" + std::to_string(bricksPerAxis) + "^3 bricks)");
    } else {
        NODE_LOG_WARNING("BRICK_GRID_LOOKUP_BUFFER not connected - using fallback linear brick indices");
    }

    // Extract AABBs, material IDs, and brick mappings from cached voxel grid
    std::vector<uint32_t> materialIds;
    std::vector<VoxelBrickMapping> brickMappings;
    std::vector<VoxelAABB> aabbs = ExtractAABBsFromGrid(materialIds, brickMappings, brickGridLookup);

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

        // Create and upload material ID buffer
        CreateMaterialIdBuffer(materialIds);
        UploadMaterialIdData(materialIds);

        // Create and upload brick mapping buffer (for compressed RTX shaders)
        CreateBrickMappingBuffer(brickMappings);
        UploadBrickMappingData(brickMappings);
    }

    // Output the AABB data struct (pointer to member)
    ctx.Out(VoxelAABBConverterNodeConfig::AABB_DATA, &aabbData_);

    // Also output raw buffers for shader descriptor binding
    ctx.Out(VoxelAABBConverterNodeConfig::AABB_BUFFER, aabbData_.aabbBuffer);
    ctx.Out(VoxelAABBConverterNodeConfig::MATERIAL_ID_BUFFER, aabbData_.materialIdBuffer);
    ctx.Out(VoxelAABBConverterNodeConfig::BRICK_MAPPING_BUFFER, aabbData_.brickMappingBuffer);

    NODE_LOG_INFO("=== VoxelAABBConverterNode::CompileImpl COMPLETE ===");
    NODE_LOG_DEBUG("[VoxelAABBConverterNode::CompileImpl] COMPLETED");
}

void VoxelAABBConverterNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // AABB data is static (created during compile)
    // Just pass through the cached data pointer
    ctx.Out(VoxelAABBConverterNodeConfig::AABB_DATA, &aabbData_);
    ctx.Out(VoxelAABBConverterNodeConfig::AABB_BUFFER, aabbData_.aabbBuffer);
    ctx.Out(VoxelAABBConverterNodeConfig::MATERIAL_ID_BUFFER, aabbData_.materialIdBuffer);
    ctx.Out(VoxelAABBConverterNodeConfig::BRICK_MAPPING_BUFFER, aabbData_.brickMappingBuffer);
}

void VoxelAABBConverterNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("VoxelAABBConverterNode cleanup");
    DestroyAABBBuffer();
}

// ============================================================================
// AABB EXTRACTION
// ============================================================================

std::vector<VoxelAABB> VoxelAABBConverterNode::ExtractAABBsFromGrid(
    std::vector<uint32_t>& outMaterialIds,
    std::vector<VoxelBrickMapping>& outBrickMappings,
    const std::vector<uint32_t>& brickGridLookup)
{
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
    outMaterialIds.reserve(grid.CountSolidVoxels());
    outBrickMappings.reserve(grid.CountSolidVoxels());

    // Brick size is 8x8x8 = 512 voxels per brick
    // This matches the compressed buffer layout used by VoxelGridNode
    constexpr uint32_t BRICK_SIZE = 8;
    const uint32_t bricksPerAxis = (gridResolution_ + BRICK_SIZE - 1) / BRICK_SIZE;

    // Check if we have the brick grid lookup buffer from VoxelGridNode
    // This buffer maps linear grid index -> Morton-sorted brick index
    // Format: lookupData[brickX + brickY*bricksPerAxis + brickZ*bricksPerAxis²] = mortonBrickIndex
    const bool haveLookup = !brickGridLookup.empty();
    if (haveLookup) {
        NODE_LOG_INFO("Using brick grid lookup buffer (" + std::to_string(brickGridLookup.size()) +
                      " entries) for Morton-sorted brick indices");
    } else {
        NODE_LOG_WARNING("No brick grid lookup - using fallback linear brick indices (colors may be wrong)");
    }

    // Iterate through all voxels and collect solid ones
    const uint32_t res = grid.GetResolution();
    uint32_t lookupMisses = 0;

    for (uint32_t z = 0; z < res; ++z) {
        for (uint32_t y = 0; y < res; ++y) {
            for (uint32_t x = 0; x < res; ++x) {
                uint8_t materialId = grid.Get(x, y, z);
                if (materialId != 0) {
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
                    outMaterialIds.push_back(static_cast<uint32_t>(materialId));

                    // Compute brick coordinates (which 8x8x8 brick this voxel belongs to)
                    uint32_t brickX = x / BRICK_SIZE;
                    uint32_t brickY = y / BRICK_SIZE;
                    uint32_t brickZ = z / BRICK_SIZE;

                    // Look up Morton-sorted brick index from brickGridLookup buffer
                    // This buffer was created by VoxelGridNode from brickGridToBrickView
                    // Format: linearIdx = brickX + brickY*bricksPerAxis + brickZ*bricksPerAxis²
                    uint32_t linearIdx = brickX + brickY * bricksPerAxis + brickZ * bricksPerAxis * bricksPerAxis;
                    uint32_t brickIndex = 0;

                    if (haveLookup && linearIdx < brickGridLookup.size()) {
                        uint32_t lookupValue = brickGridLookup[linearIdx];
                        if (lookupValue != 0xFFFFFFFF) {
                            brickIndex = lookupValue;
                        } else {
                            // Empty brick marker - shouldn't happen for solid voxels
                            brickIndex = linearIdx;  // Fallback
                            lookupMisses++;
                        }
                    } else {
                        // No lookup buffer or out of range - use linear fallback
                        brickIndex = linearIdx;
                        if (haveLookup) lookupMisses++;
                    }

                    // Local position within brick (0-7 for each axis)
                    uint32_t localX = x % BRICK_SIZE;
                    uint32_t localY = y % BRICK_SIZE;
                    uint32_t localZ = z % BRICK_SIZE;

                    // Linear index within brick (XYZ order, 0-511)
                    // MUST match SVORebuild.cpp compression order:
                    //   int x = voxelLinearIdx & 7;
                    //   int y = (voxelLinearIdx >> 3) & 7;
                    //   int z = (voxelLinearIdx >> 6) & 7;
                    // So: voxelLinearIdx = x + y*8 + z*64
                    uint32_t localVoxelIdx = localX +
                                            localY * BRICK_SIZE +
                                            localZ * BRICK_SIZE * BRICK_SIZE;

                    VoxelBrickMapping mapping;
                    mapping.brickIndex = brickIndex;
                    mapping.localVoxelIdx = localVoxelIdx;
                    outBrickMappings.push_back(mapping);
                }
            }
        }
    }

    if (lookupMisses > 0) {
        NODE_LOG_WARNING("Brick lookup misses: " + std::to_string(lookupMisses) +
                        " voxels (will have incorrect colors)");
    }

    NODE_LOG_INFO("Extracted " + std::to_string(aabbs.size()) + " AABBs (" +
                  std::to_string(aabbs.size() * 100 / (res * res * res)) + "% density), " +
                  "bricksPerAxis=" + std::to_string(bricksPerAxis) +
                  (haveLookup ? ", using Morton-sorted brick indices" : ", using linear brick indices"));

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

    // Create buffer with acceleration structure build input flag AND storage buffer bit
    // - ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT: Required for BLAS build
    // - STORAGE_BUFFER_BIT: Required for shader descriptor binding (VoxelRT.rint)
    // - SHADER_DEVICE_ADDRESS_BIT: Required for device address queries
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
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

void VoxelAABBConverterNode::CreateMaterialIdBuffer(const std::vector<uint32_t>& materialIds) {
    if (!vulkanDevice_ || materialIds.empty()) {
        return;
    }

    VkDevice device = vulkanDevice_->device;
    VkDeviceSize bufferSize = materialIds.size() * sizeof(uint32_t);

    // Create storage buffer for shader access
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &aabbData_.materialIdBuffer) != VK_SUCCESS) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to create material ID buffer");
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, aabbData_.materialIdBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    // Find device-local memory type
    auto memTypeResult = vulkanDevice_->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (!memTypeResult.has_value()) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to find suitable memory type for material IDs");
    }
    allocInfo.memoryTypeIndex = memTypeResult.value();

    if (vkAllocateMemory(device, &allocInfo, nullptr, &aabbData_.materialIdBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to allocate material ID buffer memory");
    }

    vkBindBufferMemory(device, aabbData_.materialIdBuffer, aabbData_.materialIdBufferMemory, 0);
    aabbData_.materialIdBufferSize = bufferSize;

    NODE_LOG_INFO("Created material ID buffer: " + std::to_string(bufferSize) + " bytes for " +
                  std::to_string(materialIds.size()) + " material IDs");
}

void VoxelAABBConverterNode::UploadMaterialIdData(const std::vector<uint32_t>& materialIds) {
    if (!vulkanDevice_ || materialIds.empty() || aabbData_.materialIdBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkDevice device = vulkanDevice_->device;
    VkDeviceSize bufferSize = materialIds.size() * sizeof(uint32_t);

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &stagingBufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to create material staging buffer");
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
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to find staging memory type for material IDs");
    }
    allocInfo.memoryTypeIndex = memTypeResult.value();

    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to allocate staging memory for material IDs");
    }

    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    // Map and copy data
    void* data;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, materialIds.data(), bufferSize);
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
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, aabbData_.materialIdBuffer, 1, &copyRegion);

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

    NODE_LOG_INFO("Uploaded " + std::to_string(bufferSize) + " bytes of material ID data to GPU");
}

void VoxelAABBConverterNode::CreateBrickMappingBuffer(const std::vector<VoxelBrickMapping>& brickMappings) {
    if (!vulkanDevice_ || brickMappings.empty()) {
        return;
    }

    VkDevice device = vulkanDevice_->device;
    VkDeviceSize bufferSize = brickMappings.size() * sizeof(VoxelBrickMapping);

    // Create storage buffer for shader access
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &aabbData_.brickMappingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to create brick mapping buffer");
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, aabbData_.brickMappingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    // Find device-local memory type
    auto memTypeResult = vulkanDevice_->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (!memTypeResult.has_value()) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to find suitable memory type for brick mappings");
    }
    allocInfo.memoryTypeIndex = memTypeResult.value();

    if (vkAllocateMemory(device, &allocInfo, nullptr, &aabbData_.brickMappingBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to allocate brick mapping buffer memory");
    }

    vkBindBufferMemory(device, aabbData_.brickMappingBuffer, aabbData_.brickMappingBufferMemory, 0);
    aabbData_.brickMappingBufferSize = bufferSize;

    NODE_LOG_INFO("Created brick mapping buffer: " + std::to_string(bufferSize) + " bytes for " +
                  std::to_string(brickMappings.size()) + " mappings");
}

void VoxelAABBConverterNode::UploadBrickMappingData(const std::vector<VoxelBrickMapping>& brickMappings) {
    if (!vulkanDevice_ || brickMappings.empty() || aabbData_.brickMappingBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkDevice device = vulkanDevice_->device;
    VkDeviceSize bufferSize = brickMappings.size() * sizeof(VoxelBrickMapping);

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &stagingBufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to create brick mapping staging buffer");
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
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to find staging memory type for brick mappings");
    }
    allocInfo.memoryTypeIndex = memTypeResult.value();

    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to allocate staging memory for brick mappings");
    }

    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    // Map and copy data
    void* data;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, brickMappings.data(), bufferSize);
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
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, aabbData_.brickMappingBuffer, 1, &copyRegion);

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

    NODE_LOG_INFO("Uploaded " + std::to_string(bufferSize) + " bytes of brick mapping data to GPU");
}

std::vector<uint32_t> VoxelAABBConverterNode::DownloadBufferToHost(VkBuffer srcBuffer, VkDeviceSize bufferSize) {
    if (!vulkanDevice_ || srcBuffer == VK_NULL_HANDLE || bufferSize == 0) {
        return {};
    }

    VkDevice device = vulkanDevice_->device;
    std::vector<uint32_t> result(bufferSize / sizeof(uint32_t));

    // Create staging buffer for readback
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &stagingBufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to create staging buffer for download");
        return {};
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
        NODE_LOG_ERROR("Failed to find staging memory type for download");
        return {};
    }
    allocInfo.memoryTypeIndex = memTypeResult.value();

    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        NODE_LOG_ERROR("Failed to allocate staging memory for download");
        return {};
    }

    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    // Copy from device buffer to staging
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
    vkCmdCopyBuffer(cmdBuffer, srcBuffer, stagingBuffer, 1, &copyRegion);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(vulkanDevice_->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanDevice_->queue);

    // Map and read data
    void* data;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(result.data(), data, bufferSize);
    vkUnmapMemory(device, stagingMemory);

    // Cleanup staging resources
    vkFreeCommandBuffers(device, commandPool_, 1, &cmdBuffer);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    return result;
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

    if (aabbData_.materialIdBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, aabbData_.materialIdBuffer, nullptr);
        aabbData_.materialIdBuffer = VK_NULL_HANDLE;
    }

    if (aabbData_.materialIdBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, aabbData_.materialIdBufferMemory, nullptr);
        aabbData_.materialIdBufferMemory = VK_NULL_HANDLE;
    }

    if (aabbData_.brickMappingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, aabbData_.brickMappingBuffer, nullptr);
        aabbData_.brickMappingBuffer = VK_NULL_HANDLE;
    }

    if (aabbData_.brickMappingBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, aabbData_.brickMappingBufferMemory, nullptr);
        aabbData_.brickMappingBufferMemory = VK_NULL_HANDLE;
    }

    aabbData_.aabbCount = 0;
    aabbData_.aabbBufferSize = 0;
    aabbData_.materialIdBufferSize = 0;
    aabbData_.brickMappingBufferSize = 0;

    NODE_LOG_INFO("Destroyed AABB buffer resources");
}

} // namespace Vixen::RenderGraph
