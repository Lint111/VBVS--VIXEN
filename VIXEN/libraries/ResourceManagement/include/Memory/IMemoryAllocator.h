#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vulkan/vulkan.h>

namespace ResourceManagement {

// Forward declarations
class ResourceBudgetManager;

/**
 * @brief Memory allocation error codes
 */
enum class AllocationError : uint8_t {
    Success = 0,
    OutOfDeviceMemory,
    OutOfHostMemory,
    OverBudget,
    InvalidParameters,
    MappingFailed,
    Unknown
};

/**
 * @brief Convert allocation error to string for debugging
 */
constexpr std::string_view AllocationErrorToString(AllocationError error) {
    switch (error) {
        case AllocationError::Success: return "Success";
        case AllocationError::OutOfDeviceMemory: return "Out of device memory";
        case AllocationError::OutOfHostMemory: return "Out of host memory";
        case AllocationError::OverBudget: return "Over budget";
        case AllocationError::InvalidParameters: return "Invalid parameters";
        case AllocationError::MappingFailed: return "Mapping failed";
        case AllocationError::Unknown: return "Unknown error";
    }
    return "Unknown error";
}

/**
 * @brief Memory location hint for allocation
 */
enum class MemoryLocation : uint8_t {
    DeviceLocal,      // GPU-only, fastest for GPU access
    HostVisible,      // CPU-readable/writable, slower GPU access
    HostCached,       // CPU-cached, good for readback
    Auto              // Let allocator decide based on usage
};

/**
 * @brief Buffer allocation request descriptor
 */
struct BufferAllocationRequest {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    MemoryLocation location = MemoryLocation::DeviceLocal;
    std::string_view debugName;
    bool dedicated = false;   // Request dedicated allocation (large buffers)
    bool allowAliasing = false;  // Allow this allocation to be aliased with other resources
};

/**
 * @brief Image allocation request descriptor
 */
struct ImageAllocationRequest {
    VkImageCreateInfo createInfo{};
    MemoryLocation location = MemoryLocation::DeviceLocal;
    std::string_view debugName;
    bool dedicated = false;
    bool allowAliasing = false;  // Allow this allocation to be aliased with other resources
};

/**
 * @brief Opaque handle to an allocation
 *
 * The allocator implementation defines what this points to.
 * For VMA: VmaAllocation
 * For direct Vulkan: custom AllocationRecord
 */
using AllocationHandle = void*;

/**
 * @brief Request to create a buffer aliased with an existing allocation
 *
 * Used for memory aliasing where multiple non-overlapping-lifetime resources
 * share the same memory backing. The source allocation must have been created
 * with allowAliasing = true.
 */
struct AliasedBufferRequest {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    AllocationHandle sourceAllocation = nullptr;  // Existing allocation to alias
    VkDeviceSize offsetInAllocation = 0;          // Offset within source allocation
    std::string_view debugName;
};

/**
 * @brief Request to create an image aliased with an existing allocation
 */
struct AliasedImageRequest {
    VkImageCreateInfo createInfo{};
    AllocationHandle sourceAllocation = nullptr;  // Existing allocation to alias
    VkDeviceSize offsetInAllocation = 0;          // Offset within source allocation
    std::string_view debugName;
};

/**
 * @brief Result of a buffer allocation
 */
struct BufferAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    AllocationHandle allocation = nullptr;
    VkDeviceSize size = 0;
    VkDeviceSize offset = 0;       // Offset within larger allocation (suballocation)
    void* mappedData = nullptr;    // Non-null if persistently mapped
    VkDeviceAddress deviceAddress = 0;  // Non-zero if VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT enabled
    bool canAlias = false;         // True if this allocation supports aliasing
    bool isAliased = false;        // True if this is an aliased resource (doesn't own memory)

    explicit operator bool() const { return buffer != VK_NULL_HANDLE; }
};

/**
 * @brief Result of an image allocation
 */
