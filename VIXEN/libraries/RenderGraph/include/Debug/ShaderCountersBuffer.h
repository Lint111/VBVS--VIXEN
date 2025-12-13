#pragma once

#define NOMINMAX

#include "IDebugBuffer.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <span>
#include <any>

namespace Vixen::RenderGraph::Debug {

/**
 * @brief Shader performance counter data collected from GPU
 *
 * This struct matches the GLSL layout for shader counters.
 * Must be std430-compatible (4-byte aligned members).
 *
 * GLSL equivalent:
 * @code
 * struct ShaderCounters {
 *     uint iterations;    // Ray march iterations
 *     uint boundsChecks;  // Bounds check operations
 *     uint maxDepth;      // Maximum tree depth reached
 *     uint nodeAccesses;  // Node memory accesses
 * };
 * @endcode
 */
struct ShaderCounters {
    uint32_t iterations = 0;      ///< Ray march iterations
    uint32_t boundsChecks = 0;    ///< Bounds check operations
    uint32_t maxDepth = 0;        ///< Maximum tree depth reached
    uint32_t nodeAccesses = 0;    ///< Node memory accesses

    /**
     * @brief Reset all counters to zero
     */
    void Clear() {
        iterations = 0;
        boundsChecks = 0;
        maxDepth = 0;
        nodeAccesses = 0;
    }

    /**
     * @brief Accumulate counters from another instance
     */
    void Accumulate(const ShaderCounters& other) {
        iterations += other.iterations;
        boundsChecks += other.boundsChecks;
        maxDepth = std::max(maxDepth, other.maxDepth);
        nodeAccesses += other.nodeAccesses;
    }
};

// Ensure struct is std430 compatible (no padding)
static_assert(sizeof(ShaderCounters) == 16, "ShaderCounters must be 16 bytes for GPU alignment");
static_assert(alignof(ShaderCounters) == 4, "ShaderCounters must be 4-byte aligned");

/**
 * @brief GPU buffer header for shader counters
 *
 * Placed at the start of the buffer before counter entries.
 */
struct ShaderCountersHeader {
    uint32_t entryCount = 0;      ///< Number of valid entries
    uint32_t capacity = 0;        ///< Maximum entries buffer can hold
    uint32_t _padding[2] = {0};   ///< Align to 16 bytes
};

static_assert(sizeof(ShaderCountersHeader) == 16, "ShaderCountersHeader must be 16 bytes");

/**
 * @brief GPU buffer for collecting shader performance counters
 *
 * This class manages a HOST_VISIBLE | HOST_COHERENT Vulkan buffer for
 * accumulating shader performance metrics (iterations, bounds checks,
 * tree depth, node accesses) that are written by compute shaders.
 *
 * Implements IDebugBuffer interface for polymorphic usage with
 * DebugBufferReaderNode.
 *
 * Buffer layout:
 * - [0..15]: ShaderCountersHeader (16 bytes)
 * - [16..]: ShaderCounters[] array (16 bytes each)
 *
 * Usage:
 * @code
 * // Create buffer
 * ShaderCountersBuffer buffer(1024);  // 1024 entries
 * buffer.Create(device, physicalDevice);
 *
 * // Bind to descriptor set (binding N)
 * VkDescriptorBufferInfo bufferInfo{buffer.GetVkBuffer(), 0, buffer.GetBufferSize()};
 *
 * // Before dispatch
 * buffer.Reset(device);
 *
 * // After dispatch (wait for GPU first)
 * uint32_t count = buffer.Read(device);
 * auto counters = buffer.GetCounters();
 * for (const auto& c : counters) {
 *     // Process metrics...
 * }
 * @endcode
 */
class ShaderCountersBuffer : public IDebugBuffer {
public:
    /**
     * @brief Construct buffer with specified capacity
     * @param entryCount Maximum number of ShaderCounters entries
     */
    explicit ShaderCountersBuffer(uint32_t entryCount = 1024);

    ~ShaderCountersBuffer() override;

    // Non-copyable
    ShaderCountersBuffer(const ShaderCountersBuffer&) = delete;
    ShaderCountersBuffer& operator=(const ShaderCountersBuffer&) = delete;

    // Movable
    ShaderCountersBuffer(ShaderCountersBuffer&& other) noexcept;
    ShaderCountersBuffer& operator=(ShaderCountersBuffer&& other) noexcept;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Create Vulkan buffer with HOST_VISIBLE | HOST_COHERENT memory
     * @param device Vulkan device
     * @param physicalDevice Physical device for memory type selection
     * @return true if creation succeeded
     */
    bool Create(VkDevice device, VkPhysicalDevice physicalDevice);

    /**
     * @brief Destroy Vulkan resources
     * @param device Vulkan device used in Create()
     */
    void Destroy(VkDevice device);

    // =========================================================================
    // IDebugBuffer interface
    // =========================================================================

    DebugBufferType GetType() const override { return DebugBufferType::ShaderCounters; }
    const char* GetTypeName() const override { return "ShaderCounters"; }

    VkBuffer GetVkBuffer() const override { return buffer_; }
    VkDeviceSize GetBufferSize() const override { return bufferSize_; }
    bool IsValid() const override { return buffer_ != VK_NULL_HANDLE && memory_ != VK_NULL_HANDLE; }
    bool IsHostVisible() const override { return isHostVisible_; }

    bool Reset(VkDevice device) override;
    uint32_t Read(VkDevice device) override;

    std::any GetData() const override;

protected:
    std::any GetDataPtr() const override;

public:
    // =========================================================================
    // Counter-specific accessors
    // =========================================================================

    /**
     * @brief Get read-only view of counter entries
     * @return Span of ShaderCounters (empty if no data read yet)
     */
    std::span<const ShaderCounters> GetCounters() const {
        return std::span<const ShaderCounters>(counters_.data(), counters_.size());
    }

    /**
     * @brief Get the configured capacity (max entries)
     */
    uint32_t GetCapacity() const { return capacity_; }

    /**
     * @brief Get number of entries read in last Read() call
     */
    uint32_t GetReadCount() const { return readCount_; }

    /**
     * @brief Calculate required buffer size for given entry count
     */
    static VkDeviceSize CalculateBufferSize(uint32_t entryCount) {
        return sizeof(ShaderCountersHeader) + sizeof(ShaderCounters) * entryCount;
    }

    /**
     * @brief Get aggregated statistics from all entries
     * @return ShaderCounters with accumulated values
     */
    ShaderCounters GetAggregatedCounters() const;

private:
    // Vulkan resources
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize bufferSize_ = 0;

    // Configuration
    uint32_t capacity_ = 0;
    bool isHostVisible_ = true;

    // CPU-side data after readback
    std::vector<ShaderCounters> counters_;
    uint32_t readCount_ = 0;

    // Helper to find suitable memory type
    static uint32_t FindMemoryType(
        VkPhysicalDevice physicalDevice,
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    );
};

/**
 * @brief Factory function for creating ShaderCountersBuffer
 *
 * Convenience function that creates and initializes buffer in one call.
 *
 * @param device Vulkan device
 * @param physicalDevice Physical device
 * @param entryCount Maximum number of entries (default: 1024)
 * @return Configured buffer (check IsValid() for success)
 */
inline ShaderCountersBuffer CreateShaderCountersBuffer(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    uint32_t entryCount = 1024
) {
    ShaderCountersBuffer buffer(entryCount);
    buffer.Create(device, physicalDevice);
    return buffer;
}

} // namespace Vixen::RenderGraph::Debug
