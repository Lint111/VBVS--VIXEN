#pragma once

#include "Memory/IMemoryAllocator.h"
#include "Memory/ResourceBudgetManager.h"

// Forward declare VMA types to avoid including vk_mem_alloc.h in header
// VMA implementation is in the .cpp file
struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

#include <atomic>

namespace ResourceManagement {

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
 * Thread-safe: Yes. VMA synchronises its own state; per-allocation facts (aliasing) ride in the
 * VmaAllocation user data, so this wrapper keeps no registry and takes no lock.
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

    // Aliased allocations (Sprint 4 Phase B+)
    [[nodiscard]] std::expected<BufferAllocation, AllocationError>
    CreateAliasedBuffer(const AliasedBufferRequest& request) override;

    [[nodiscard]] std::expected<ImageAllocation, AllocationError>
    CreateAliasedImage(const AliasedImageRequest& request) override;

    [[nodiscard]] bool SupportsAliasing(AllocationHandle allocation) const override;

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
    /// Bits stored in each VmaAllocation's pUserData (set at creation, read back with
    /// vmaGetAllocationInfo): the only per-allocation facts this wrapper needs.
    static constexpr uintptr_t kUserDataCanAlias = 1;


    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;
    std::atomic<ResourceBudgetManager*> budgetManager_{nullptr};  // set at wiring time, read lock-free

    [[nodiscard]] ResourceBudgetManager* Budget() const noexcept { return budgetManager_.load(std::memory_order_acquire); }
    /// Allocation size and alias capability, read from VMA itself (false/0 for a null handle).
    bool QueryAllocation(AllocationHandle handle, VkDeviceSize& size, bool& canAlias) const;
};

} // namespace ResourceManagement
