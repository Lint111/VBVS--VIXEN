#include "pch.h"
#include "AccelerationStructureCacher.h"
#include "VulkanDevice.h"

// SVO library for ESVO node parsing and complete types for unique_ptr
#include "SVOTypes.h"
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"
#include "Data/SceneGenerator.h"

#include <iostream>
#include <cstring>
#include <stdexcept>
#include <bit>

namespace CashSystem {

// ============================================================================
// VOXEL AABB DATA - CLEANUP
// ============================================================================

void VoxelAABBData::Cleanup(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    if (aabbBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, aabbBuffer, nullptr);
        aabbBuffer = VK_NULL_HANDLE;
    }
    if (aabbBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, aabbBufferMemory, nullptr);
        aabbBufferMemory = VK_NULL_HANDLE;
    }
    if (materialIdBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, materialIdBuffer, nullptr);
        materialIdBuffer = VK_NULL_HANDLE;
    }
    if (materialIdBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, materialIdBufferMemory, nullptr);
        materialIdBufferMemory = VK_NULL_HANDLE;
    }
    if (brickMappingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, brickMappingBuffer, nullptr);
        brickMappingBuffer = VK_NULL_HANDLE;
    }
    if (brickMappingBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, brickMappingBufferMemory, nullptr);
        brickMappingBufferMemory = VK_NULL_HANDLE;
    }

    aabbCount = 0;
    aabbBufferSize = 0;
    materialIdBufferSize = 0;
    brickMappingBufferSize = 0;
}

// ============================================================================
// ACCELERATION STRUCTURE DATA - CLEANUP
// ============================================================================

void AccelerationStructureData::Cleanup(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    // Load destroy function if needed
    auto destroyAS = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR")
    );

    if (destroyAS) {
        if (blas != VK_NULL_HANDLE) {
            destroyAS(device, blas, nullptr);
            blas = VK_NULL_HANDLE;
        }
        if (tlas != VK_NULL_HANDLE) {
            destroyAS(device, tlas, nullptr);
            tlas = VK_NULL_HANDLE;
        }
    }

    // Destroy BLAS resources
    if (blasBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, blasBuffer, nullptr);
        blasBuffer = VK_NULL_HANDLE;
    }
    if (blasMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, blasMemory, nullptr);
        blasMemory = VK_NULL_HANDLE;
    }

    // Destroy TLAS resources
    if (tlasBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, tlasBuffer, nullptr);
        tlasBuffer = VK_NULL_HANDLE;
    }
    if (tlasMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, tlasMemory, nullptr);
        tlasMemory = VK_NULL_HANDLE;
    }

    // Destroy instance buffer
    if (instanceBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, instanceBuffer, nullptr);
        instanceBuffer = VK_NULL_HANDLE;
    }
    if (instanceMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, instanceMemory, nullptr);
        instanceMemory = VK_NULL_HANDLE;
    }

    // Destroy scratch buffer
    if (scratchBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, scratchBuffer, nullptr);
        scratchBuffer = VK_NULL_HANDLE;
    }
    if (scratchMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, scratchMemory, nullptr);
        scratchMemory = VK_NULL_HANDLE;
    }

    blasDeviceAddress = 0;
    tlasDeviceAddress = 0;
    primitiveCount = 0;
}

// ============================================================================
// CACHED ACCELERATION STRUCTURE - CLEANUP
// ============================================================================

void CachedAccelerationStructure::Cleanup(VkDevice device) {
    aabbData.Cleanup(device);
    accelStruct.Cleanup(device);
}

// ============================================================================
// ACCELERATION STRUCTURE CACHER - PUBLIC API
// ============================================================================

std::shared_ptr<CachedAccelerationStructure> AccelerationStructureCacher::GetOrCreate(const AccelStructCreateInfo& ci) {
    return TypedCacher<CachedAccelerationStructure, AccelStructCreateInfo>::GetOrCreate(ci);
}

