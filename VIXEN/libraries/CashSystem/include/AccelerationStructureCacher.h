#pragma once

#include "TypedCacher.h"
#include "MainCacher.h"
#include "VoxelSceneCacher.h"

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
// VOXEL AABB STRUCTURES (matching VoxelAABBConverterNodeConfig.h)
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
// VOXEL AABB DATA (from VoxelAABBConverterNode)
// ============================================================================

/**
 * @brief Complete AABB data for acceleration structure building
 *
 * Contains GPU buffers for AABBs, material IDs, and brick mappings.
 */
struct VoxelAABBData {
    // AABB buffer - VkAabbPositionsKHR array
    VkBuffer aabbBuffer = VK_NULL_HANDLE;
    VkDeviceMemory aabbBufferMemory = VK_NULL_HANDLE;
    uint32_t aabbCount = 0;
    VkDeviceSize aabbBufferSize = 0;

    // Material ID buffer - one uint32 per AABB, indexed by gl_PrimitiveID
    VkBuffer materialIdBuffer = VK_NULL_HANDLE;
    VkDeviceMemory materialIdBufferMemory = VK_NULL_HANDLE;
    VkDeviceSize materialIdBufferSize = 0;

    // Brick mapping buffer - one VoxelBrickMapping per AABB
    VkBuffer brickMappingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory brickMappingBufferMemory = VK_NULL_HANDLE;
    VkDeviceSize brickMappingBufferSize = 0;

    // Grid info for SVO lookup
    uint32_t gridResolution = 0;
    float voxelSize = 1.0f;

    bool IsValid() const noexcept {
        return aabbBuffer != VK_NULL_HANDLE && aabbCount > 0;
    }

    void Cleanup(VkDevice device);
};

// ============================================================================
// ACCELERATION STRUCTURE DATA (from AccelerationStructureNode)
// ============================================================================

/**
 * @brief Acceleration structure handles for ray tracing
 *
 * Contains both BLAS (geometry) and TLAS (instances) for the scene.
 */
struct AccelerationStructureData {
    // Bottom-Level Acceleration Structure (geometry)
    VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
    VkBuffer blasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory blasMemory = VK_NULL_HANDLE;
    VkDeviceAddress blasDeviceAddress = 0;

    // Top-Level Acceleration Structure (instances)
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    VkBuffer tlasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory tlasMemory = VK_NULL_HANDLE;
    VkDeviceAddress tlasDeviceAddress = 0;

    // Instance buffer (for TLAS)
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceMemory = VK_NULL_HANDLE;

    // Scratch buffer (temporary, needed during build)
    VkBuffer scratchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory scratchMemory = VK_NULL_HANDLE;

    // Metadata
    uint32_t primitiveCount = 0;

    bool IsValid() const noexcept {
        return blas != VK_NULL_HANDLE && tlas != VK_NULL_HANDLE;
    }

    void Cleanup(VkDevice device);
};

// ============================================================================
// CACHED ACCELERATION STRUCTURE (Combined wrapper)
// ============================================================================

/**
 * @brief Combined AABB + Acceleration Structure data
 *
 * Contains all data needed for hardware ray tracing:
 * - VoxelAABBData: Geometry primitives and mappings
 * - AccelerationStructureData: BLAS/TLAS for ray queries
 */
struct CachedAccelerationStructure {
    VoxelAABBData aabbData;
    AccelerationStructureData accelStruct;

    bool IsValid() const noexcept {
        return aabbData.IsValid() && accelStruct.IsValid();
    }

    void Cleanup(VkDevice device);
};

// ============================================================================
// ACCELERATION STRUCTURE CREATE INFO
// ============================================================================

/**
 * @brief Creation parameters for cached acceleration structure
 *
 * Key: Scene data key + build flags.
 * Same scene with different build flags produces different AS.
 */
struct AccelStructCreateInfo {
    // Key to VoxelSceneCacher entry (from VoxelSceneCreateInfo::ComputeHash())
    uint64_t sceneDataKey = 0;

    // Pointer to cached scene data (must be valid during Create())
    std::shared_ptr<VoxelSceneData> sceneData;

