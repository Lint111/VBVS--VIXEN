#pragma once

#include "Headers.h"
#include "VulkanLayerAndExtension.h"
#include "error/VulkanError.h"
#include "CapabilityGraph.h"
#include <memory>

// Forward declarations for upload/update/allocation infrastructure (Sprint 5)
namespace ResourceManagement {
    class BatchedUploader;
    class BatchedUpdater;
    class DeviceBudgetManager;
    class IMemoryAllocator;
    class UpdateRequestBase;
    struct BufferAllocation;
    struct BufferAllocationRequest;
    using UploadHandle = uint64_t;
    using UpdateRequestPtr = std::unique_ptr<UpdateRequestBase>;
    // Note: InvalidUploadHandle defined in BatchedUploader.h (included in .cpp)
}

namespace Vixen::Vulkan::Resources {

/**
 * @brief Ray tracing capability information
 */
struct RTXCapabilities {
    bool supported = false;                    // All required extensions available
    bool accelerationStructure = false;        // VK_KHR_acceleration_structure
    bool rayTracingPipeline = false;           // VK_KHR_ray_tracing_pipeline
    bool rayQuery = false;                     // VK_KHR_ray_query (optional)

    // Properties from VkPhysicalDeviceRayTracingPipelinePropertiesKHR
    uint32_t shaderGroupHandleSize = 0;
    uint32_t maxRayRecursionDepth = 0;
    uint32_t shaderGroupBaseAlignment = 0;
    uint32_t shaderGroupHandleAlignment = 0;

    // Properties from VkPhysicalDeviceAccelerationStructurePropertiesKHR
    uint64_t maxGeometryCount = 0;
    uint64_t maxInstanceCount = 0;
    uint64_t maxPrimitiveCount = 0;
};

class VulkanDevice {
public:
    VulkanDevice(VkPhysicalDevice* gpu);
    ~VulkanDevice();

    // device related member variables
    VkDevice device; // logical device
    VkPhysicalDevice* gpu; // physical device
    VkPhysicalDeviceProperties gpuProperties; // physical device properties
    VkPhysicalDeviceMemoryProperties gpuMemoryProperties; // physical device mem properties

    VkQueue queue;
    std::vector<VkQueueFamilyProperties> queueFamilyProperties;
    uint32_t graphicsQueueIndex;
    uint32_t graphicsQueueWithPresentIndex;
    uint32_t queueFamilyCount;
    VkPhysicalDeviceFeatures deviceFeatures; // physical device features

    VulkanLayerAndExtension layerExtension;

    //this class exposes the below functions to the outer world
    VulkanStatus CreateDevice(std::vector<const char *> &layers, std::vector<const char *> &extensions);
    void DestroyDevice();

    VulkanResult<uint32_t> MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirementsMask);
    void GetPhysicalDeviceQueuesAndProperties();
    VulkanResult<uint32_t> GetGraphicsQueueHandle();
    void GetDeviceQueue();

    // Present queue support
    bool HasPresentSupport() const;
    PFN_vkQueuePresentKHR GetPresentFunction() const;

    // ===== RTX Support (Phase K) =====

    /**
     * @brief Check if hardware ray tracing is supported
     * @return RTXCapabilities struct with support flags and properties
     *
     * Queries support for:
     * - VK_KHR_acceleration_structure
     * - VK_KHR_ray_tracing_pipeline
     * - VK_KHR_deferred_host_operations
     * - VK_KHR_buffer_device_address
     */
    RTXCapabilities CheckRTXSupport() const;

    /**
     * @brief Get required device extensions for RTX
     * @return Vector of extension names to enable
     */
    static std::vector<const char*> GetRTXExtensions();

    /**
     * @brief Check if RTX was enabled during device creation
     */
    bool IsRTXEnabled() const { return rtxEnabled_; }

    /**
     * @brief Get cached RTX capabilities (valid after CreateDevice)
     */
    const RTXCapabilities& GetRTXCapabilities() const { return rtxCapabilities_; }

    // ===== Capability Graph (Unified GPU Capability System) =====

    /**
     * @brief Get the capability graph for this device
     * @return Reference to the device's capability graph
     *
     * Use this to query any GPU capability:
     * - device.GetCapabilityGraph().IsCapabilityAvailable("RTXSupport")
     * - device.GetCapabilityGraph().IsCapabilityAvailable("SwapchainMaintenance3")
     */
    Vixen::CapabilityGraph& GetCapabilityGraph() { return capabilityGraph_; }
    const Vixen::CapabilityGraph& GetCapabilityGraph() const { return capabilityGraph_; }

    /**
     * @brief Convenient shorthand to check if a capability is available
     * @param capabilityName Name of the capability (e.g., "RTXSupport", "SwapchainMaintenance3")
     * @return true if capability exists and is available
     */
    bool HasCapability(const std::string& capabilityName) const {
        return capabilityGraph_.IsCapabilityAvailable(capabilityName);
    }

    struct DeviceFeatureMapping {
        const char* extensionName;
        VkStructureType structType;
        size_t structSize;
    };

    std::vector<std::unique_ptr<uint8_t[]>> deviceFeatureStorage; // to hold extension feature structures

    // ===== Upload Infrastructure (Sprint 5 Phase 2.5.3) =====