// ============================================================================
// ACCELERATION STRUCTURE CACHER - TYPEDCACHER IMPLEMENTATION
// ============================================================================

std::shared_ptr<CachedAccelerationStructure> AccelerationStructureCacher::Create(const AccelStructCreateInfo& ci) {
    std::cout << "[AccelerationStructureCacher::Create] Creating acceleration structure for scene key "
              << ci.sceneDataKey << std::endl;

    if (!IsInitialized()) {
        throw std::runtime_error("[AccelerationStructureCacher::Create] Cacher not initialized with device");
    }

    if (!ci.sceneData) {
        throw std::runtime_error("[AccelerationStructureCacher::Create] Scene data is null");
    }

    // Load RT extension functions on first use
    LoadRTFunctions();

    auto cached = std::make_shared<CachedAccelerationStructure>();

    // Step 1: Convert scene voxels to AABBs
    ConvertToAABBs(*ci.sceneData, cached->aabbData);

    if (cached->aabbData.aabbCount == 0) {
        std::cout << "[AccelerationStructureCacher::Create] No AABBs to build AS from" << std::endl;
        return cached;
    }

    // Step 2: Build BLAS from AABBs
    BuildBLAS(ci, cached->aabbData, cached->accelStruct);

    // Step 3: Build TLAS containing single BLAS instance
    BuildTLAS(ci, cached->accelStruct);

    std::cout << "[AccelerationStructureCacher::Create] Created AS with "
              << cached->accelStruct.primitiveCount << " primitives" << std::endl;

    return cached;
}

std::uint64_t AccelerationStructureCacher::ComputeKey(const AccelStructCreateInfo& ci) const {
    return ci.ComputeHash();
}

void AccelerationStructureCacher::Cleanup() {
    std::cout << "[AccelerationStructureCacher::Cleanup] Cleaning up cached acceleration structures" << std::endl;

    // Cleanup all cached entries
    for (auto& [key, entry] : m_entries) {
        if (entry.resource) {
            entry.resource->Cleanup(m_device->device);
        }
    }

    // Destroy command pool
    if (m_buildCommandPool != VK_NULL_HANDLE && m_device) {
        vkDestroyCommandPool(m_device->device, m_buildCommandPool, nullptr);
        m_buildCommandPool = VK_NULL_HANDLE;
    }

    // Clear entries
    Clear();

    std::cout << "[AccelerationStructureCacher::Cleanup] Cleanup complete" << std::endl;
}

// ============================================================================
// SERIALIZATION
// ============================================================================
// Note: AccelerationStructureCacher deliberately does not persist to disk.
// Reasons:
// 1. VkAccelerationStructureKHR objects are device-specific (cannot serialize)
// 2. VoxelSceneData is already cached/serialized by VoxelSceneCacher
// 3. AABB conversion from cached scene data is fast (CPU iteration only)
// 4. BLAS/TLAS must be rebuilt per-device anyway
//
// The cacher provides VALUE via in-memory caching during a single benchmark run
// (avoiding repeated BLAS/TLAS builds for same scene). Cross-session persistence
// is handled by VoxelSceneCacher at the scene data level.
// ============================================================================

bool AccelerationStructureCacher::SerializeToFile(const std::filesystem::path& path) const {
    // No-op: AS is device-specific and rebuilt from cached VoxelSceneData
    // (Intentional design - see comment block above)
    return true;
}

bool AccelerationStructureCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // No-op: AS will be rebuilt on-demand when requested via GetOrCreate()
    // (Intentional design - see comment block above)
    return true;
}

// ============================================================================
// PRIVATE HELPER METHODS
// ============================================================================

