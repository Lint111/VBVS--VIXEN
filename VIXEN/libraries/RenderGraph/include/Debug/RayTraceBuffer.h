#pragma once

#define NOMINMAX

#include "IDebugBuffer.h"
#include "DebugRaySample.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <span>
#include <any>
#include <cstdint>

namespace Vixen::RenderGraph::Debug {

/**
 * @brief GPU buffer for capturing per-ray traversal traces
 *
 * This class implements IDebugBuffer for ray trace data, enabling
 * polymorphic buffer handling in the render graph.
 *
 * Buffer layout:
 * - [0..15]: TraceBufferHeader (writeIndex, capacity, padding)
 * - [16..]: RayTrace[] array (header + MAX_TRACE_STEPS * TraceStep each)
 *
 * Usage:
 * @code
 * RayTraceBuffer buffer(1024);  // 1024 rays max
 * buffer.Create(device, physicalDevice);
 *
 * // Bind to descriptor set at binding 4
 * buffer.Reset(device);
 * // ... dispatch compute shader ...
 * uint32_t count = buffer.Read(device);
 * auto traces = buffer.GetRayTraces();
 * @endcode
 */
class RayTraceBuffer : public IDebugBuffer {
public:
    /**
     * @brief Construct buffer with specified capacity
     * @param rayCapacity Maximum number of rays to capture
     */
    explicit RayTraceBuffer(uint32_t rayCapacity = 1024);

    ~RayTraceBuffer() override;

    // Non-copyable
    RayTraceBuffer(const RayTraceBuffer&) = delete;
    RayTraceBuffer& operator=(const RayTraceBuffer&) = delete;

    // Movable
    RayTraceBuffer(RayTraceBuffer&& other) noexcept;
    RayTraceBuffer& operator=(RayTraceBuffer&& other) noexcept;

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

    DebugBufferType GetType() const override { return DebugBufferType::RayTrace; }
    const char* GetTypeName() const override { return "RayTrace"; }

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
    // RayTrace-specific accessors
    // =========================================================================

    /**
     * @brief Get read-only view of ray traces
     * @return Span of RayTrace (empty if no data read yet)
     */
    std::span<const RayTrace> GetRayTraces() const {
        return std::span<const RayTrace>(rayTraces_.data(), rayTraces_.size());
    }

    /**
     * @brief Get the configured capacity (max rays)
     */
    uint32_t GetCapacity() const { return capacity_; }

    /**
     * @brief Get number of rays read in last Read() call
     */
    uint32_t GetCapturedCount() const { return capturedCount_; }

    /**
     * @brief Get total writes since last reset (may exceed capacity if wrapped)
     */
    uint32_t GetTotalWrites() const { return totalWrites_; }

    /**
     * @brief Check if buffer has wrapped (more writes than capacity)
     */
    bool HasWrapped() const { return totalWrites_ > capacity_; }

    /**
     * @brief Calculate required buffer size for given ray count
     */
    static VkDeviceSize CalculateBufferSize(uint32_t rayCapacity) {
        return sizeof(TraceBufferHeader) + TRACE_RAY_SIZE * rayCapacity;
    }

private:
    // Vulkan resources
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize bufferSize_ = 0;

    // Configuration
    uint32_t capacity_ = 0;
    bool isHostVisible_ = true;

    // CPU-side data after readback
    std::vector<RayTrace> rayTraces_;
    uint32_t capturedCount_ = 0;
    uint32_t totalWrites_ = 0;

    // Helper to find suitable memory type
    static uint32_t FindMemoryType(
        VkPhysicalDevice physicalDevice,
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    );
};

/**
 * @brief Factory function for creating RayTraceBuffer
 *
 * @param device Vulkan device
 * @param physicalDevice Physical device
 * @param rayCapacity Maximum number of rays (default: 1024)
 * @return Configured buffer (check IsValid() for success)
 */
inline RayTraceBuffer CreateRayTraceBuffer(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    uint32_t rayCapacity = 1024
) {
    RayTraceBuffer buffer(rayCapacity);
    buffer.Create(device, physicalDevice);
    return buffer;
}

} // namespace Vixen::RenderGraph::Debug