struct ImageAllocation {
    VkImage image = VK_NULL_HANDLE;
    AllocationHandle allocation = nullptr;
    VkDeviceSize size = 0;
    bool canAlias = false;         // True if this allocation supports aliasing
    bool isAliased = false;        // True if this is an aliased resource (doesn't own memory)

    explicit operator bool() const { return image != VK_NULL_HANDLE; }
};

/**
 * @brief Allocation statistics for monitoring
 */
struct AllocationStats {
    uint64_t totalAllocatedBytes = 0;
    uint64_t totalUsedBytes = 0;       // After fragmentation
    uint32_t allocationCount = 0;
    uint32_t blockCount = 0;           // Memory blocks from Vulkan
    float fragmentationRatio = 0.0f;   // 0.0 = no fragmentation
};

/**
 * @brief Phase A.2: Memory Allocator Interface
 *
 * Abstracts GPU memory allocation to support multiple backends:
 * - VMAAllocator: Production allocator using Vulkan Memory Allocator
 * - DirectAllocator: Simple wrapper around vkAllocateMemory (testing/fallback)
 * - MockAllocator: For unit testing without Vulkan
 *
 * All implementations integrate with ResourceBudgetManager for tracking.
 *
 * Usage:
 * @code
 * auto result = allocator->AllocateBuffer({
 *     .size = 1024 * 1024,
 *     .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
 *     .location = MemoryLocation::DeviceLocal,
 *     .debugName = "VertexBuffer"
 * });
 *
 * if (result) {
 *     // Use result->buffer
 * } else {
 *     // Handle result.error()
 * }
 * @endcode
 */
class IMemoryAllocator {
public:
    virtual ~IMemoryAllocator() = default;

    // Non-copyable, non-movable (implementations may hold Vulkan state)
    IMemoryAllocator(const IMemoryAllocator&) = delete;
    IMemoryAllocator& operator=(const IMemoryAllocator&) = delete;
    IMemoryAllocator(IMemoryAllocator&&) = delete;
    IMemoryAllocator& operator=(IMemoryAllocator&&) = delete;

    // =========================================================================
    // Buffer Operations
    // =========================================================================

    /**
     * @brief Allocate a GPU buffer
     *
     * @param request Buffer allocation parameters
     * @return Buffer allocation or error
     */
    [[nodiscard]] virtual std::expected<BufferAllocation, AllocationError>
    AllocateBuffer(const BufferAllocationRequest& request) = 0;

    /**
     * @brief Free a previously allocated buffer
     *
     * @param allocation The allocation to free (invalidated after call)
     */
    virtual void FreeBuffer(BufferAllocation& allocation) = 0;

    // =========================================================================
    // Image Operations
    // =========================================================================

    /**
     * @brief Allocate a GPU image
     *
     * @param request Image allocation parameters
     * @return Image allocation or error
     */
    [[nodiscard]] virtual std::expected<ImageAllocation, AllocationError>
    AllocateImage(const ImageAllocationRequest& request) = 0;

    /**
     * @brief Free a previously allocated image
     *
     * @param allocation The allocation to free (invalidated after call)
     */
    virtual void FreeImage(ImageAllocation& allocation) = 0;

    // =========================================================================
    // Aliased Allocations (Sprint 4 Phase B+)
    // =========================================================================

    /**
     * @brief Create a buffer that aliases memory from an existing allocation
     *
     * Memory aliasing allows multiple resources with non-overlapping lifetimes
     * to share the same memory backing, reducing memory usage.
     *
     * IMPORTANT: The source allocation must have been created with allowAliasing=true.
     * The caller is responsible for ensuring non-overlapping resource lifetimes
     * and proper synchronization (memory barriers) between aliased resources.
     *
     * @param request Aliased buffer parameters including source allocation
     * @return Aliased buffer allocation or error
     */
    [[nodiscard]] virtual std::expected<BufferAllocation, AllocationError>
    CreateAliasedBuffer(const AliasedBufferRequest& request) = 0;

