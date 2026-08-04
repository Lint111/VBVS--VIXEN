#pragma once

#include "Headers.h"
#include "VulkanLayerAndExtension.h"
#include "error/VulkanError.h"
#include "CapabilityGraph.h"
#include <memory>
#include <functional>
#include <mutex>
#include <unordered_map>

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

    // Per-DEVICE synchronization2 entry points, resolved in CreateDevice() via
    // vkGetDeviceProcAddr(device, "...KHR") — the KHR-suffixed names resolve on both genuine
    // 1.3-core drivers and 1.2+VK_KHR_synchronization2 drivers (Mesa Dozen). These are device-level
    // dispatch pointers and MUST live per-instance, not in a process global: two live devices (or a
    // device recreated during device-loss recovery) each need their own resolved pointers — a
    // global would silently dispatch one device's calls through another device's driver entry.
    PFN_vkCmdPipelineBarrier2KHR fpCmdPipelineBarrier2 = nullptr;
    PFN_vkQueueSubmit2KHR fpQueueSubmit2 = nullptr;

    // VIXEN_PIPELINE_STATS: resolved only when VK_KHR_pipeline_executable_properties was enabled
    // (see IsPipelineStatsEnabled), same per-device resolution rationale as the pair above.
    PFN_vkGetPipelineExecutablePropertiesKHR fpGetPipelineExecutableProperties = nullptr;
    PFN_vkGetPipelineExecutableStatisticsKHR fpGetPipelineExecutableStatistics = nullptr;

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

    /**
     * @brief Get the mutex that must be held around any vkQueueSubmit/vkQueueSubmit2/
     * vkQueuePresentKHR/vkQueueWaitIdle call on the given VkQueue (audit V-M11).
     *
     * The Vulkan spec requires external synchronization on VkQueue for these calls. The
     * RenderGraph TBB parallel executor can schedule two independent submit-recording nodes in
     * the same wave (the access tracker only models data-flow resources, not the queue itself),
     * so concurrent vkQueueSubmit on one VkQueue is reachable and is UB per spec. One mutex per
     * queue handle, created on first use and stable for the device's lifetime (never erased, so
     * the returned reference stays valid).
     *
     * Do NOT hold this mutex across a fence wait (vkWaitForFences) — only across the
     * submit/present call itself, or independent submit chains serialize on the fence wait too.
     *
     * @param queue The VkQueue that will be submitted to or presented on.
     */
    std::mutex& SubmitMutex(VkQueue queue);

    // KI-012: some queue families (e.g. Mesa Dozen's transfer-capable graphics queue) report
    // minImageTransferGranularity = (0,0,0), which per spec means vkCmdCopyImageToBuffer/
    // vkCmdCopyBufferToImage on that queue only accept whole-image copies at offset (0,0,0) -- any
    // sub-region copy (e.g. a single-texel readback) is a spec violation there even though some
    // drivers tolerate it anyway. Checked once from the already-queried queueFamilyProperties
    // (GetPhysicalDeviceQueuesAndProperties), not re-queried per call site.
    bool RequiresFullImageTransfers() const;

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
     * @brief Check if VK_KHR_pipeline_executable_properties was enabled (VIXEN_PIPELINE_STATS=1
     * and the device supports it -- see DeviceNode::CreateLogicalDevice)
     */
    bool IsPipelineStatsEnabled() const { return pipelineStatsEnabled_; }

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
     * @brief Submit pending uploads without blocking (Sparse-Mip ESVO LOD Inc1 M4c)
     *
     * Kicks off GPU execution of whatever is currently queued via Upload() so its
     * completion can be polled later via IsUploadComplete(), instead of blocking the
     * caller until the GPU finishes (WaitAllUploads()'s behavior). Non-blocking.
     */
    void FlushUploads();

    /**
     * @brief Check whether a queued upload has finished on the GPU (non-blocking)
     *
     * Polls (does not wait); call once per frame from a tick that can tolerate a
     * multi-frame latency between FlushUploads() and completion becoming visible.
     *
     * @param handle Upload handle from Upload()
     * @return true once the GPU-side copy is visible (or the upload failed)
     */
    [[nodiscard]] bool IsUploadComplete(ResourceManagement::UploadHandle handle) const;

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

    // ===== GPU Query Infrastructure (Sprint 6.3 Phase 0) =====

    /**
     * @brief Initialize the GPU query manager for this device
     *
     * Called by DeviceNode during initialization. The query manager coordinates
     * GPU timestamp queries across multiple consumers (ProfilerSystem, nodes, etc.)
     * to prevent query slot conflicts.
     *
     * @param framesInFlight Number of frames in flight (typically 2-4)
     * @param maxConsumers Maximum number of query consumers (default 16)
     */
    void InitializeQueryManager(uint32_t framesInFlight, uint32_t maxConsumers = 16);

    /**
     * @brief Get the GPU query manager for this device
     * @return Query manager pointer, or nullptr if not initialized
     *
     * Returns void* to avoid circular dependency (VulkanResources <-> RenderGraph).
     * Cast to RenderGraph::GPUQueryManager* in RenderGraph code.
     */
    [[nodiscard]] void* GetQueryManager() const;

    /**
     * @brief Check if GPU query infrastructure is ready
     * @return true if query manager is initialized and timestamp queries are supported
     */
    [[nodiscard]] bool HasQuerySupport() const;

    /**
     * @brief Internal method to set query manager (called by DeviceNode)
     *
     * Uses template to avoid circular dependency. DeviceNode passes GPUQueryManager
     * which is stored as void* internally.
     *
     * @param manager Shared pointer to query manager
     */
    template<typename T>
    void SetQueryManagerInternal(std::shared_ptr<T> manager) {
        queryManager_ = std::static_pointer_cast<void>(manager);
        // Type-erased GPU-resource release so DestroyDevice() can free the query pools BEFORE
        // vkDestroyDevice without VulkanResources depending on RenderGraph::GPUQueryManager.
        // The manager is shared with render-graph nodes, so a plain reset would not guarantee
        // the pools are destroyed while the device is still alive; this releases them explicitly.
        queryManagerRelease_ = [manager]() { if (manager) manager->ReleaseGPUResources(); };
    }

private:
    // Helper to append a feature struct to the pNext chain
    inline void* AppendToPNext(void** chainEnd, void* featureStruct);

    inline bool HasExtension(const std::vector<const char*>& extensions, const char* name);

    // Query the physical device for the non-concrete features tracked by the capability graph,
    // returning the names it reports as supported (fed into DeviceFeatureCapability).
    std::vector<std::string> QueryAvailableDeviceFeatures() const;

    // RTX state
    bool rtxEnabled_ = false;
    RTXCapabilities rtxCapabilities_;

    // VIXEN_PIPELINE_STATS state (see IsPipelineStatsEnabled)
    bool pipelineStatsEnabled_ = false;

    // Capability graph (initialized in CreateDevice)
    Vixen::CapabilityGraph capabilityGraph_;

    // Upload infrastructure (Sprint 5 Phase 2.5.3)
    std::unique_ptr<ResourceManagement::BatchedUploader> uploader_;
    std::shared_ptr<ResourceManagement::DeviceBudgetManager> budgetManager_;

    // Update infrastructure (Sprint 5 Phase 3.5)
    std::unique_ptr<ResourceManagement::BatchedUpdater> updater_;

    // GPU query infrastructure (Sprint 6.3 Phase 0)
    // Stored as void* to avoid circular dependency (VulkanResources <-> RenderGraph)
    // Actual type: std::shared_ptr<RenderGraph::GPUQueryManager>
    std::shared_ptr<void> queryManager_;

    // Type-erased "release GPU resources" hook for queryManager_, invoked before vkDestroyDevice
    // so the query pools are destroyed while the device is still valid (the manager is shared
    // with render-graph nodes, whose destruction order relative to the device is not guaranteed).
    std::function<void()> queryManagerRelease_;

    // Per-queue submit mutexes (audit V-M11). unique_ptr so a rehash never invalidates a mutex
    // reference already handed out by SubmitMutex(); guarded by submitMutexMapLock_ (only
    // during first-use creation of an entry, not around the submit itself).
    std::mutex submitMutexMapLock_;
    std::unordered_map<VkQueue, std::unique_ptr<std::mutex>> submitMutexes_;

};

} // namespace Vixen::Vulkan::Resources