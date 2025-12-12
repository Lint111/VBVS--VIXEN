#include "pch.h"
#include "VoxelAABBCacher.h"
#include "VulkanDevice.h"

#include <cstring>
#include <stdexcept>
#include <sstream>

namespace CashSystem {

// ============================================================================
// VOXEL AABB CACHER - PUBLIC API
// ============================================================================

std::shared_ptr<VoxelAABBData> VoxelAABBCacher::GetOrCreate(const VoxelAABBCreateInfo& ci) {
    return TypedCacher<VoxelAABBData, VoxelAABBCreateInfo>::GetOrCreate(ci);
}

// ============================================================================
// VOXEL AABB CACHER - TYPEDCACHER IMPLEMENTATION
// ============================================================================

std::shared_ptr<VoxelAABBData> VoxelAABBCacher::Create(const VoxelAABBCreateInfo& ci) {
    LOG_INFO("[VoxelAABBCacher::Create] Creating AABB data for scene key " + std::to_string(ci.sceneDataKey));

    if (!IsInitialized()) {
        throw std::runtime_error("[VoxelAABBCacher::Create] Cacher not initialized with device");
    }

    if (!ci.sceneData) {
        throw std::runtime_error("[VoxelAABBCacher::Create] Scene data is null");
    }

    auto aabbData = std::make_shared<VoxelAABBData>();
    aabbData->gridResolution = ci.gridResolution;
    aabbData->voxelSize = ci.voxelSize;

    // Extract AABBs from scene data
    std::vector<VoxelAABB> aabbs;
    std::vector<uint32_t> materialIds;
    std::vector<VoxelBrickMapping> brickMappings;

    ExtractAABBsFromSceneData(*ci.sceneData, ci.voxelSize, aabbs, materialIds, brickMappings);

    if (aabbs.empty()) {
        LOG_INFO("[VoxelAABBCacher::Create] No solid voxels found - 0 AABBs");
        aabbData->aabbCount = 0;
        return aabbData;
    }

    aabbData->aabbCount = static_cast<uint32_t>(aabbs.size());

    // Upload to GPU
    UploadToGPU(*aabbData, aabbs, materialIds, brickMappings);

    LOG_INFO("[VoxelAABBCacher::Create] Created " + std::to_string(aabbData->aabbCount) + " AABBs");

    return aabbData;
}

std::uint64_t VoxelAABBCacher::ComputeKey(const VoxelAABBCreateInfo& ci) const {
    return ci.ComputeHash();
}

void VoxelAABBCacher::Cleanup() {
    LOG_INFO("[VoxelAABBCacher::Cleanup] Cleaning up cached AABB data");

    // Cleanup all cached entries
    for (auto& [key, entry] : m_entries) {
        if (entry.resource) {
            entry.resource->Cleanup(m_device->device);
        }
    }

    // Destroy command pool
    if (m_transferCommandPool != VK_NULL_HANDLE && m_device) {
        vkDestroyCommandPool(m_device->device, m_transferCommandPool, nullptr);
        m_transferCommandPool = VK_NULL_HANDLE;
    }

    // Clear entries
    Clear();

    LOG_INFO("[VoxelAABBCacher::Cleanup] Cleanup complete");
}

// ============================================================================
// SERIALIZATION
// ============================================================================
// Note: VoxelAABBCacher deliberately does not persist to disk.
// Reasons:
// 1. VoxelSceneData is already cached/serialized by VoxelSceneCacher
// 2. AABB extraction from cached scene data is fast (CPU iteration only)
// 3. GPU buffers must be recreated per-device anyway
//
// The cacher provides value via in-memory caching during a single run
// (avoiding repeated AABB extraction for same scene).
// ============================================================================

bool VoxelAABBCacher::SerializeToFile(const std::filesystem::path& path) const {
    (void)path;
    return true;  // Intentional no-op
}

bool VoxelAABBCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    (void)path;
    (void)device;
    return true;  // Intentional no-op
}

// ============================================================================
// PRIVATE HELPER METHODS
// ============================================================================

