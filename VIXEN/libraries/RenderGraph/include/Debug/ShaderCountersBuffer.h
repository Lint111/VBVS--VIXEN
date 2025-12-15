#pragma once

#define NOMINMAX

#include "IDebugBuffer.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <span>
#include <any>
#include <algorithm>

namespace Vixen::RenderGraph::Debug {

/**
 * @brief GPU-side shader counter data matching GLSL layout
 *
 * This struct MUST match the GLSL layout in ShaderCounters.glsl:
 * @code
 * layout(std430, binding = N) buffer ShaderCountersBuffer {
 *     uint totalVoxelsTraversed;
 *     uint totalRaysCast;
 *     uint totalNodesVisited;
 *     uint totalLeafNodesVisited;
 *     uint totalEmptySpaceSkipped;
 *     uint rayHitCount;
 *     uint rayMissCount;
 *     uint earlyTerminations;
 *     uint nodeVisitsPerLevel[16];
 *     uint cacheHitsPerLevel[16];
 *     uint cacheMissesPerLevel[16];
 *     uint _padding[8];
 * } shaderCounters;
 * @endcode
 *
 * All counters are atomically incremented by shader invocations.
 * Total buffer size: 256 bytes (64 uint32_t values)
 */
struct GPUShaderCounters {
    static constexpr size_t MAX_SVO_LEVELS = 16;

    uint32_t totalVoxelsTraversed = 0;    ///< Total voxels traversed across all rays
    uint32_t totalRaysCast = 0;           ///< Total rays cast this frame
    uint32_t totalNodesVisited = 0;       ///< Octree nodes visited
    uint32_t totalLeafNodesVisited = 0;   ///< Leaf nodes (bricks) visited
    uint32_t totalEmptySpaceSkipped = 0;  ///< Voxels skipped via empty-space
    uint32_t rayHitCount = 0;             ///< Rays that hit geometry
    uint32_t rayMissCount = 0;            ///< Rays that missed
    uint32_t earlyTerminations = 0;       ///< Rays that hit max iterations

    // Per-level SVO statistics (for cache locality analysis)
    uint32_t nodeVisitsPerLevel[MAX_SVO_LEVELS] = {};   ///< Node visits per octree level
    uint32_t cacheHitsPerLevel[MAX_SVO_LEVELS] = {};    ///< Consecutive/sibling accesses
    uint32_t cacheMissesPerLevel[MAX_SVO_LEVELS] = {};  ///< Random accesses

    uint32_t _padding[8] = {0};           ///< Cache line alignment

    /**
     * @brief Reset all counters to zero
     */
    void Clear() {
        totalVoxelsTraversed = 0;
        totalRaysCast = 0;
        totalNodesVisited = 0;
        totalLeafNodesVisited = 0;
        totalEmptySpaceSkipped = 0;
        rayHitCount = 0;
        rayMissCount = 0;
        earlyTerminations = 0;
        std::fill(std::begin(nodeVisitsPerLevel), std::end(nodeVisitsPerLevel), 0u);
        std::fill(std::begin(cacheHitsPerLevel), std::end(cacheHitsPerLevel), 0u);
        std::fill(std::begin(cacheMissesPerLevel), std::end(cacheMissesPerLevel), 0u);
        std::fill(std::begin(_padding), std::end(_padding), 0u);
    }

    /**
     * @brief Check if counters contain valid data
     */
    bool HasData() const {
        return totalRaysCast > 0;
    }

    /**
     * @brief Calculate average octree iterations (node visits) per ray
     *
     * NOTE: This counts ESVO traversal iterations, not individual voxels.
     * Each iteration is a PUSH/ADVANCE/POP in the octree traversal.
     * For brick-level voxel counting, a separate counter would be needed.
     */
    float GetAvgIterationsPerRay() const {
        return totalRaysCast > 0
            ? static_cast<float>(totalVoxelsTraversed) / static_cast<float>(totalRaysCast)
            : 0.0f;
    }

    /// @deprecated Use GetAvgIterationsPerRay() - name is misleading
    float GetAvgVoxelsPerRay() const { return GetAvgIterationsPerRay(); }

    /**
     * @brief Calculate ray hit rate (0.0 - 1.0)
     */
    float GetHitRate() const {
        return totalRaysCast > 0
            ? static_cast<float>(rayHitCount) / static_cast<float>(totalRaysCast)
            : 0.0f;
    }

    /**
     * @brief Get cache hit rate for a specific SVO level (0.0-1.0)
     */
    float GetCacheHitRateForLevel(size_t level) const {
        if (level >= MAX_SVO_LEVELS) return 0.0f;
        uint32_t total = cacheHitsPerLevel[level] + cacheMissesPerLevel[level];
        return total > 0 ? static_cast<float>(cacheHitsPerLevel[level]) / static_cast<float>(total) : 0.0f;
    }

