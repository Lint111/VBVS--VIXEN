#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <cstdint>

namespace CashSystem {

/**
 * @brief CPU-side tracking of TLAS instances with dirty level detection
 *
 * Manages a collection of BLAS instances for TLAS building. Tracks modifications
 * to determine optimal rebuild strategy:
 * - TransformsOnly: Use VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
 * - StructuralChange: Use VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR
 *
 * Part of Sprint 5 Phase 3: TLAS Lifecycle
 */
class TLASInstanceManager {
public:
    using InstanceId = uint32_t;
    static constexpr InstanceId InvalidId = ~0u;

    /**
     * @brief Instance data for TLAS building
     */
    struct Instance {
        uint64_t blasKey = 0;               ///< Reference to cached BLAS
        VkDeviceAddress blasAddress = 0;    ///< Resolved BLAS device address
        glm::mat3x4 transform{1.0f};        ///< Row-major 3x4 transform (identity default)
        uint32_t customIndex = 0;           ///< SBT record offset / instance ID
        uint8_t mask = 0xFF;                ///< Visibility mask (8-bit)
        VkGeometryInstanceFlagsKHR flags = 0; ///< VK_GEOMETRY_INSTANCE_* flags
        bool active = true;                 ///< False = removed, slot available for reuse
    };

    /**
     * @brief Level of modification since last ClearDirty()
     */
    enum class DirtyLevel : uint8_t {
        Clean,           ///< No changes - no rebuild needed
        TransformsOnly,  ///< Only transforms changed - can use UPDATE mode
        StructuralChange ///< Instances added/removed - must use BUILD mode
    };

    TLASInstanceManager() = default;
    ~TLASInstanceManager() = default;

    // Non-copyable, movable
    TLASInstanceManager(const TLASInstanceManager&) = delete;
    TLASInstanceManager& operator=(const TLASInstanceManager&) = delete;
    TLASInstanceManager(TLASInstanceManager&&) = default;
    TLASInstanceManager& operator=(TLASInstanceManager&&) = default;

    // ========================================================================
    // Instance Lifecycle
    // ========================================================================

    /**
     * @brief Add a new instance
     * @param instance Instance data (blasAddress must be valid)
     * @return Unique ID for this instance, or InvalidId on failure
     *
     * Sets dirty level to StructuralChange.
     */
    InstanceId AddInstance(const Instance& instance);

    /**
     * @brief Update transform for existing instance
     * @param id Instance ID from AddInstance()
     * @param transform New 3x4 row-major transform
     * @return true if updated, false if id invalid
     *
     * Sets dirty level to at least TransformsOnly.
     */
    bool UpdateTransform(InstanceId id, const glm::mat3x4& transform);

    /**
     * @brief Update BLAS address for existing instance
     * @param id Instance ID
     * @param blasAddress New BLAS device address
     * @return true if updated, false if id invalid
     *
     * Sets dirty level to StructuralChange (BLAS reference change).
     */
    bool UpdateBLASAddress(InstanceId id, VkDeviceAddress blasAddress);

    /**
     * @brief Remove an instance
     * @param id Instance ID from AddInstance()
     * @return true if removed, false if id invalid
     *
     * Marks slot for reuse. Sets dirty level to StructuralChange.
     */
    bool RemoveInstance(InstanceId id);

    /**
     * @brief Remove all instances
     *
     * Sets dirty level to StructuralChange if not already empty.
     */
    void Clear();

    // ========================================================================
    // Query
    // ========================================================================

    /**
     * @brief Get count of active (non-removed) instances
     */
    uint32_t GetActiveCount() const { return activeCount_; }

    /**
     * @brief Check if no active instances
     */
    bool IsEmpty() const { return activeCount_ == 0; }

    /**
     * @brief Get total capacity (including removed slots)
     */
    uint32_t GetCapacity() const { return static_cast<uint32_t>(instances_.size()); }

    /**
     * @brief Get instance by ID (const access)
     * @return Pointer to instance, or nullptr if invalid/removed
     */
    const Instance* GetInstance(InstanceId id) const;

    // ========================================================================
    // Dirty Tracking
    // ========================================================================

    /**
     * @brief Get current dirty level
     */
    DirtyLevel GetDirtyLevel() const { return dirtyLevel_; }

    /**
     * @brief Check if any rebuild needed
     */
    bool IsDirty() const { return dirtyLevel_ != DirtyLevel::Clean; }

    /**
     * @brief Reset dirty level after processing
     *
     * Call after TLAS build/update completes successfully.
     */
    void ClearDirty() { dirtyLevel_ = DirtyLevel::Clean; }

    // ========================================================================
    // Vulkan Instance Generation
    // ========================================================================

    /**
     * @brief Generate Vulkan instance array for TLAS build
     * @param[out] out Vector to receive VkAccelerationStructureInstanceKHR data
     *
     * Appends only active instances. Does not clear output vector.
     * Instance order matches increasing InstanceId for active instances.
     */
    void GenerateVulkanInstances(std::vector<VkAccelerationStructureInstanceKHR>& out) const;

    /**
     * @brief Generate Vulkan instance array (convenience overload)
     * @return Vector of VkAccelerationStructureInstanceKHR
     */
    std::vector<VkAccelerationStructureInstanceKHR> GenerateVulkanInstances() const;

private:
    std::vector<Instance> instances_;      ///< All instances (active + removed)
    std::vector<InstanceId> freeList_;     ///< Recycled IDs from removed instances
    uint32_t activeCount_ = 0;             ///< Count of active instances
    DirtyLevel dirtyLevel_ = DirtyLevel::Clean;

    /**
     * @brief Promote dirty level (never demotes)
     */
    void SetDirtyLevel(DirtyLevel level);
};

} // namespace CashSystem
