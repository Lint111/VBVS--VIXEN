#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <optional>
#include <cstdint>

namespace Vixen::RenderGraph {

/**
 * @brief Push constant data container for dispatch passes
 *
 * Holds raw bytes and metadata for vkCmdPushConstants.
 * Used when passes have different push constant values.
 */
struct PushConstantData {
    std::vector<uint8_t> data;           // Raw push constant bytes
    VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    uint32_t offset = 0;                 // Byte offset in push constant range
};

/**
 * @brief Descriptor for a single compute dispatch pass
 *
 * Contains all information needed to record a vkCmdDispatch:
 * - Pipeline and layout for binding
 * - Descriptor sets for resource binding
 * - Optional push constants for per-pass data
 * - Work group dimensions
 * - Debug name for profiling/logging
 *
 * Part of Sprint 6: Timeline Foundation - MultiDispatchNode
 *
 * @see MultiDispatchNode for usage
 */
struct DispatchPass {
    // Pipeline binding
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    // Resource binding
    std::vector<VkDescriptorSet> descriptorSets;
    uint32_t firstSet = 0;               // First descriptor set index

    // Push constants (optional - per-pass overrides)
    std::optional<PushConstantData> pushConstants;

    // Dispatch dimensions
    glm::uvec3 workGroupCount = {1, 1, 1};

    // Debug/profiling
    std::string debugName;

    // Validation helpers
    [[nodiscard]] bool IsValid() const {
        return pipeline != VK_NULL_HANDLE &&
               layout != VK_NULL_HANDLE &&
               workGroupCount.x > 0 &&
               workGroupCount.y > 0 &&
               workGroupCount.z > 0;
    }

    [[nodiscard]] uint32_t TotalWorkGroups() const {
        return workGroupCount.x * workGroupCount.y * workGroupCount.z;
    }
};

/**
 * @brief Barrier descriptor for explicit synchronization between passes
 *
 * Allows inserting memory barriers between dispatch passes when
 * automatic barrier insertion is insufficient.
 */
struct DispatchBarrier {
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    std::vector<VkMemoryBarrier2> memoryBarriers;

    [[nodiscard]] bool IsEmpty() const {
        return bufferBarriers.empty() &&
               imageBarriers.empty() &&
               memoryBarriers.empty();
    }
};

/**
 * @brief Statistics for multi-dispatch execution
 *
 * Collected during ExecuteImpl for performance monitoring.
 */
struct MultiDispatchStats {
    uint32_t dispatchCount = 0;          // Number of dispatches recorded
    uint32_t barrierCount = 0;           // Number of barriers inserted
    uint64_t totalWorkGroups = 0;        // Sum of all work groups
    double recordTimeMs = 0.0;           // CPU time to record commands
};

} // namespace Vixen::RenderGraph