void AccelerationStructureCacher::LoadRTFunctions() {
    if (m_rtFunctionsLoaded) {
        return;
    }

    VkDevice device = m_device->device;

    vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR")
    );
    vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR")
    );
    vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR")
    );
    vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR")
    );
    vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR")
    );
    vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(
        vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR")
    );

    m_rtFunctionsLoaded = true;

    // Log which functions were loaded
    std::cout << "[AccelerationStructureCacher] RT functions loaded: "
              << "createAS=" << (vkCreateAccelerationStructureKHR ? "yes" : "no")
              << ", buildAS=" << (vkCmdBuildAccelerationStructuresKHR ? "yes" : "no")
              << std::endl;
}

VkBuildAccelerationStructureFlagsKHR AccelerationStructureCacher::GetBuildFlags(const AccelStructCreateInfo& ci) const {
    VkBuildAccelerationStructureFlagsKHR flags = 0;

    if (ci.preferFastTrace) {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    } else {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    }

    if (ci.allowUpdate) {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }

    if (ci.allowCompaction) {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    }

    return flags;
}

uint32_t AccelerationStructureCacher::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    const VkPhysicalDeviceMemoryProperties& memProperties = m_device->gpuMemoryProperties;

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("[AccelerationStructureCacher::FindMemoryType] Failed to find suitable memory type");
}

// ============================================================================
// CONVERT TO AABBs - Iterate ESVO leaf nodes to generate AABBs
// ============================================================================

void AccelerationStructureCacher::ConvertToAABBs(const VoxelSceneData& sceneData, VoxelAABBData& aabbData) {
    std::cout << "[AccelerationStructureCacher::ConvertToAABBs] Converting voxels to AABBs..." << std::endl;

    aabbData.gridResolution = sceneData.resolution;
    aabbData.voxelSize = 1.0f;

    if (sceneData.esvoNodesCPU.empty() || sceneData.brickDataCPU.empty()) {
        std::cout << "[AccelerationStructureCacher::ConvertToAABBs] No ESVO data - 0 AABBs" << std::endl;
        aabbData.aabbCount = 0;
        return;
    }

    // Parse ESVO nodes to find leaf voxels
    const auto* esvoNodes = reinterpret_cast<const Vixen::SVO::ChildDescriptor*>(sceneData.esvoNodesCPU.data());
    const size_t nodeCount = sceneData.esvoNodesCPU.size() / sizeof(Vixen::SVO::ChildDescriptor);

    const auto* brickData = reinterpret_cast<const uint32_t*>(sceneData.brickDataCPU.data());
    const size_t brickCount = sceneData.brickCount;
    constexpr size_t VOXELS_PER_BRICK = 512;  // 8x8x8
    constexpr int BRICK_SIZE = 8;

    // Temporary CPU arrays
    std::vector<VoxelAABB> aabbs;
    std::vector<uint32_t> materialIds;
    std::vector<VoxelBrickMapping> brickMappings;

    // Reserve based on solid voxel count estimate
    aabbs.reserve(sceneData.solidVoxelCount);
    materialIds.reserve(sceneData.solidVoxelCount);
    brickMappings.reserve(sceneData.solidVoxelCount);

    // Use brick grid lookup if available
    const bool haveLookup = !sceneData.brickGridLookupCPU.empty();
    const uint32_t bricksPerAxis = sceneData.configCPU.bricksPerAxis;

    // World scale factor (grid is normalized to [0, WORLD_GRID_SIZE])
    const float worldGridSize = sceneData.configCPU.worldGridSize;
    const float voxelWorldSize = worldGridSize / static_cast<float>(sceneData.resolution);

    // Iterate all bricks and emit AABBs for solid voxels
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

        if (brickX < 0) continue;  // Brick not found in lookup

        // Brick world origin
        const float brickOriginX = static_cast<float>(brickX * BRICK_SIZE) * voxelWorldSize;
        const float brickOriginY = static_cast<float>(brickY * BRICK_SIZE) * voxelWorldSize;
        const float brickOriginZ = static_cast<float>(brickZ * BRICK_SIZE) * voxelWorldSize;

        // Iterate voxels in this brick
        const uint32_t* brickVoxels = brickData + brickIdx * VOXELS_PER_BRICK;

        for (int lz = 0; lz < BRICK_SIZE; ++lz) {
            for (int ly = 0; ly < BRICK_SIZE; ++ly) {
                for (int lx = 0; lx < BRICK_SIZE; ++lx) {
                    const size_t localIdx = lz * BRICK_SIZE * BRICK_SIZE + ly * BRICK_SIZE + lx;
                    const uint32_t materialId = brickVoxels[localIdx];

                    if (materialId == 0) continue;  // Empty voxel

                    // Compute world-space AABB
                    const float minX = brickOriginX + static_cast<float>(lx) * voxelWorldSize;
                    const float minY = brickOriginY + static_cast<float>(ly) * voxelWorldSize;
                    const float minZ = brickOriginZ + static_cast<float>(lz) * voxelWorldSize;

                    VoxelAABB aabb;
                    aabb.min = glm::vec3(minX, minY, minZ);
                    aabb.max = glm::vec3(minX + voxelWorldSize, minY + voxelWorldSize, minZ + voxelWorldSize);
                    aabbs.push_back(aabb);

                    materialIds.push_back(materialId);

                    VoxelBrickMapping mapping;
                    mapping.brickIndex = static_cast<uint32_t>(brickIdx);
                    mapping.localVoxelIdx = static_cast<uint32_t>(localIdx);
                    brickMappings.push_back(mapping);
                }
            }
        }
    }

    aabbData.aabbCount = static_cast<uint32_t>(aabbs.size());
    std::cout << "[AccelerationStructureCacher::ConvertToAABBs] Generated " << aabbData.aabbCount << " AABBs" << std::endl;

    if (aabbData.aabbCount == 0) {
        return;
    }

    // Upload AABB buffer to GPU
    {
        aabbData.aabbBufferSize = aabbs.size() * sizeof(VoxelAABB);

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = aabbData.aabbBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
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

        // Upload via staging
        UploadBufferData(aabbData.aabbBuffer, aabbs.data(), aabbData.aabbBufferSize);
    }

    // Upload material ID buffer
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

    // Upload brick mapping buffer
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

    std::cout << "[AccelerationStructureCacher::ConvertToAABBs] Uploaded AABB buffers ("
              << (aabbData.aabbBufferSize / 1024.0f) << " KB AABBs, "
              << (aabbData.materialIdBufferSize / 1024.0f) << " KB materials, "
              << (aabbData.brickMappingBufferSize / 1024.0f) << " KB mappings)" << std::endl;
}