void VoxelAABBCacher::ExtractAABBsFromSceneData(
    const VoxelSceneData& sceneData,
    float voxelSize,
    std::vector<VoxelAABB>& outAABBs,
    std::vector<uint32_t>& outMaterialIds,
    std::vector<VoxelBrickMapping>& outBrickMappings)
{
    LOG_INFO("[VoxelAABBCacher::ExtractAABBsFromSceneData] Extracting AABBs...");

    if (sceneData.brickDataCPU.empty()) {
        LOG_DEBUG("[VoxelAABBCacher::ExtractAABBsFromSceneData] No brick data - 0 AABBs");
        return;
    }

    const auto* brickData = reinterpret_cast<const uint32_t*>(sceneData.brickDataCPU.data());
    const size_t brickCount = sceneData.brickCount;
    constexpr size_t VOXELS_PER_BRICK = 512;  // 8x8x8
    constexpr int BRICK_SIZE = 8;

    LOG_DEBUG("[ExtractAABBsFromSceneData] brickCount=" + std::to_string(brickCount) +
              ", brickGridLookupSize=" + std::to_string(sceneData.brickGridLookupCPU.size()) +
              ", bricksPerAxis=" + std::to_string(sceneData.configCPU.bricksPerAxis));

    // Reserve based on solid voxel count estimate
    outAABBs.reserve(sceneData.solidVoxelCount);
    outMaterialIds.reserve(sceneData.solidVoxelCount);
    outBrickMappings.reserve(sceneData.solidVoxelCount);

    // Use brick grid lookup if available
    const bool haveLookup = !sceneData.brickGridLookupCPU.empty();
    const uint32_t bricksPerAxis = sceneData.configCPU.bricksPerAxis;

    // World scale factor (grid is normalized to [0, worldGridSize])
    const float worldGridSize = sceneData.configCPU.worldGridSize;
    const float voxelWorldSize = worldGridSize / static_cast<float>(sceneData.resolution);

    // Use provided voxelSize for AABB generation
    const float aabbVoxelSize = voxelSize;

    // Iterate all bricks and emit AABBs for solid voxels
    size_t bricksFound = 0, bricksSkipped = 0, totalSolidVoxels = 0;
    for (size_t brickIdx = 0; brickIdx < brickCount; ++brickIdx) {
        // Find brick grid coordinates by searching brickGridLookup
        int brickX = -1, brickY = -1, brickZ = -1;
        if (haveLookup) {
            // Reverse lookup: find grid coord that maps to this brickIdx
            for (uint32_t idx = 0; idx < sceneData.brickGridLookupCPU.size(); ++idx) {
                if (sceneData.brickGridLookupCPU[idx] == brickIdx) {
                    brickX = idx % bricksPerAxis;
                    brickY = (idx / bricksPerAxis) % bricksPerAxis;
                    brickZ = idx / (bricksPerAxis * bricksPerAxis);
                    break;
                }
            }
        } else {
            // Fallback: assume linear ordering
            brickX = static_cast<int>(brickIdx % bricksPerAxis);
            brickY = static_cast<int>((brickIdx / bricksPerAxis) % bricksPerAxis);
            brickZ = static_cast<int>(brickIdx / (bricksPerAxis * bricksPerAxis));
        }

        if (brickX < 0) {
            bricksSkipped++;
            continue;  // Brick not found in lookup
        }
        bricksFound++;

        // Brick world origin
        const float brickOriginX = static_cast<float>(brickX * BRICK_SIZE) * aabbVoxelSize;
        const float brickOriginY = static_cast<float>(brickY * BRICK_SIZE) * aabbVoxelSize;
        const float brickOriginZ = static_cast<float>(brickZ * BRICK_SIZE) * aabbVoxelSize;

        // Iterate voxels in this brick
        const uint32_t* brickVoxels = brickData + brickIdx * VOXELS_PER_BRICK;

        for (int lz = 0; lz < BRICK_SIZE; ++lz) {
            for (int ly = 0; ly < BRICK_SIZE; ++ly) {
                for (int lx = 0; lx < BRICK_SIZE; ++lx) {
                    const size_t localIdx = lz * BRICK_SIZE * BRICK_SIZE + ly * BRICK_SIZE + lx;
                    const uint32_t materialId = brickVoxels[localIdx];

                    if (materialId == 0) continue;  // Empty voxel
                    totalSolidVoxels++;

                    // Compute world-space AABB
                    const float minX = brickOriginX + static_cast<float>(lx) * aabbVoxelSize;
                    const float minY = brickOriginY + static_cast<float>(ly) * aabbVoxelSize;
                    const float minZ = brickOriginZ + static_cast<float>(lz) * aabbVoxelSize;

                    VoxelAABB aabb;
                    aabb.min = glm::vec3(minX, minY, minZ);
                    aabb.max = glm::vec3(minX + aabbVoxelSize, minY + aabbVoxelSize, minZ + aabbVoxelSize);
                    outAABBs.push_back(aabb);

                    outMaterialIds.push_back(materialId);

                    VoxelBrickMapping mapping;
                    mapping.brickIndex = static_cast<uint32_t>(brickIdx);
                    mapping.localVoxelIdx = static_cast<uint32_t>(localIdx);
                    outBrickMappings.push_back(mapping);
                }
            }
        }
    }

    LOG_INFO("[ExtractAABBsFromSceneData] Generated " + std::to_string(outAABBs.size()) + " AABBs" +
             " (bricksFound=" + std::to_string(bricksFound) +
             ", bricksSkipped=" + std::to_string(bricksSkipped) +
             ", solidVoxels=" + std::to_string(totalSolidVoxels) + ")");
}

