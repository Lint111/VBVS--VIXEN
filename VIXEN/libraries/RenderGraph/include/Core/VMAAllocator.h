#pragma once

#include "Core/IMemoryAllocator.h"
#include "Core/ResourceBudgetManager.h"

// Forward declare VMA types to avoid including vk_mem_alloc.h in header
// VMA implementation is in the .cpp file
struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

#include <mutex>
#include <unordered_map>

namespace Vixen::RenderGraph {

/**
 * @brief VMA-backed memory allocator for production use
 *
 * Uses Vulkan Memory Allocator (VMA) for efficient GPU memory management.
 * Features:
 * - Suballocation from larger memory blocks
 * - Memory defragmentation support
 * - Optimal memory type selection
 * - Dedicated allocations for large resources
 * - Budget tracking integration with ResourceBudgetManager
 *
 * Thread-safe: Yes (VMA is thread-safe, plus internal tracking mutex)
 */
class VMAAllocator : public IMemoryAllocator {
public:
    /**
     * @brief Create VMA allocator
     *
     * @param instance Vulkan instance
     * @param physicalDevice Physical device for memory properties
     * @param device Logical device for allocations
     * @param budgetManager Optional budget manager for tracking
     */
    VMAAllocator(
        VkInstance instance,
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        ResourceBudgetManager* budgetManager = nullptr);

    ~VMAAllocator() override;

    // IMemoryAllocator interface
    [[nodiscard]] std::expected<BufferAllocation, AllocationError>
    AllocateBuffer(const BufferAllocationRequest& request) override;

    void FreeBuffer(BufferAllocation& allocation) override;

    [[nodiscard]] std::expected<ImageAllocation, AllocationError>
    AllocateImage(const ImageAllocationRequest& request) override;

    void FreeImage(ImageAllocation& allocation) override;

    [[nodiscard]] void* MapBuffer(const BufferAllocation& allocation) override;
    void UnmapBuffer(const BufferAllocation& allocation) override;

    void FlushMappedRange(
        const BufferAllocation& allocation,
        VkDeviceSize offset,
        VkDeviceSize size) override;

    void InvalidateMappedRange(
        const BufferAllocation& allocation,
        VkDeviceSize offset,
        VkDeviceSize size) override;

    [[nodiscard]] AllocationStats GetStats() const override;
    [[nodiscard]] std::string_view GetName() const override { return "VMAAllocator"; }

    void SetBudgetManager(ResourceBudgetManager* budgetManager) override;
    [[nodiscard]] ResourceBudgetManager* GetBudgetManager() const override;

    /**
     * @brief Get the underlying VMA allocator handle
     *
     * Use for advanced operations not exposed through IMemoryAllocator.
     * @return VMA allocator handle
     */
    [[nodiscard]] VmaAllocator GetVmaAllocator() const { return allocator_; }

    /**
     * @brief Check if allocator was successfully initialized
     * @return true if VMA allocator is valid
     */
    [[nodiscard]] bool IsValid() const { return allocator_ != nullptr; }

private:
    /**
     * @brief Internal record tracking VMA allocation metadata
     *
     * Stored alongside VmaAllocation to track size for budget reporting.
     */
    struct AllocationRecord {
        VmaAllocation vmaAllocation = nullptr;
        VkDeviceSize size = 0;
        bool isMapped = false;
    };

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;
    ResourceBudgetManager* budgetManager_ = nullptr;

    mutable std::mutex mutex_;
    std::unordered_map<void*, AllocationRecord> allocationRecords_;

    // Helper methods
    AllocationRecord* GetRecord(AllocationHandle handle);
    const AllocationRecord* GetRecord(AllocationHandle handle) const;
};

} // namespace Vixen::RenderGraph