    // Build flags
    bool preferFastTrace = true;    // VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
    bool allowUpdate = false;       // VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
    bool allowCompaction = true;    // VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR

    /**
     * @brief Compute hash for cache key
     */
    uint64_t ComputeHash() const noexcept {
        uint64_t hash = sceneDataKey;
        hash = hash * 31 + (preferFastTrace ? 1 : 0);
        hash = hash * 31 + (allowUpdate ? 2 : 0);
        hash = hash * 31 + (allowCompaction ? 4 : 0);
        return hash;
    }

    bool operator==(const AccelStructCreateInfo& other) const noexcept {
        return sceneDataKey == other.sceneDataKey &&
               preferFastTrace == other.preferFastTrace &&
               allowUpdate == other.allowUpdate &&
               allowCompaction == other.allowCompaction;
    }
};

// ============================================================================
// ACCELERATION STRUCTURE CACHER
// ============================================================================

/**
 * @brief Cacher for acceleration structures
 *
 * Caches the AABB conversion + BLAS/TLAS build pipeline.
 * Key: (sceneDataKey, buildFlags)
 *
 * Thread-safe via TypedCacher's shared_mutex.
 *
 * @note This cacher is device-dependent (Vulkan RT extension).
 */
class AccelerationStructureCacher : public TypedCacher<CachedAccelerationStructure, AccelStructCreateInfo> {
public:
    AccelerationStructureCacher() = default;
    ~AccelerationStructureCacher() override = default;

    /**
     * @brief Get or create cached acceleration structure
     *
     * @param ci Creation parameters including scene data reference
     * @return Shared pointer to cached acceleration structure
     */
    std::shared_ptr<CachedAccelerationStructure> GetOrCreate(const AccelStructCreateInfo& ci);

    // ===== TypedCacher interface =====
    std::string_view name() const noexcept override { return "AccelerationStructureCacher"; }

    // Serialization (stub - AS is device-specific and must be rebuilt)
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;

protected:
    // ===== TypedCacher implementation =====
    std::shared_ptr<CachedAccelerationStructure> Create(const AccelStructCreateInfo& ci) override;
    std::uint64_t ComputeKey(const AccelStructCreateInfo& ci) const override;

    // Resource cleanup
    void Cleanup() override;

private:
    // ===== Helper methods =====

    /**
     * @brief Convert scene voxels to AABB primitives
     */
    void ConvertToAABBs(const VoxelSceneData& sceneData, VoxelAABBData& aabbData);

    /**
     * @brief Build BLAS from AABB buffer
     */
    void BuildBLAS(const AccelStructCreateInfo& ci, VoxelAABBData& aabbData, AccelerationStructureData& asData);

    /**
     * @brief Build TLAS containing single BLAS instance
     */
    void BuildTLAS(const AccelStructCreateInfo& ci, AccelerationStructureData& asData);

    /**
     * @brief Find memory type index for buffer requirements
     */
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    /**
     * @brief Get VkBuildAccelerationStructureFlagsKHR from create info
     */
    VkBuildAccelerationStructureFlagsKHR GetBuildFlags(const AccelStructCreateInfo& ci) const;

    /**
     * @brief Upload data to GPU buffer via staging buffer
     */
    void UploadBufferData(VkBuffer buffer, const void* srcData, VkDeviceSize size);

    // Command pool for AS builds
    VkCommandPool m_buildCommandPool = VK_NULL_HANDLE;

    // Function pointers for RT extension (loaded on first use)
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = nullptr;

    bool m_rtFunctionsLoaded = false;

    /**
     * @brief Load RT extension function pointers
     */
    void LoadRTFunctions();
};

// ============================================================================
// REGISTRATION HELPER
// ============================================================================

/**
 * @brief Register AccelerationStructureCacher with MainCacher
 *
 * Call during application initialization before using the cacher.
 */
inline void RegisterAccelerationStructureCacher() {
    MainCacher::Instance().RegisterCacher<AccelerationStructureCacher, CachedAccelerationStructure, AccelStructCreateInfo>(
        std::type_index(typeid(CachedAccelerationStructure)),
        "AccelerationStructureCacher",
        true  // device-dependent
    );
}

} // namespace CashSystem
