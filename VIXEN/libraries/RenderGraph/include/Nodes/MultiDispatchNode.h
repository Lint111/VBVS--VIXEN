#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "State/StatefulContainer.h"
#include "Data/DispatchPass.h"
#include "Data/Nodes/MultiDispatchNodeConfig.h"

#include <vector>
#include <deque>
#include <map>  // Sprint 6.1: For groupedDispatches_ (deterministic ordering)

namespace Vixen::RenderGraph {

/**
 * @brief Node type for multi-dispatch compute operations
 *
 * Factory for creating MultiDispatchNode instances.
 */
class MultiDispatchNodeType : public TypedNodeType<MultiDispatchNodeConfig> {
public:
    MultiDispatchNodeType(const std::string& typeName = "MultiDispatch")
        : TypedNodeType<MultiDispatchNodeConfig>(typeName) {}
    virtual ~MultiDispatchNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Node that queues and executes multiple compute dispatches
 *
 * Records multiple vkCmdDispatch calls to a single command buffer with
 * optional automatic barrier insertion between passes. Designed for
 * multi-pass compute sequences like:
 *
 * - Prefilter -> Main -> Postfilter
 * - Mipmap generation chains
 * - Iterative simulation steps
 * - Multi-stage post-processing
 *
 * Sprint 6: Timeline Foundation - Task #312
 *
 * ## Usage Pattern
 *
 * ```cpp
 * // Get node reference
 * auto* multiDispatch = graph->GetNode<MultiDispatchNode>("computeChain");
 *
 * // Queue passes (call before frame execution)
 * DispatchPass prefilter{...};
 * DispatchPass mainPass{...};
 * DispatchPass postfilter{...};
 *
 * multiDispatch->QueueDispatch(std::move(prefilter));
 * multiDispatch->QueueDispatch(std::move(mainPass));
 * multiDispatch->QueueDispatch(std::move(postfilter));
 *
 * // ExecuteImpl records all queued passes to command buffer
 * // Queue is cleared after execution
 * ```
 *
 * ## Barrier Insertion
 *
 * When `autoBarriers` parameter is true (default), the node inserts
 * VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT barriers between passes to
 * handle UAV (read-after-write) hazards.
 *
 * For fine-grained control, use QueueBarrier() to insert explicit
 * barriers at specific points in the dispatch sequence.
 *
 * @see DispatchPass for pass descriptor
 * @see ComputeDispatchNode for single-dispatch operations
 */
class MultiDispatchNode : public TypedNode<MultiDispatchNodeConfig> {
public:
    MultiDispatchNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~MultiDispatchNode() override = default;

    // =========================================================================
    // PUBLIC API
    // =========================================================================

    /**
     * @brief Queue a compute dispatch for execution
     *
     * Adds a dispatch pass to the queue. All queued passes are recorded
     * to the command buffer during ExecuteImpl, then the queue is cleared.
     *
     * @param pass Dispatch pass descriptor (moved)
     * @return Index of the queued pass (for barrier insertion)
     * @throws std::runtime_error if pass is invalid or queue is full
     */
    size_t QueueDispatch(DispatchPass&& pass);

    /**
     * @brief Queue an explicit barrier between passes
     *
     * Inserts a synchronization point at the current position in the
     * dispatch queue. The barrier will be recorded before the next
     * QueueDispatch pass.
     *
     * @param barrier Barrier descriptor with buffer/image/memory barriers
     */
    void QueueBarrier(DispatchBarrier&& barrier);

    /**
     * @brief Clear all queued dispatches and barriers
     *
     * Typically called automatically after ExecuteImpl, but can be
     * called manually to reset the queue.
     */
    void ClearQueue();

    /**
     * @brief Get current queue size
     * @return Number of queued dispatches
     */
    [[nodiscard]] size_t GetQueueSize() const { return dispatchQueue_.size(); }

    /**
     * @brief Get execution statistics from last frame
     * @return Statistics from most recent ExecuteImpl
     */
    [[nodiscard]] const MultiDispatchStats& GetStats() const { return stats_; }

protected:
    // =========================================================================
    // NODE LIFECYCLE
    // =========================================================================

    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // =========================================================================
    // INTERNAL HELPERS
    // =========================================================================

    /**
     * @brief Record all queued dispatches to command buffer
     */
    void RecordDispatches(VkCommandBuffer cmdBuffer);

    /**
     * @brief Insert automatic UAV barrier between passes
     */
    void InsertAutoBarrier(VkCommandBuffer cmdBuffer);

    /**
     * @brief Record explicit barrier from queue
     */
    void RecordBarrier(VkCommandBuffer cmdBuffer, const DispatchBarrier& barrier);

    // =========================================================================
    // STATE
    // =========================================================================

    // Device and command pool references
    VulkanDevice* vulkanDevice_ = nullptr;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    // Per-swapchain-image command buffers
    StatefulContainer<VkCommandBuffer> commandBuffers_;

    // Dispatch queue (cleared after each frame)
    std::deque<DispatchPass> dispatchQueue_;

    // Barrier queue (indices where barriers should be inserted)
    std::vector<std::pair<size_t, DispatchBarrier>> barrierQueue_;

    // Sprint 6.1: Group-based dispatch support
    // Maps group ID -> vector of dispatch passes for that group
    // Empty map means no GROUP_INPUTS connected (fall back to dispatchQueue_)
    // Using std::map (not unordered_map) for deterministic group execution order
    std::map<uint32_t, std::vector<DispatchPass>> groupedDispatches_;

    // Configuration
    bool autoBarriers_ = true;
    bool enableTimestamps_ = false;

    // Statistics
    MultiDispatchStats stats_;
};

} // namespace Vixen::RenderGraph