    /**
     * @brief Set the batched uploader for this device
     *
     * Called by DeviceNode during initialization. The uploader handles
     * all CPU→GPU data transfers with automatic batching and staging buffer management.
     *
     * @param uploader Unique ownership of the uploader
     */
    void SetUploader(std::unique_ptr<ResourceManagement::BatchedUploader> uploader);

    /**
     * @brief Set the budget manager for this device
     *
     * Called by DeviceNode during initialization. The budget manager tracks
     * GPU memory usage and enforces allocation quotas.
     *
     * @param manager Shared ownership of the budget manager
     */
    void SetBudgetManager(std::shared_ptr<ResourceManagement::DeviceBudgetManager> manager);

    /**
     * @brief Upload data to a GPU buffer
     *
     * Queues data for upload via staging buffer. Non-blocking - the upload
     * is batched with other pending uploads for efficiency.
     *
     * @param data Source data pointer (copied immediately to staging)
     * @param size Size in bytes
     * @param dstBuffer Destination GPU buffer
     * @param dstOffset Offset in destination buffer (default: 0)
     * @return Upload handle for tracking completion
     */
    [[nodiscard]] ResourceManagement::UploadHandle Upload(
        const void* data,
        VkDeviceSize size,
        VkBuffer dstBuffer,
        VkDeviceSize dstOffset = 0);

    /**
     * @brief Wait for all pending uploads to complete
     *
     * Flushes pending uploads and blocks until GPU finishes all transfers.
     */
    void WaitAllUploads();

    /**
     * @brief Get the budget manager for this device
     * @return Budget manager pointer, or nullptr if not configured
     */
    ResourceManagement::DeviceBudgetManager* GetBudgetManager() const;

    /**
     * @brief Check if upload infrastructure is ready
     * @return true if uploader and budget manager are configured
     */
    bool HasUploadSupport() const;

    // ===== Update Infrastructure (Sprint 5 Phase 3.5) =====

    /**
     * @brief Set the batched updater for this device
     *
     * Called by DeviceNode during initialization. The updater handles
     * per-frame GPU operations like TLAS rebuilds with automatic batching.
     *
     * @param updater Unique ownership of the updater
     */
    void SetUpdater(std::unique_ptr<ResourceManagement::BatchedUpdater> updater);

    /**
     * @brief Queue a GPU update request
     *
     * Queues an update (TLAS rebuild, buffer write, etc.) for later recording.
     * The request's imageIndex determines which frame queue it goes to.
     *
     * @param request Update request (ownership transferred)
     */
    void QueueUpdate(ResourceManagement::UpdateRequestPtr request);

    /**
     * @brief Record all pending updates for a frame
     *
     * Records update commands into the provided command buffer.
     * Call this during the Execute phase's command buffer recording.
     *
     * @param cmd Active command buffer in recording state
     * @param imageIndex Frame index to record
     * @return Number of updates recorded
     */
    uint32_t RecordUpdates(VkCommandBuffer cmd, uint32_t imageIndex);

    /**
     * @brief Check if update infrastructure is ready
     * @return true if updater is configured
     */
    bool HasUpdateSupport() const;

    // ===== Allocation Infrastructure (Sprint 5 Phase 3.5) =====

    /**
     * @brief Allocate a GPU buffer via centralized allocator
     *
     * All buffer allocations should go through this API for:
     * - Consistent budget tracking
     * - Unified memory management
     * - Debug naming
     *
     * @param request Allocation request with size, usage, location
     * @return BufferAllocation on success, or nullopt on failure
     */
    [[nodiscard]] std::optional<ResourceManagement::BufferAllocation> AllocateBuffer(
        const ResourceManagement::BufferAllocationRequest& request);

    /**
     * @brief Free a buffer allocated via AllocateBuffer()
     *
     * @param allocation The allocation to free
     */
    void FreeBuffer(ResourceManagement::BufferAllocation& allocation);

    /**
     * @brief Map a buffer for CPU access
     *
     * @param allocation Buffer to map
     * @return Mapped pointer, or nullptr on failure
     */
    [[nodiscard]] void* MapBuffer(ResourceManagement::BufferAllocation& allocation);

    /**
     * @brief Unmap a previously mapped buffer
     *
     * @param allocation Buffer to unmap
     */
    void UnmapBuffer(ResourceManagement::BufferAllocation& allocation);

    /**
     * @brief Get the memory allocator
     * @return Allocator pointer, or nullptr if not configured
     */
    ResourceManagement::IMemoryAllocator* GetAllocator() const;

private:
    // Helper to append a feature struct to the pNext chain
    inline void* AppendToPNext(void** chainEnd, void* featureStruct);

    inline bool HasExtension(const std::vector<const char*>& extensions, const char* name);

    // RTX state
    bool rtxEnabled_ = false;
    RTXCapabilities rtxCapabilities_;

    // Capability graph (initialized in CreateDevice)
    Vixen::CapabilityGraph capabilityGraph_;

    // Upload infrastructure (Sprint 5 Phase 2.5.3)
    std::unique_ptr<ResourceManagement::BatchedUploader> uploader_;
    std::shared_ptr<ResourceManagement::DeviceBudgetManager> budgetManager_;

    // Update infrastructure (Sprint 5 Phase 3.5)
    std::unique_ptr<ResourceManagement::BatchedUpdater> updater_;

};

} // namespace Vixen::Vulkan::Resources