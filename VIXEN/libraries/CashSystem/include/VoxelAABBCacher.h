#pragma once

#include "TypedCacher.h"
#include "MainCacher.h"
#include "VoxelSceneCacher.h"
#include "Memory/IMemoryAllocator.h"

// Note: BatchedUploader included via TypedCacher.h (Sprint 5 Phase 2.5.2)

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <memory>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {

// ============================================================================
// VOXEL AABB STRUCTURES
// ============================================================================

/**
 * @brief Single voxel AABB for acceleration structure building
 *
 * Layout matches VkAabbPositionsKHR (6 floats, tightly packed)
 */
struct VoxelAABB {
    glm::vec3 min;  // Minimum corner (x, y, z)
    glm::vec3 max;  // Maximum corner (x+1, y+1, z+1)
};
static_assert(sizeof(VoxelAABB) == 24, "VoxelAABB must be 24 bytes for VkAabbPositionsKHR");

/**
 * @brief Brick mapping entry for compressed RTX shaders
 *
 * Maps each AABB primitive to its brick and local voxel position.
 * Packed as uvec2 in shader: (brickIndex, localVoxelIdx)
 */
struct VoxelBrickMapping {
    uint32_t brickIndex;      // Index into compressed buffer arrays
    uint32_t localVoxelIdx;   // Position within brick (0-511)
};
static_assert(sizeof(VoxelBrickMapping) == 8, "VoxelBrickMapping must be 8 bytes for uvec2");

// ============================================================================
// VOXEL AABB DATA
// ============================================================================

/**
 * @brief Complete AABB data for acceleration structure building
 *
 * Contains GPU buffers for AABBs, material IDs, and brick mappings.
 * Created and managed by VoxelAABBCacher.
 *
 * Uses BufferAllocation for proper memory management via allocator infrastructure.
 */
struct VoxelAABBData {
    // AABB buffer - VkAabbPositionsKHR array
    ResourceManagement::BufferAllocation aabbAllocation{};
    uint32_t aabbCount = 0;

    // Material ID buffer - one uint32 per AABB, indexed by gl_PrimitiveID
    ResourceManagement::BufferAllocation materialIdAllocation{};

    // Brick mapping buffer - one VoxelBrickMapping per AABB
    ResourceManagement::BufferAllocation brickMappingAllocation{};

    // Grid info for SVO lookup
    uint32_t gridResolution = 0;
    float voxelSize = 1.0f;

    // ===== Convenience accessors for backward compatibility =====
    VkBuffer GetAABBBuffer() const noexcept { return aabbAllocation.buffer; }
    VkBuffer GetMaterialIdBuffer() const noexcept { return materialIdAllocation.buffer; }
    VkBuffer GetBrickMappingBuffer() const noexcept { return brickMappingAllocation.buffer; }
    VkDeviceSize GetAABBBufferSize() const noexcept { return aabbAllocation.size; }
    VkDeviceSize GetMaterialIdBufferSize() const noexcept { return materialIdAllocation.size; }
    VkDeviceSize GetBrickMappingBufferSize() const noexcept { return brickMappingAllocation.size; }
    VkDeviceAddress GetAABBDeviceAddress() const noexcept { return aabbAllocation.deviceAddress; }

    bool IsValid() const noexcept {
        return aabbAllocation.buffer != VK_NULL_HANDLE && aabbCount > 0;
    }

    // Note: Cleanup now handled by cacher via FreeBufferTracked()
};

// ============================================================================
// VOXEL AABB CREATE INFO
// ============================================================================

/**
 * @brief Creation parameters for cached AABB data
 *
 * Key: (sceneDataKey, voxelSize, gridResolution)
 * The sceneDataKey comes from VoxelSceneCreateInfo::ComputeHash().
 */
struct VoxelAABBCreateInfo {
    // Key to VoxelSceneCacher entry (from VoxelSceneCreateInfo::ComputeHash())
    uint64_t sceneDataKey = 0;

    // Pointer to cached scene data (must be valid during Create())
    std::shared_ptr<VoxelSceneData> sceneData;

    // AABB generation parameters
    float voxelSize = 1.0f;         // Size of each voxel AABB in world units
    uint32_t gridResolution = 128;  // Grid resolution (for validation)

    /**
     * @brief Compute hash for cache key
     */
    uint64_t ComputeHash() const noexcept {
        // Quantize voxelSize to prevent floating-point hash instability
        uint32_t voxelSizeQuantized = static_cast<uint32_t>(voxelSize * 10000.0f);

        uint64_t hash = sceneDataKey;
        hash = hash * 31 + voxelSizeQuantized;
        hash = hash * 31 + gridResolution;
        return hash;
    }

    bool operator==(const VoxelAABBCreateInfo& other) const noexcept {
        return sceneDataKey == other.sceneDataKey &&
               static_cast<uint32_t>(voxelSize * 10000) == static_cast<uint32_t>(other.voxelSize * 10000) &&
               gridResolution == other.gridResolution;
    }
};

// ============================================================================
// VOXEL AABB CACHER
// ============================================================================

/**
 * @brief Cacher for AABB extraction from voxel scene data
 *
 * Extracts axis-aligned bounding boxes from VoxelSceneData for hardware
 * ray tracing acceleration structure construction.
 *
 * Key: (sceneDataKey, voxelSize, gridResolution)
 *
 * Output: VoxelAABBData containing:
 * - aabbBuffer: GPU buffer with VoxelAABB array (VkAabbPositionsKHR compatible)
 * - materialIdBuffer: GPU buffer with material IDs per AABB
 * - brickMappingBuffer: GPU buffer with brick mappings per AABB
 * - aabbCount: Number of AABBs extracted
 *
 * Thread-safe via TypedCacher's shared_mutex.
 *
 * @note This cacher is device-dependent (GPU buffers).
 */
class VoxelAABBCacher : public TypedCacher<VoxelAABBData, VoxelAABBCreateInfo> {
public:
    VoxelAABBCacher() = default;
    ~VoxelAABBCacher() override = default;

    /**
     * @brief Get or create cached AABB data
     *
     * If AABB data with matching key exists, returns cached data.
     * Otherwise, extracts AABBs from scene data and uploads to GPU.
     *
     * @param ci AABB creation parameters
     * @return Shared pointer to cached AABB data
     */
    std::shared_ptr<VoxelAABBData> GetOrCreate(const VoxelAABBCreateInfo& ci);

    // ===== TypedCacher interface =====
    std::string_view name() const noexcept override { return "VoxelAABBCacher"; }

    // Serialization (stub - AABBs are cheap to regenerate from cached scene data)
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;

protected:
    // ===== TypedCacher implementation =====
    std::shared_ptr<VoxelAABBData> Create(const VoxelAABBCreateInfo& ci) override;
    std::uint64_t ComputeKey(const VoxelAABBCreateInfo& ci) const override;

    // Resource cleanup
    void Cleanup() override;

private:
    // ===== Helper methods =====

    /**
     * @brief Extract AABBs from scene data
     *
     * Iterates through brick data and generates AABBs for solid voxels.
     * Also generates material IDs and brick mappings for RT shaders.
     *
     * @param sceneData Source scene data with bricks
     * @param voxelSize Size of each voxel in world units
     * @param[out] outAABBs Vector to receive AABB data
     * @param[out] outMaterialIds Vector to receive material IDs
     * @param[out] outBrickMappings Vector to receive brick mappings
     */
    void ExtractAABBsFromSceneData(
        const VoxelSceneData& sceneData,
        float voxelSize,
        std::vector<VoxelAABB>& outAABBs,
        std::vector<uint32_t>& outMaterialIds,
        std::vector<VoxelBrickMapping>& outBrickMappings);

    /**
     * @brief Upload AABB data to GPU buffers
     *
     * Creates device-local buffers and uploads via staging.
     */
    void UploadToGPU(
        VoxelAABBData& aabbData,
        const std::vector<VoxelAABB>& aabbs,
        const std::vector<uint32_t>& materialIds,
        const std::vector<VoxelBrickMapping>& brickMappings);

    // Note: BatchedUploader is in TypedCacher base class via GetUploader()
};

// ============================================================================
// REGISTRATION HELPER
// ============================================================================

/**
 * @brief Register VoxelAABBCacher with MainCacher
 *
 * Call during application initialization before using the cacher.
 */
inline void RegisterVoxelAABBCacher() {
    MainCacher::Instance().RegisterCacher<VoxelAABBCacher, VoxelAABBData, VoxelAABBCreateInfo>(
        std::type_index(typeid(VoxelAABBData)),
        "VoxelAABBCacher",
        true  // device-dependent
    );
}

} // namespace CashSystem