    /**
     * @brief Create an image that aliases memory from an existing allocation
     *
     * @param request Aliased image parameters including source allocation
     * @return Aliased image allocation or error
     */
    [[nodiscard]] virtual std::expected<ImageAllocation, AllocationError>
    CreateAliasedImage(const AliasedImageRequest& request) = 0;

    /**
     * @brief Check if an allocation supports aliasing
     *
     * @param allocation Allocation handle to check
     * @return true if the allocation was created with allowAliasing=true
     */
    [[nodiscard]] virtual bool SupportsAliasing(AllocationHandle allocation) const = 0;

    // =========================================================================
    // Memory Mapping
    // =========================================================================

    /**
     * @brief Map buffer memory for CPU access
     *
     * @param allocation Buffer to map
     * @return Mapped pointer or nullptr on failure
     */
    [[nodiscard]] virtual void* MapBuffer(const BufferAllocation& allocation) = 0;

    /**
     * @brief Unmap previously mapped buffer memory
     *
     * @param allocation Buffer to unmap
     */
    virtual void UnmapBuffer(const BufferAllocation& allocation) = 0;

    /**
     * @brief Flush mapped memory range to make CPU writes visible to GPU
     *
     * @param allocation Buffer allocation
     * @param offset Offset within buffer
     * @param size Size to flush (VK_WHOLE_SIZE for entire buffer)
     */
    virtual void FlushMappedRange(
        const BufferAllocation& allocation,
        VkDeviceSize offset = 0,
        VkDeviceSize size = VK_WHOLE_SIZE) = 0;

    /**
     * @brief Invalidate mapped memory range to make GPU writes visible to CPU
     *
     * @param allocation Buffer allocation
     * @param offset Offset within buffer
     * @param size Size to invalidate (VK_WHOLE_SIZE for entire buffer)
     */
    virtual void InvalidateMappedRange(
        const BufferAllocation& allocation,
        VkDeviceSize offset = 0,
        VkDeviceSize size = VK_WHOLE_SIZE) = 0;

    // =========================================================================
    // Statistics & Debugging
    // =========================================================================

    /**
     * @brief Get allocation statistics
     *
     * @return Current allocation statistics
     */
    [[nodiscard]] virtual AllocationStats GetStats() const = 0;

    /**
     * @brief Get allocator name for debugging
     *
     * @return Allocator implementation name (e.g., "VMA", "Direct")
     */
    [[nodiscard]] virtual std::string_view GetName() const = 0;

    /**
     * @brief Set budget manager for allocation tracking
     *
     * @param budgetManager Budget manager (can be nullptr to disable tracking)
     */
    virtual void SetBudgetManager(ResourceBudgetManager* budgetManager) = 0;

    /**
     * @brief Get currently configured budget manager
     *
     * @return Budget manager or nullptr
     */
    [[nodiscard]] virtual ResourceBudgetManager* GetBudgetManager() const = 0;

protected:
    IMemoryAllocator() = default;
};

/**
 * @brief Factory for creating memory allocators
 */
struct MemoryAllocatorFactory {
    /**
     * @brief Create a VMA-backed allocator (requires VMA library)
     *
     * @param instance Vulkan instance
     * @param physicalDevice Physical device
     * @param device Logical device
     * @param budgetManager Optional budget manager for tracking
     * @return Allocator instance or nullptr on failure
     */
    static std::unique_ptr<IMemoryAllocator> CreateVMAAllocator(
        VkInstance instance,
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        ResourceBudgetManager* budgetManager = nullptr);

    /**
     * @brief Create a direct Vulkan allocator (no VMA, simpler but less efficient)
     *
     * @param physicalDevice Physical device
     * @param device Logical device
     * @param budgetManager Optional budget manager for tracking
     * @return Allocator instance or nullptr on failure
     */
    static std::unique_ptr<IMemoryAllocator> CreateDirectAllocator(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        ResourceBudgetManager* budgetManager = nullptr);
};

} // namespace ResourceManagement
