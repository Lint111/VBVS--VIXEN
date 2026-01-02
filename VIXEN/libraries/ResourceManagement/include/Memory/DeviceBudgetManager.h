#pragma once

#include "Memory/IMemoryAllocator.h"
#include "Memory/ResourceBudgetManager.h"
#include <memory>

namespace ResourceManagement {

/**
 * @brief GPU memory heap type for tracking
 */
enum class DeviceHeapType : uint8_t {
    DeviceLocal,    // GPU-only memory (fastest)
    HostVisible,    // CPU-accessible GPU memory
    HostCached,     // CPU-cached GPU memory (readback)
    Staging         // Staging buffer quota
};

/**
 * @brief Device memory statistics
 */
struct DeviceMemoryStats {
    uint64_t totalDeviceMemory = 0;     // Total GPU VRAM
    uint64_t usedDeviceMemory = 0;      // Currently used VRAM
    uint64_t availableDeviceMemory = 0; // Available VRAM
    uint64_t stagingQuotaUsed = 0;      // Staging buffer usage
    uint64_t stagingQuotaMax = 0;       // Staging buffer limit
    float fragmentationRatio = 0.0f;    // Memory fragmentation
};

/**
 * @brief Phase A.4: Device Budget Manager with IMemoryAllocator Integration
 *
 * Specialized budget manager for GPU/device memory that integrates with
 * IMemoryAllocator (VMA or DirectAllocator).
 *
 * Features:
 * - GPU VRAM budget tracking per heap type
 * - IMemoryAllocator facade for allocation
 * - Staging buffer quota management
 * - Memory statistics from allocator
 * - Budget warnings and enforcement
 *
 * Thread-safe: Yes (delegates to thread-safe allocator and budget manager)
 */
class DeviceBudgetManager {
public:
    /**
     * @brief Configuration for device budget manager
     */
    struct Config {
        uint64_t deviceMemoryBudget = 0;  // 0 = auto-detect from physical device
        uint64_t deviceMemoryWarning = 0; // Warning threshold
        uint64_t stagingQuota = 256 * 1024 * 1024; // 256 MB staging buffer quota
        bool strictBudget = false;        // Fail allocations over budget
    };

    /**
     * @brief Create device budget manager
     *
     * @param allocator Memory allocator to use (VMA or Direct)
     * @param physicalDevice For auto-detecting VRAM size
     * @param config Budget configuration
     */
    DeviceBudgetManager(
        std::shared_ptr<IMemoryAllocator> allocator,
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE,
        const Config& config = Config{});

    ~DeviceBudgetManager() = default;

    // Non-copyable (owns allocator reference)
    DeviceBudgetManager(const DeviceBudgetManager&) = delete;
    DeviceBudgetManager& operator=(const DeviceBudgetManager&) = delete;

    // =========================================================================
    // Buffer Allocation (Delegates to IMemoryAllocator)
    // =========================================================================

    /**
     * @brief Allocate a GPU buffer with budget tracking
     *
     * @param request Buffer allocation parameters
     * @return Buffer allocation or error
     */
    [[nodiscard]] std::expected<BufferAllocation, AllocationError>
    AllocateBuffer(const BufferAllocationRequest& request);

    /**
     * @brief Free a buffer allocation
     */
    void FreeBuffer(BufferAllocation& allocation);

    // =========================================================================
    // Image Allocation (Delegates to IMemoryAllocator)
    // =========================================================================

    /**
     * @brief Allocate a GPU image with budget tracking
     *
     * @param request Image allocation parameters
     * @return Image allocation or error
     */
    [[nodiscard]] std::expected<ImageAllocation, AllocationError>
    AllocateImage(const ImageAllocationRequest& request);

    /**
     * @brief Free an image allocation
     */
    void FreeImage(ImageAllocation& allocation);

    // =========================================================================
    // Aliased Allocations (Sprint 4 Phase B+)
    // =========================================================================

