#pragma once

#include "Memory/IMemoryAllocator.h"
#include "Memory/RecordPool.h"
#include "Memory/ResourceBudgetManager.h"
#include <atomic>

namespace ResourceManagement {

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
    /// One live allocation; the record is the allocation handle. Fields are written by the
    /// allocating thread before `live` is published, then immutable except the map state, which
    /// Vulkan itself requires the caller to externally synchronise per VkDeviceMemory.
    struct AllocationRecord {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        uint32_t memoryTypeIndex = 0;
        bool isMapped = false;
        void* mappedPtr = nullptr;
        bool canAlias = false;     // Created with allowAliasing=true
        bool isAliased = false;    // This is an aliased resource (doesn't own memory)
        std::atomic<bool> live{false};  // published after the fields; cleared by the one freeing thread
        uint32_t poolIndex = 0;         // RecordPool bookkeeping
        uint32_t nextFree = 0;          // RecordPool bookkeeping
    };

    VkPhysicalDevice physicalDevice_;
    VkDevice device_;
    VkPhysicalDeviceMemoryProperties memProperties_;

    // Lock-free registry: the pool hands out stable records, `live` + Owns() validate handles
    // without dereferencing foreign pointers, and the stats are independent atomics.
    RecordPool<AllocationRecord> allocations_;
    std::atomic<uint64_t> totalAllocatedBytes_{0};
    std::atomic<uint64_t> totalUsedBytes_{0};
    std::atomic<uint32_t> allocationCount_{0};
    std::atomic<uint32_t> blockCount_{0};
    std::atomic<ResourceBudgetManager*> budgetManager_{nullptr};  // set at wiring time, read lock-free
    [[nodiscard]] ResourceBudgetManager* Budget() const noexcept { return budgetManager_.load(std::memory_order_acquire); }

    /// Take a pool slot and publish it as live; nullptr when the pool is at its cap.
    AllocationRecord* AcquireRecord(VkDeviceMemory memory, VkDeviceSize size,
                                    uint32_t memoryTypeIndex, bool canAlias, bool isAliased);
    /// Claim `handle` for freeing: true for exactly one caller per live handle, false for an
    /// invalid or already-freed handle (which is never dereferenced).
    bool ClaimForFree(AllocationHandle handle);

    // Helper methods
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkMemoryPropertyFlags GetMemoryProperties(MemoryLocation location) const;
    AllocationRecord* GetRecord(AllocationHandle handle);
    const AllocationRecord* GetRecord(AllocationHandle handle) const;
};

} // namespace ResourceManagement
