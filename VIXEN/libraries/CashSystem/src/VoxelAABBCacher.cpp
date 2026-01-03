#include "pch.h"
#include "VoxelAABBCacher.h"
#include "VulkanDevice.h"
#include "error/VulkanError.h"
#include "Memory/BatchedUploader.h"  // For InvalidUploadHandle

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

    // Free all cached buffer allocations via FreeBufferTracked
    for (auto& [key, entry] : m_entries) {
        if (entry.resource) {
            FreeBufferTracked(entry.resource->aabbAllocation);
            FreeBufferTracked(entry.resource->materialIdAllocation);
            FreeBufferTracked(entry.resource->brickMappingAllocation);
        }
    }

    // Note: BatchedUploader now owned by VulkanDevice - no cleanup needed here

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

    // Compute sizes upfront
    const VkDeviceSize aabbSize = aabbs.size() * sizeof(VoxelAABB);
    const VkDeviceSize materialSize = materialIds.size() * sizeof(uint32_t);
    const VkDeviceSize mappingSize = brickMappings.size() * sizeof(VoxelBrickMapping);

    // Allocate all buffers first, then upload - ensures exception safety
    // If any allocation fails, we clean up all previous allocations before throwing

    // 1. Allocate AABB buffer (device-local with device address for RT)
    auto aabbAlloc = AllocateBufferTracked(
        aabbSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "VoxelAABB_aabbs"
    );
    if (!aabbAlloc) {
        throw std::runtime_error("[VoxelAABBCacher::UploadToGPU] Failed to allocate AABB buffer");
    }

    // 2. Allocate material ID buffer (device-local)
    auto materialAlloc = AllocateBufferTracked(
        materialSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "VoxelAABB_materials"
    );
    if (!materialAlloc) {
        FreeBufferTracked(*aabbAlloc);
        throw std::runtime_error("[VoxelAABBCacher::UploadToGPU] Failed to allocate material ID buffer");
    }

    // 3. Allocate brick mapping buffer (device-local)
    auto mappingAlloc = AllocateBufferTracked(
        mappingSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "VoxelAABB_mappings"
    );
    if (!mappingAlloc) {
        FreeBufferTracked(*aabbAlloc);
        FreeBufferTracked(*materialAlloc);
        throw std::runtime_error("[VoxelAABBCacher::UploadToGPU] Failed to allocate brick mapping buffer");
    }

    // All allocations succeeded - now upload data
    // Upload can still throw, but allocations are tracked via aabbData after assignment
    aabbData.aabbAllocation = *aabbAlloc;
    aabbData.materialIdAllocation = *materialAlloc;
    aabbData.brickMappingAllocation = *mappingAlloc;

    // Upload buffer data via VulkanDevice (Sprint 5 Phase 2.5.3)
    // Centralized upload API hides staging/batching mechanics
    if (!m_device->HasUploadSupport()) {
        throw std::runtime_error("[VoxelAABBCacher::UploadToGPU] Upload infrastructure not configured");
    }

    // Queue all uploads (non-blocking)
    auto handle1 = m_device->Upload(aabbs.data(), aabbSize, aabbData.aabbAllocation.buffer, 0);
    auto handle2 = m_device->Upload(materialIds.data(), materialSize, aabbData.materialIdAllocation.buffer, 0);
    auto handle3 = m_device->Upload(brickMappings.data(), mappingSize, aabbData.brickMappingAllocation.buffer, 0);

    if (handle1 == ResourceManagement::InvalidUploadHandle ||
        handle2 == ResourceManagement::InvalidUploadHandle ||
        handle3 == ResourceManagement::InvalidUploadHandle) {
        throw std::runtime_error("[VoxelAABBCacher::UploadToGPU] Failed to queue uploads");
    }

    // Flush all queued uploads in a single batch and wait for completion
    m_device->WaitAllUploads();

    LOG_INFO("[VoxelAABBCacher::UploadToGPU] Uploaded buffers (via BatchedUploader): " +
             std::to_string(aabbData.aabbAllocation.size / 1024.0f) + " KB AABBs, " +
             std::to_string(aabbData.materialIdAllocation.size / 1024.0f) + " KB materials, " +
             std::to_string(aabbData.brickMappingAllocation.size / 1024.0f) + " KB mappings");
}

// NOTE: UploadBufferData removed - replaced by BatchedUploader in UploadToGPU (Sprint 5 Phase 2.5.2)

} // namespace CashSystem
