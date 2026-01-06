#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <map>  // Sprint 6.1: Task #314 - Per-group statistics

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

    // Sprint 6.1: Group-based dispatch support
    // When set, this pass belongs to a specific group for partitioned processing
    std::optional<uint32_t> groupId;

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
 * @brief Per-group dispatch statistics
 *
 * Sprint 6.1: Task #314 - Pipeline Statistics
 * Tracks performance metrics for a single dispatch group.
 */
struct GroupDispatchStats {
    uint32_t dispatchCount = 0;          // Number of dispatches in this group
    uint64_t totalWorkGroups = 0;        // Sum of work groups in this group
    double recordTimeMs = 0.0;           // CPU time to record this group's commands
};

/**
 * @brief Statistics for multi-dispatch execution
 *
 * Collected during ExecuteImpl for performance monitoring.
 * Sprint 6.1: Task #314 adds per-group statistics breakdown.
 */
struct MultiDispatchStats {
    // Overall statistics
    uint32_t dispatchCount = 0;          // Total number of dispatches recorded
    uint32_t barrierCount = 0;           // Number of barriers inserted
    uint64_t totalWorkGroups = 0;        // Sum of all work groups across all groups
    double recordTimeMs = 0.0;           // Total CPU time to record commands

    // Per-group statistics (Sprint 6.1: Task #314)
    // Maps group ID -> statistics for that group
    // Empty when GROUP_INPUTS not connected (legacy mode)
    std::map<uint32_t, GroupDispatchStats> groupStats;

    /**
     * @brief Get number of dispatch groups
     * @return Number of groups with recorded statistics
     */
    [[nodiscard]] uint32_t GetGroupCount() const {
        return static_cast<uint32_t>(groupStats.size());
    }

    /**
     * @brief Get statistics for a specific group
     * @param groupId Group identifier
     * @return Pointer to group stats, or nullptr if group not found
     */
    [[nodiscard]] const GroupDispatchStats* GetGroupStats(uint32_t groupId) const {
        auto it = groupStats.find(groupId);
        return (it != groupStats.end()) ? &it->second : nullptr;
    }
};

} // namespace Vixen::RenderGraph
