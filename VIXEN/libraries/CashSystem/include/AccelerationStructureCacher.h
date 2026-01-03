#pragma once

#include "TypedCacher.h"
#include "MainCacher.h"
#include "VoxelAABBCacher.h"  // VoxelAABBData, VoxelAABB, VoxelBrickMapping
#include "Memory/IMemoryAllocator.h"

// Sprint 5 Phase 3: Dynamic TLAS support
#include "TLASInstanceManager.h"
#include "DynamicTLAS.h"
#include "CacheKeyHasher.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {

// ============================================================================
// ACCELERATION STRUCTURE BUILD MODE (Sprint 5 Phase 3)
// ============================================================================

/**
 * @brief Build mode for acceleration structures
 *
 * Determines how BLAS/TLAS are built and cached:
 * - Static: Build once, cache both BLAS and TLAS (current behavior)
 * - Dynamic: Cache BLAS, rebuild TLAS per-frame from mutable instances
 * - SubScene: Cache multiple BLAS regions, rebuild TLAS incrementally
 */
enum class ASBuildMode : uint8_t {
    Static,     ///< Build BLAS+TLAS once, no updates (default, current behavior)
    Dynamic,    ///< Cache BLAS only, manage TLAS per-frame from instances
    SubScene    ///< Cache per-region BLAS, incremental TLAS rebuild (future)
};

// ============================================================================
// ACCELERATION STRUCTURE DATA
// ============================================================================

/**
 * @brief Acceleration structure handles for ray tracing
 *
 * Contains both BLAS (geometry) and TLAS (instances) for the scene.
 * Uses BufferAllocation for proper memory management via allocator infrastructure.
 */
struct AccelerationStructureData {
    // Bottom-Level Acceleration Structure (geometry)
    VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
    ResourceManagement::BufferAllocation blasAllocation{};
    VkDeviceAddress blasDeviceAddress = 0;

    // Top-Level Acceleration Structure (instances)
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    ResourceManagement::BufferAllocation tlasAllocation{};
    VkDeviceAddress tlasDeviceAddress = 0;

    // Instance buffer (for TLAS, host-visible)
    ResourceManagement::BufferAllocation instanceAllocation{};

    // Scratch buffer (temporary, needed during build, freed after)
    ResourceManagement::BufferAllocation scratchAllocation{};

    // Metadata
    uint32_t primitiveCount = 0;

    // Build timing (measured during creation)
    float blasBuildTimeMs = 0.0f;
    float tlasBuildTimeMs = 0.0f;

    // ===== Convenience accessors for backward compatibility =====
    VkBuffer GetBLASBuffer() const noexcept { return blasAllocation.buffer; }
    VkBuffer GetTLASBuffer() const noexcept { return tlasAllocation.buffer; }
    VkBuffer GetInstanceBuffer() const noexcept { return instanceAllocation.buffer; }
    VkBuffer GetScratchBuffer() const noexcept { return scratchAllocation.buffer; }

    bool IsValid() const noexcept {
        return blas != VK_NULL_HANDLE && tlas != VK_NULL_HANDLE;
    }

    // Note: Cleanup now handled by cacher via FreeBufferTracked()
};

// ============================================================================
// CACHED ACCELERATION STRUCTURE (Combined wrapper)
// ============================================================================

/**
 * @brief Cached Acceleration Structure (BLAS + optional dynamic TLAS)
 *
 * Contains BLAS/TLAS for ray queries. Self-contained after creation -
 * no external dependencies. Stores metadata (AABB count) from source
 * data for validation, but does not retain pointer to source.
 *
 * For Dynamic/SubScene modes, also holds TLASInstanceManager and DynamicTLAS.
 */
struct CachedAccelerationStructure {
    // Acceleration structure data (owned by this struct)
    // For Static mode: contains both BLAS and TLAS
    // For Dynamic/SubScene modes: contains BLAS only, TLAS is in dynamicTLAS
    AccelerationStructureData accelStruct;

