#pragma once

#include "Core/IMemoryAllocator.h"
#include "Core/ResourceBudgetManager.h"
#include <mutex>
#include <unordered_map>

namespace Vixen::RenderGraph {

/**
 * @brief Direct Vulkan memory allocator (no VMA)
 *
 * Simple allocator that wraps vkAllocateMemory directly.
 * Use for testing or as fallback when VMA is unavailable.
 *
 * Limitations:
 * - No suballocation (one vkAllocateMemory per buffer/image)
 * - No memory defragmentation
 * - Higher memory overhead for small allocations
 *
 * Thread-safe: Yes (internal mutex protects allocation tracking)
 */
class DirectAllocator : public IMemoryAllocator {
public:
    DirectAllocator(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        ResourceBudgetManager* budgetManager = nullptr);

    ~DirectAllocator() override;

    // IMemoryAllocator interface
    [[nodiscard]] std::expected<BufferAllocation, AllocationError>
    AllocateBuffer(const BufferAllocationRequest& request) override;

    void FreeBuffer(BufferAllocation& allocation) override;

    [[nodiscard]] std::expected<ImageAllocation, AllocationError>
    AllocateImage(const ImageAllocationRequest& request) override;

    void FreeImage(ImageAllocation& allocation) override;

    // Aliased allocations (Sprint 4 Phase B+)
    // Note: DirectAllocator has limited aliasing support (basic implementation)
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
    [[nodiscard]] std::string_view GetName() const override { return "DirectAllocator"; }

    void SetBudgetManager(ResourceBudgetManager* budgetManager) override;
    [[nodiscard]] ResourceBudgetManager* GetBudgetManager() const override;

private:
    /**
     * @brief Internal allocation record
     *
     * Stored as AllocationHandle (void*) in BufferAllocation/ImageAllocation
     */
    struct AllocationRecord {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        uint32_t memoryTypeIndex = 0;
        bool isMapped = false;
        void* mappedPtr = nullptr;
        bool canAlias = false;     // Created with allowAliasing=true
        bool isAliased = false;    // This is an aliased resource (doesn't own memory)
    };

    VkPhysicalDevice physicalDevice_;
    VkDevice device_;
    VkPhysicalDeviceMemoryProperties memProperties_;
    ResourceBudgetManager* budgetManager_ = nullptr;

    mutable std::mutex mutex_;
    std::unordered_map<AllocationHandle, AllocationRecord> allocations_;
    AllocationStats stats_;

    // Helper methods
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkMemoryPropertyFlags GetMemoryProperties(MemoryLocation location) const;
    AllocationRecord* GetRecord(AllocationHandle handle);
    const AllocationRecord* GetRecord(AllocationHandle handle) const;
};

} // namespace Vixen::RenderGraph
