#pragma once

#include "TypedCacher.h"
#include "MainCacher.h"
#include "VoxelAABBCacher.h"  // VoxelAABBData, VoxelAABB, VoxelBrickMapping

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {

// ============================================================================
// ACCELERATION STRUCTURE DATA
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

    // Build timing (measured during creation)
    float blasBuildTimeMs = 0.0f;
    float tlasBuildTimeMs = 0.0f;

    bool IsValid() const noexcept {
        return blas != VK_NULL_HANDLE && tlas != VK_NULL_HANDLE;
    }

    void Cleanup(VkDevice device);
};

// ============================================================================
// CACHED ACCELERATION STRUCTURE (Combined wrapper)
// ============================================================================

/**
 * @brief Cached Acceleration Structure (BLAS + TLAS)
 *
 * Contains BLAS/TLAS for ray queries. Self-contained after creation -
 * no external dependencies. Stores metadata (AABB count) from source
 * data for validation, but does not retain pointer to source.
 */
struct CachedAccelerationStructure {
    // Acceleration structure data (owned by this struct)
    AccelerationStructureData accelStruct;

    // Metadata from source AABB data (stored at creation, no pointer dependency)
    uint32_t sourceAABBCount = 0;

    bool IsValid() const noexcept {
        return sourceAABBCount > 0 && accelStruct.IsValid();
    }

    void Cleanup(VkDevice device);
};

// ============================================================================
// ACCELERATION STRUCTURE CREATE INFO
// ============================================================================

/**
 * @brief Creation parameters for cached acceleration structure
 *
 * Key: AABB data pointer + build flags.
 * Same AABB data with different build flags produces different AS.
 */
struct AccelStructCreateInfo {
    // AABB data from VoxelAABBCacher (required - must be valid during Create())
    VoxelAABBData* aabbData = nullptr;

    // Build flags
    bool preferFastTrace = true;    // VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
    bool allowUpdate = false;       // VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
    bool allowCompaction = true;    // VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR

    /**
     * @brief Compute hash for cache key
     */
    uint64_t ComputeHash() const noexcept {
        // Use AABB buffer address as key (unique per AABB data instance)
        uint64_t hash = aabbData ? reinterpret_cast<uint64_t>(aabbData->GetAABBBuffer()) : 0;
        hash = hash * 31 + (preferFastTrace ? 1 : 0);
        hash = hash * 31 + (allowUpdate ? 2 : 0);
        hash = hash * 31 + (allowCompaction ? 4 : 0);
        return hash;
    }

    bool operator==(const AccelStructCreateInfo& other) const noexcept {
        return aabbData == other.aabbData &&
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
 * Builds BLAS/TLAS from pre-extracted AABB data (from VoxelAABBCacher).
 * Key: (AABB buffer address, buildFlags)
 *
 * Thread-safe via TypedCacher's shared_mutex.
 *
 * @note This cacher is device-dependent (Vulkan RT extension).
 * @note AABB extraction is now handled by VoxelAABBCacher, not here.
 */
class AccelerationStructureCacher : public TypedCacher<CachedAccelerationStructure, AccelStructCreateInfo> {
public:
    AccelerationStructureCacher() = default;
    ~AccelerationStructureCacher() override = default;

    /**
     * @brief Get or create cached acceleration structure
     *
     * @param ci Creation parameters including AABB data reference
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
     * @brief Build BLAS from AABB buffer
     */
    void BuildBLAS(const AccelStructCreateInfo& ci, const VoxelAABBData& aabbData, AccelerationStructureData& asData);

    /**
     * @brief Build TLAS containing single BLAS instance
     */
    void BuildTLAS(const AccelStructCreateInfo& ci, AccelerationStructureData& asData);

    /**
     * @brief Get VkBuildAccelerationStructureFlagsKHR from create info
     */
    VkBuildAccelerationStructureFlagsKHR GetBuildFlags(const AccelStructCreateInfo& ci) const;

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