    // Metadata from source AABB data (stored at creation, no pointer dependency)
    uint32_t sourceAABBCount = 0;

    // Build mode used for this structure (Sprint 5 Phase 3)
    ASBuildMode buildMode = ASBuildMode::Static;

    // Dynamic TLAS support (Sprint 5 Phase 3)
    // These are populated only when buildMode != Static
    std::unique_ptr<TLASInstanceManager> instanceManager;
    std::unique_ptr<DynamicTLAS> dynamicTLAS;

    bool IsValid() const noexcept {
        if (buildMode == ASBuildMode::Static) {
            return sourceAABBCount > 0 && accelStruct.IsValid();
        } else {
            // Dynamic mode: BLAS must be valid, TLAS is managed separately
            return sourceAABBCount > 0 && accelStruct.blas != VK_NULL_HANDLE;
        }
    }

    // Note: Cleanup now handled by AccelerationStructureCacher via FreeBufferTracked()
};

// ============================================================================
// ACCELERATION STRUCTURE CREATE INFO
// ============================================================================

/**
 * @brief Creation parameters for cached acceleration structure
 *
 * Key: AABB data pointer + build flags + build mode.
 * Same AABB data with different build flags produces different AS.
 */
struct AccelStructCreateInfo {
    // AABB data from VoxelAABBCacher (required - must be valid during Create())
    VoxelAABBData* aabbData = nullptr;

    // Build flags
    bool preferFastTrace = true;    // VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
    bool allowUpdate = false;       // VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
    bool allowCompaction = true;    // VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR

    // ===== Sprint 5 Phase 3: Dynamic TLAS support =====

    // Build mode (default: Static for backward compatibility)
    ASBuildMode buildMode = ASBuildMode::Static;

    // For Dynamic/SubScene modes: max instance capacity
    uint32_t maxInstances = 1024;

    // For Dynamic/SubScene modes: swapchain image count (from SwapChainNode)
    // Only used when buildMode != Static
    uint32_t imageCount = 0;

    /**
     * @brief Compute hash for cache key using CacheKeyHasher
     */
    uint64_t ComputeHash() const noexcept {
        CacheKeyHasher hasher;

        // AABB buffer address as key (unique per AABB data instance)
        uint64_t aabbPtr = aabbData ? reinterpret_cast<uint64_t>(aabbData->GetAABBBuffer()) : 0;
        hasher.Add(aabbPtr);

        // Build flags
        hasher.Add(preferFastTrace);
        hasher.Add(allowUpdate);
        hasher.Add(allowCompaction);

        // Build mode (Sprint 5 Phase 3)
        hasher.Add(buildMode);

        return hasher.Finalize();
    }

    bool operator==(const AccelStructCreateInfo& other) const noexcept {
        return aabbData == other.aabbData &&
               preferFastTrace == other.preferFastTrace &&
               allowUpdate == other.allowUpdate &&
               allowCompaction == other.allowCompaction &&
               buildMode == other.buildMode;
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

    // ===== Dynamic Mode Update API (Phase 3.5) =====

    /**
     * @brief Queue TLAS update for a Dynamic mode acceleration structure
     *
     * For Dynamic/SubScene mode entries, queues a TLAS rebuild via the
     * generalized update API (m_device->QueueUpdate). Does nothing for
     * Static mode entries.
     *
     * @param cached Cached acceleration structure (must be Dynamic mode)
     * @param imageIndex Swapchain image index for this frame
     *
     * @note Call device->RecordUpdates(cmd, imageIndex) to record the commands
     */
    void QueueTLASUpdate(CachedAccelerationStructure* cached, uint32_t imageIndex);

    /**
     * @brief Queue TLAS update by cache key
     *
     * Convenience overload that looks up the cached entry by key.
     *
     * @param cacheKey Key returned by AccelStructCreateInfo::ComputeHash()
     * @param imageIndex Swapchain image index for this frame
     */
    void QueueTLASUpdate(uint64_t cacheKey, uint32_t imageIndex);

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
