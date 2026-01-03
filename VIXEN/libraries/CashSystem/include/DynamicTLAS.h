#pragma once

#include "TLASInstanceManager.h"
#include "TLASInstanceBuffer.h"
#include "Memory/IMemoryAllocator.h"
#include "State/StatefulContainer.h"
#include "Lifetime/DeferredDestruction.h"
#include "ILoggable.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>
#include <cstdint>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace ResourceManagement {
    class DeviceBudgetManager;
}

namespace CashSystem {

/**
 * @brief Parameters for TLAS build command recording
 *
 * Returned by DynamicTLAS::PrepareBuild() for use by TLASUpdateRequest.
 * Separates data preparation (DynamicTLAS) from command recording (Request).
 */
struct TLASBuildParams {
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    VkAccelerationStructureGeometryKHR geometry{};
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    bool shouldBuild = false;  // false if no instances or allocation failed
    bool isUpdate = false;     // true if using VK_BUILD_MODE_UPDATE
};

/**
 * @brief Per-frame TLAS rebuild orchestrator
 *
 * Manages dynamic TLAS with per-frame updates. Uses:
 * - TLASInstanceManager for CPU-side instance tracking
 * - TLASInstanceBuffer for per-frame GPU instance data
 * - StatefulContainer for per-frame TLAS state
 * - VK_BUILD_MODE_UPDATE for transform-only changes
 *
 * Frame count comes from SwapChainNode (not hardcoded).
 *
 * Part of Sprint 5 Phase 3: TLAS Lifecycle
 */
class DynamicTLAS : public ILoggable {
public:
    /**
     * @brief Configuration for dynamic TLAS
     */
    struct Config {
        uint32_t maxInstances = 1024;       ///< Max instances per TLAS
        bool preferFastTrace = true;        ///< Optimize for trace speed vs build speed
        bool allowUpdate = true;            ///< Enable VK_BUILD_MODE_UPDATE
    };

    DynamicTLAS() = default;
    ~DynamicTLAS();

    // Non-copyable, movable
    DynamicTLAS(const DynamicTLAS&) = delete;
    DynamicTLAS& operator=(const DynamicTLAS&) = delete;
    DynamicTLAS(DynamicTLAS&&) noexcept;
    DynamicTLAS& operator=(DynamicTLAS&&) noexcept;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * @brief Initialize dynamic TLAS
     *
     * Uses VulkanDevice's centralized allocation API.
     *
     * @param device Vulkan device
     * @param imageCount Swapchain image count (from SwapChainNode)
     * @param config Configuration options
     * @return true on success
     */
    bool Initialize(
        Vixen::Vulkan::Resources::VulkanDevice* device,
        uint32_t imageCount,
        const Config& config);

    /**
     * @brief Cleanup all resources
     *
     * @param deferQueue Optional deferred destruction queue for zero-stutter cleanup
     */
    void Cleanup(ResourceManagement::DeferredDestructionQueue* deferQueue = nullptr);

    /**
     * @brief Check if initialized
     */
    bool IsInitialized() const { return device_ != nullptr; }

    // ========================================================================
    // Per-Frame Operations
    // ========================================================================

    /**
     * @brief Update instances for a specific frame
     *
     * Generates Vulkan instance data from manager and writes to frame buffer.
     *
     * @param imageIndex Swapchain image index (from SwapChainNode)
     * @param manager Instance manager with current instance state
     */
    void UpdateInstances(uint32_t imageIndex, const TLASInstanceManager& manager);

    /**
     * @brief Prepare build parameters for TLAS (no command recording)
     *
     * Returns parameters needed for vkCmdBuildAccelerationStructuresKHR.
     * Command recording is handled by TLASUpdateRequest::Record().
     *
     * @param imageIndex Swapchain image index
     * @param dirtyLevel Dirty level from TLASInstanceManager
     * @return Build parameters, with shouldBuild=false if no build needed
     */
    TLASBuildParams PrepareBuild(
        uint32_t imageIndex,
        TLASInstanceManager::DirtyLevel dirtyLevel);