void VoxelAABBCacher::UploadToGPU(
    VoxelAABBData& aabbData,
    const std::vector<VoxelAABB>& aabbs,
    const std::vector<uint32_t>& materialIds,
    const std::vector<VoxelBrickMapping>& brickMappings)
{
    if (aabbs.empty()) {
        return;
    }

    // Create AABB buffer
    {
        aabbData.aabbBufferSize = aabbs.size() * sizeof(VoxelAABB);

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = aabbData.aabbBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &aabbData.aabbBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, aabbData.aabbBuffer, &memReq);

        VkMemoryAllocateFlagsInfo allocFlagsInfo{};
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &allocFlagsInfo;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(m_device->device, &allocInfo, nullptr, &aabbData.aabbBufferMemory);
        vkBindBufferMemory(m_device->device, aabbData.aabbBuffer, aabbData.aabbBufferMemory, 0);

        UploadBufferData(aabbData.aabbBuffer, aabbs.data(), aabbData.aabbBufferSize);
    }

    // Create material ID buffer
    {
        aabbData.materialIdBufferSize = materialIds.size() * sizeof(uint32_t);

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = aabbData.materialIdBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &aabbData.materialIdBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, aabbData.materialIdBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(m_device->device, &allocInfo, nullptr, &aabbData.materialIdBufferMemory);
        vkBindBufferMemory(m_device->device, aabbData.materialIdBuffer, aabbData.materialIdBufferMemory, 0);

        UploadBufferData(aabbData.materialIdBuffer, materialIds.data(), aabbData.materialIdBufferSize);
    }

    // Create brick mapping buffer
    {
        aabbData.brickMappingBufferSize = brickMappings.size() * sizeof(VoxelBrickMapping);

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = aabbData.brickMappingBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &aabbData.brickMappingBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, aabbData.brickMappingBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(m_device->device, &allocInfo, nullptr, &aabbData.brickMappingBufferMemory);
        vkBindBufferMemory(m_device->device, aabbData.brickMappingBuffer, aabbData.brickMappingBufferMemory, 0);

        UploadBufferData(aabbData.brickMappingBuffer, brickMappings.data(), aabbData.brickMappingBufferSize);
    }

    LOG_INFO("[VoxelAABBCacher::UploadToGPU] Uploaded buffers: " +
             std::to_string(aabbData.aabbBufferSize / 1024.0f) + " KB AABBs, " +
             std::to_string(aabbData.materialIdBufferSize / 1024.0f) + " KB materials, " +
             std::to_string(aabbData.brickMappingBufferSize / 1024.0f) + " KB mappings");
}

uint32_t VoxelAABBCacher::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    const VkPhysicalDeviceMemoryProperties& memProperties = m_device->gpuMemoryProperties;

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("[VoxelAABBCacher::FindMemoryType] Failed to find suitable memory type");
}

void VoxelAABBCacher::UploadBufferData(VkBuffer buffer, const void* srcData, VkDeviceSize size) {
    if (size == 0 || !srcData) {
        return;
    }

    // Create command pool if needed
    if (m_transferCommandPool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_device->graphicsQueueIndex;
        vkCreateCommandPool(m_device->device, &poolInfo, nullptr, &m_transferCommandPool);
    }

    // Create staging buffer
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = size;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer stagingBuffer;
    vkCreateBuffer(m_device->device, &stagingInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements stagingMemReq;
    vkGetBufferMemoryRequirements(m_device->device, stagingBuffer, &stagingMemReq);

    VkMemoryAllocateInfo stagingAllocInfo{};
    stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocInfo.allocationSize = stagingMemReq.size;
    stagingAllocInfo.memoryTypeIndex = FindMemoryType(
        stagingMemReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    VkDeviceMemory stagingMemory;
    vkAllocateMemory(m_device->device, &stagingAllocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(m_device->device, stagingBuffer, stagingMemory, 0);

    // Copy data to staging buffer
    void* mappedData;
    vkMapMemory(m_device->device, stagingMemory, 0, size, 0, &mappedData);
    std::memcpy(mappedData, srcData, size);
    vkUnmapMemory(m_device->device, stagingMemory);

    // Record and submit copy command
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_transferCommandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(m_device->device, &cmdAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, buffer, 1, &copyRegion);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(m_device->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_device->queue);

    // Cleanup staging resources
    vkFreeCommandBuffers(m_device->device, m_transferCommandPool, 1, &cmdBuffer);
    vkDestroyBuffer(m_device->device, stagingBuffer, nullptr);
    vkFreeMemory(m_device->device, stagingMemory, nullptr);
}

} // namespace CashSystem