    /**
     * @brief Get overall cache hit rate across all levels (0.0-1.0)
     */
    float GetOverallCacheHitRate() const {
        uint32_t totalHits = 0, totalMisses = 0;
        for (size_t i = 0; i < MAX_SVO_LEVELS; ++i) {
            totalHits += cacheHitsPerLevel[i];
            totalMisses += cacheMissesPerLevel[i];
        }
        uint32_t total = totalHits + totalMisses;
        return total > 0 ? static_cast<float>(totalHits) / static_cast<float>(total) : 0.0f;
    }
};

// Ensure struct matches GLSL layout exactly: 256 bytes (8 + 16*3 + 8 = 64 uint32_t values)
static_assert(sizeof(GPUShaderCounters) == 256, "GPUShaderCounters must be 256 bytes to match GLSL layout");
static_assert(alignof(GPUShaderCounters) == 4, "GPUShaderCounters must be 4-byte aligned");

/**
 * @brief GPU buffer for collecting shader performance counters
 *
 * This class manages a HOST_VISIBLE | HOST_COHERENT Vulkan buffer for
 * accumulating shader performance metrics via GPU atomics.
 *
 * Implements IDebugBuffer interface for polymorphic usage with
 * DebugBufferReaderNode.
 *
 * Buffer layout: Single GPUShaderCounters struct (64 bytes)
 * - No header, no array - shaders atomicAdd directly to fields
 * - Must be zeroed before dispatch via Reset()
 * - Read back after GPU work completes
 *
 * Usage:
 * @code
 * // Create buffer (capacity param is unused, kept for API compat)
 * ShaderCountersBuffer buffer;
 * buffer.Create(device, physicalDevice);
 *
 * // Bind to descriptor set at binding 6 (matches SHADER_COUNTERS_BINDING)
 * VkDescriptorBufferInfo bufferInfo{buffer.GetVkBuffer(), 0, buffer.GetBufferSize()};
 *
 * // Before dispatch - zero the counters
 * buffer.Reset(device);
 *
 * // After dispatch (wait for GPU first)
 * buffer.Read(device);
 * const auto& counters = buffer.GetCounters();
 * float avgVoxels = counters.GetAvgVoxelsPerRay();
 * @endcode
 */
class ShaderCountersBuffer : public IDebugBuffer {
public:
    /**
     * @brief Conversion type declaration for compile-time type system
     *
     * Enables the RenderGraph type system to recognize ShaderCountersBuffer
     * as a wrapper around VkBuffer without explicit registration.
     * See CompileTimeResourceSystem.h for the conversion_type pattern.
     */
    using conversion_type = VkBuffer;

    /**
     * @brief Implicit conversion to VkBuffer for descriptor binding
     */
    operator VkBuffer() const { return buffer_; }

    /**
     * @brief Construct buffer
     * @param capacity Unused, kept for API compatibility with DebugCaptureResource factory
     */
    explicit ShaderCountersBuffer(uint32_t capacity = 1);

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

    // Debug: track VkBuffer destruction
    VkBuffer GetDebugBuffer() const { return buffer_; }  // For tracking

    // =========================================================================
    // IDebugBuffer interface
    // =========================================================================

    DebugBufferType GetType() const override { return DebugBufferType::ShaderCounters; }
    const char* GetTypeName() const override { return "ShaderCounters"; }

    VkBuffer GetVkBuffer() const override { return buffer_; }
    VkDeviceSize GetBufferSize() const override { return sizeof(GPUShaderCounters); }
    bool IsValid() const override { return buffer_ != VK_NULL_HANDLE && memory_ != VK_NULL_HANDLE; }
    bool IsHostVisible() const override { return true; }

    /**
     * @brief Zero the GPU buffer before dispatch
     */
    bool Reset(VkDevice device) override;

    /**
     * @brief Read counters from GPU to CPU-side cache
     * @return 1 if data was read successfully, 0 otherwise
     */
    uint32_t Read(VkDevice device) override;

    /**
     * @brief Get the counter data as std::any (contains GPUShaderCounters)
     */
    std::any GetData() const override;

protected:
    std::any GetDataPtr() const override;

public:
    // =========================================================================
    // Counter-specific accessors
    // =========================================================================

    /**
     * @brief Get the counter data (read from GPU after Read() call)
     */
    const GPUShaderCounters& GetCounters() const { return counters_; }

    /**
     * @brief Check if counters have valid data
     */
    bool HasData() const { return counters_.HasData(); }

    /**
     * @brief Get average iterations per ray (convenience accessor)
     */
    float GetAvgIterationsPerRay() const { return counters_.GetAvgIterationsPerRay(); }

    /// @deprecated Use GetAvgIterationsPerRay()
    float GetAvgVoxelsPerRay() const { return GetAvgIterationsPerRay(); }

private:
    // Vulkan resources
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;

    // CPU-side cache of counter data
    GPUShaderCounters counters_;

    // Helper to find suitable memory type
    static uint32_t FindMemoryType(
        VkPhysicalDevice physicalDevice,
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    );
};

} // namespace Vixen::RenderGraph::Debug