    /**
     * @brief Mark frame as built after successful command recording
     *
     * Called by TLASUpdateRequest after recording build commands.
     *
     * @param imageIndex Swapchain image index
     * @param instanceCount Number of instances built
     */
    void MarkBuilt(uint32_t imageIndex, uint32_t instanceCount);

    /**
     * @brief Build or update TLAS for a specific frame [DEPRECATED]
     *
     * @deprecated Use PrepareBuild() + TLASUpdateRequest instead.
     * This method will be removed after migration is complete.
     */
    [[deprecated("Use PrepareBuild() + TLASUpdateRequest instead")]]
    bool BuildOrUpdate(
        uint32_t imageIndex,
        TLASInstanceManager::DirtyLevel dirtyLevel,
        VkCommandBuffer cmdBuffer);

    // ========================================================================
    // Per-Frame Accessors
    // ========================================================================

    /**
     * @brief Get TLAS handle for specific frame
     * @param imageIndex Swapchain image index
     */
    VkAccelerationStructureKHR GetTLAS(uint32_t imageIndex) const;

    /**
     * @brief Get TLAS device address for specific frame
     * @param imageIndex Swapchain image index
     */
    VkDeviceAddress GetDeviceAddress(uint32_t imageIndex) const;

    /**
     * @brief Get state of specific frame's TLAS
     */
    ResourceManagement::ContainerState GetState(uint32_t imageIndex) const;

    /**
     * @brief Check if specific frame's TLAS is valid and ready
     */
    bool IsValid(uint32_t imageIndex) const;

    // ========================================================================
    // Budget and Memory
    // ========================================================================

    /**
     * @brief Get current total memory usage across all frames
     */
    VkDeviceSize GetCurrentMemoryUsage() const;

    /**
     * @brief Get per-frame memory usage
     */
    VkDeviceSize GetPerFrameMemoryUsage() const;

    /**
     * @brief Get maximum possible memory usage at full capacity
     */
    VkDeviceSize GetMaxMemoryUsage() const;

private:
    /**
     * @brief Per-frame TLAS data
     */
    struct FrameTLAS {
        VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
        ResourceManagement::BufferAllocation tlasBuffer{};
        ResourceManagement::BufferAllocation scratchBuffer{};
        VkDeviceAddress deviceAddress = 0;
        uint32_t lastInstanceCount = 0;  // For detecting structural changes
    };

    Vixen::Vulkan::Resources::VulkanDevice* device_ = nullptr;
    Config config_;

    // Per-frame TLAS (sized to imageCount)
    ResourceManagement::StatefulContainer<FrameTLAS> frameTLAS_;

    // Instance buffer ring (shared across frames for writes)
    TLASInstanceBuffer instanceBuffer_;

    // RT function pointers (loaded on first use)
    PFN_vkCreateAccelerationStructureKHR vkCreateAS_ = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAS_ = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetASSizes_ = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAS_ = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetASAddress_ = nullptr;
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferAddress_ = nullptr;
    bool rtFunctionsLoaded_ = false;

    // Command pool for TLAS builds (created on demand)
    VkCommandPool buildCommandPool_ = VK_NULL_HANDLE;

    // ========================================================================
    // Internal Helpers
    // ========================================================================

    /**
     * @brief Load RT extension function pointers
     */
    void LoadRTFunctions();

    /**
     * @brief Get build flags based on config
     */
    VkBuildAccelerationStructureFlagsKHR GetBuildFlags() const;

    /**
     * @brief Ensure TLAS buffer is allocated for given instance count
     */
    bool EnsureTLASBuffer(uint32_t imageIndex, uint32_t instanceCount);

    /**
     * @brief Validate image index
     */
    bool ValidateImageIndex(uint32_t imageIndex) const;
};

} // namespace CashSystem