// ============================================================================
// BUILD BLAS - Create bottom-level acceleration structure from AABBs
// ============================================================================

void AccelerationStructureCacher::BuildBLAS(const AccelStructCreateInfo& ci, VoxelAABBData& aabbData, AccelerationStructureData& asData) {
    std::cout << "[AccelerationStructureCacher::BuildBLAS] Building BLAS..." << std::endl;

    if (!vkCreateAccelerationStructureKHR || !vkGetAccelerationStructureBuildSizesKHR || !vkCmdBuildAccelerationStructuresKHR) {
        std::cout << "[AccelerationStructureCacher::BuildBLAS] RT extension not available - skipping BLAS build" << std::endl;
        return;
    }

    if (aabbData.aabbCount == 0) {
        std::cout << "[AccelerationStructureCacher::BuildBLAS] No AABBs - skipping BLAS build" << std::endl;
        return;
    }

    // Get AABB buffer device address
    VkBufferDeviceAddressInfo aabbAddressInfo{};
    aabbAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    aabbAddressInfo.buffer = aabbData.aabbBuffer;
    VkDeviceAddress aabbDeviceAddress = vkGetBufferDeviceAddressKHR(m_device->device, &aabbAddressInfo);

    // Setup AABB geometry
    VkAccelerationStructureGeometryAabbsDataKHR aabbsData{};
    aabbsData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
    aabbsData.data.deviceAddress = aabbDeviceAddress;
    aabbsData.stride = sizeof(VoxelAABB);

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.aabbs = aabbsData;

    // Build info for size query
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = GetBuildFlags(ci);
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    // Query build sizes
    uint32_t primitiveCount = aabbData.aabbCount;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    vkGetAccelerationStructureBuildSizesKHR(
        m_device->device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo
    );

    std::cout << "[AccelerationStructureCacher::BuildBLAS] BLAS sizes: AS=" << sizeInfo.accelerationStructureSize
              << ", build=" << sizeInfo.buildScratchSize << std::endl;

    // Create BLAS buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeInfo.accelerationStructureSize;
        bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &asData.blasBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, asData.blasBuffer, &memReq);

        VkMemoryAllocateFlagsInfo allocFlagsInfo{};
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &allocFlagsInfo;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(m_device->device, &allocInfo, nullptr, &asData.blasMemory);
        vkBindBufferMemory(m_device->device, asData.blasBuffer, asData.blasMemory, 0);
    }

    // Create scratch buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeInfo.buildScratchSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &asData.scratchBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, asData.scratchBuffer, &memReq);

        VkMemoryAllocateFlagsInfo allocFlagsInfo{};
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &allocFlagsInfo;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(m_device->device, &allocInfo, nullptr, &asData.scratchMemory);
        vkBindBufferMemory(m_device->device, asData.scratchBuffer, asData.scratchMemory, 0);
    }

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = asData.blasBuffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    vkCreateAccelerationStructureKHR(m_device->device, &createInfo, nullptr, &asData.blas);

    // Get scratch buffer device address
    VkBufferDeviceAddressInfo scratchAddressInfo{};
    scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchAddressInfo.buffer = asData.scratchBuffer;
    VkDeviceAddress scratchAddress = vkGetBufferDeviceAddressKHR(m_device->device, &scratchAddressInfo);

    // Update build info with destination and scratch
    buildInfo.dstAccelerationStructure = asData.blas;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    // Build range info
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = primitiveCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    // Create command pool if needed
    if (m_buildCommandPool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_device->graphicsQueueIndex;
        vkCreateCommandPool(m_device->device, &poolInfo, nullptr, &m_buildCommandPool);
    }

    // Allocate and record command buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_buildCommandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(m_device->device, &cmdAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildInfo, &pRangeInfo);
    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(m_device->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_device->queue);

    vkFreeCommandBuffers(m_device->device, m_buildCommandPool, 1, &cmdBuffer);

    // Get BLAS device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = asData.blas;
    asData.blasDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_device->device, &addressInfo);

    asData.primitiveCount = primitiveCount;

    std::cout << "[AccelerationStructureCacher::BuildBLAS] BLAS built successfully, address=0x"
              << std::hex << asData.blasDeviceAddress << std::dec << std::endl;
}