    /**
     * @brief Create a buffer aliased with an existing allocation
     *
     * Aliased resources share memory with the source allocation and do NOT
     * consume additional budget. The caller is responsible for ensuring
     * non-overlapping lifetimes and proper synchronization.
     *
     * @param request Aliased buffer parameters
     * @return Aliased buffer allocation or error
     */
    [[nodiscard]] std::expected<BufferAllocation, AllocationError>
    CreateAliasedBuffer(const AliasedBufferRequest& request);

    /**
     * @brief Create an image aliased with an existing allocation
     *
     * @param request Aliased image parameters
     * @return Aliased image allocation or error
     */
    [[nodiscard]] std::expected<ImageAllocation, AllocationError>
    CreateAliasedImage(const AliasedImageRequest& request);

    /**
     * @brief Free an aliased buffer
     *
     * Destroys the buffer but does NOT free the underlying memory
     * (which belongs to the source allocation).
     */
    void FreeAliasedBuffer(BufferAllocation& allocation);

    /**
     * @brief Free an aliased image
     */
    void FreeAliasedImage(ImageAllocation& allocation);

    /**
     * @brief Check if an allocation supports aliasing
     */
    [[nodiscard]] bool SupportsAliasing(AllocationHandle allocation) const;

    /**
     * @brief Get count of active aliased allocations
     */
    [[nodiscard]] uint32_t GetAliasedAllocationCount() const;

    // =========================================================================
    // Staging Buffer Management
    // =========================================================================

    /**
     * @brief Request staging buffer quota for upload
     *
     * @param bytes Bytes needed for staging
     * @return true if quota available
     */
    [[nodiscard]] bool TryReserveStagingQuota(uint64_t bytes);

    /**
     * @brief Release staging quota after upload complete
     */
    void ReleaseStagingQuota(uint64_t bytes);

    /**
     * @brief Get current staging quota usage
     */
    [[nodiscard]] uint64_t GetStagingQuotaUsed() const;

    /**
     * @brief Get available staging quota
     */
    [[nodiscard]] uint64_t GetAvailableStagingQuota() const;

    // =========================================================================
    // Statistics & Monitoring
    // =========================================================================

    /**
     * @brief Get comprehensive device memory statistics
     */
    [[nodiscard]] DeviceMemoryStats GetStats() const;

    /**
     * @brief Get usage for specific heap type
     */
    [[nodiscard]] BudgetResourceUsage GetHeapUsage(DeviceHeapType heapType) const;

    /**
     * @brief Get allocator statistics
     */
    [[nodiscard]] AllocationStats GetAllocatorStats() const;

    /**
     * @brief Check if device memory is near budget limit
     */
    [[nodiscard]] bool IsNearBudgetLimit() const;

    /**
     * @brief Check if device memory is over budget
     */
    [[nodiscard]] bool IsOverBudget() const;

    // =========================================================================
    // Allocator Access
    // =========================================================================

    /**
     * @brief Get underlying memory allocator
     *
     * Use for advanced operations not exposed through DeviceBudgetManager.
     */
    [[nodiscard]] IMemoryAllocator* GetAllocator() const { return allocator_.get(); }

    /**
     * @brief Get allocator name
     */
    [[nodiscard]] std::string_view GetAllocatorName() const {
        return allocator_ ? allocator_->GetName() : "None";
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const Config& GetConfig() const { return config_; }

    /**
     * @brief Update staging quota
     */
    void SetStagingQuota(uint64_t quota);

private:
    Config config_;
    std::shared_ptr<IMemoryAllocator> allocator_;
    ResourceBudgetManager budgetTracker_;

    // Staging quota tracking
    std::atomic<uint64_t> stagingQuotaUsed_{0};

    // Aliased allocation tracking (Sprint 4 Phase B+)
    std::atomic<uint32_t> aliasedAllocationCount_{0};

    // Internal helpers
    static BudgetResourceType HeapTypeToBudgetType(DeviceHeapType heapType);
    DeviceHeapType MemoryLocationToHeapType(MemoryLocation location) const;
};

} // namespace ResourceManagement