// ============================================================================
// BUILD TLAS - Create top-level acceleration structure with single instance
// ============================================================================

void AccelerationStructureCacher::BuildTLAS(const AccelStructCreateInfo& ci, AccelerationStructureData& asData) {
    std::cout << "[AccelerationStructureCacher::BuildTLAS] Building TLAS..." << std::endl;

    if (!vkCreateAccelerationStructureKHR || !vkCmdBuildAccelerationStructuresKHR) {
        std::cout << "[AccelerationStructureCacher::BuildTLAS] RT extension not available - skipping TLAS build" << std::endl;
        return;
    }

    if (asData.blas == VK_NULL_HANDLE) {
        std::cout << "[AccelerationStructureCacher::BuildTLAS] No BLAS - skipping TLAS build" << std::endl;
        return;
    }

    // Create instance data
    VkAccelerationStructureInstanceKHR instance{};
    // Identity transform
    instance.transform.matrix[0][0] = 1.0f;
    instance.transform.matrix[1][1] = 1.0f;
    instance.transform.matrix[2][2] = 1.0f;
    instance.instanceCustomIndex = 0;
    instance.mask = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = asData.blasDeviceAddress;

    // Create and upload instance buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(VkAccelerationStructureInstanceKHR);
        bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &asData.instanceBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, asData.instanceBuffer, &memReq);

        VkMemoryAllocateFlagsInfo allocFlagsInfo{};
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &allocFlagsInfo;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(m_device->device, &allocInfo, nullptr, &asData.instanceMemory);
        vkBindBufferMemory(m_device->device, asData.instanceBuffer, asData.instanceMemory, 0);

        UploadBufferData(asData.instanceBuffer, &instance, sizeof(instance));
    }

    // Get instance buffer device address
    VkBufferDeviceAddressInfo instanceAddressInfo{};
    instanceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceAddressInfo.buffer = asData.instanceBuffer;
    VkDeviceAddress instanceAddress = vkGetBufferDeviceAddressKHR(m_device->device, &instanceAddressInfo);

    // Setup geometry
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.data.deviceAddress = instanceAddress;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    // Build info for size query
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    // Query build sizes
    uint32_t instanceCount = 1;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    vkGetAccelerationStructureBuildSizesKHR(
        m_device->device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &instanceCount,
        &sizeInfo
    );

    // Create TLAS buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeInfo.accelerationStructureSize;
        bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &asData.tlasBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, asData.tlasBuffer, &memReq);

        VkMemoryAllocateFlagsInfo allocFlagsInfo{};
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &allocFlagsInfo;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(m_device->device, &allocInfo, nullptr, &asData.tlasMemory);
        vkBindBufferMemory(m_device->device, asData.tlasBuffer, asData.tlasMemory, 0);
    }

    // Reuse scratch buffer if large enough, otherwise recreate
    if (asData.scratchBuffer == VK_NULL_HANDLE || sizeInfo.buildScratchSize > 0) {
        // Note: In production, would reuse/resize scratch buffer
        // For simplicity, we keep the BLAS scratch buffer
    }

    // Get scratch buffer address (reusing from BLAS build)
    VkBufferDeviceAddressInfo scratchAddressInfo{};
    scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchAddressInfo.buffer = asData.scratchBuffer;
    VkDeviceAddress scratchAddress = vkGetBufferDeviceAddressKHR(m_device->device, &scratchAddressInfo);

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = asData.tlasBuffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    vkCreateAccelerationStructureKHR(m_device->device, &createInfo, nullptr, &asData.tlas);

    // Update build info
    buildInfo.dstAccelerationStructure = asData.tlas;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    // Build range info
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = instanceCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    // Allocate and record command buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_buildCommandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(m_device->device, &cmdAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildInfo, &pRangeInfo);
    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(m_device->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_device->queue);

    vkFreeCommandBuffers(m_device->device, m_buildCommandPool, 1, &cmdBuffer);

    // Get TLAS device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = asData.tlas;
    asData.tlasDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_device->device, &addressInfo);

    std::cout << "[AccelerationStructureCacher::BuildTLAS] TLAS built successfully, address=0x"
              << std::hex << asData.tlasDeviceAddress << std::dec << std::endl;
}

// ============================================================================
// HELPER - Upload data to GPU buffer via staging
// ============================================================================

void AccelerationStructureCacher::UploadBufferData(VkBuffer buffer, const void* srcData, VkDeviceSize size) {
    if (size == 0 || !srcData) {
        return;
    }

    // Create command pool if needed
    if (m_buildCommandPool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_device->graphicsQueueIndex;
        vkCreateCommandPool(m_device->device, &poolInfo, nullptr, &m_buildCommandPool);
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
    cmdAllocInfo.commandPool = m_buildCommandPool;
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
    vkFreeCommandBuffers(m_device->device, m_buildCommandPool, 1, &cmdBuffer);
    vkDestroyBuffer(m_device->device, stagingBuffer, nullptr);
    vkFreeMemory(m_device->device, stagingMemory, nullptr);
}

} // namespace CashSystem
