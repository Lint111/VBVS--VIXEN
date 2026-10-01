// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// auto-sync FrameGraph P5b M2: generic single-compute-pass submit node.
#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "State/StatefulContainer.h"
#include "Core/GPUPerformanceLogger.h"
#include "Data/Nodes/ComputeStageNodeConfig.h"
#include "Core/FrameSyncSchedule.h"
#include "Nodes/Common/SwapchainBarriers.h"

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the generic compute-stage submit node.
 */
class ComputeStageNodeType : public TypedNodeType<ComputeStageNodeConfig> {
public:
    ComputeStageNodeType(const std::string& typeName = "ComputeStage")
        : TypedNodeType<ComputeStageNodeConfig>(typeName) {}
    ~ComputeStageNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Generic single-compute-pass submit node (auto-sync P5b M2).
 *
 * Runs one compute dispatch into a per-image command buffer and submits it via
 * vkQueueSubmit2, consuming the baked graph timeline edges with the M1 pattern
 * (deduped signals via std::set; per-edge waits). Configurable producer vs
 * consumer role via PARAM_IS_CONSUMER. See ComputeStageNodeConfig.h for the full
 * role contract. Uses the base NodeInstance device member (SetDevice/GetDevice).
 */
class ComputeStageNode : public TypedNode<ComputeStageNodeConfig> {
public:
    using Base = TypedNode<ComputeStageNodeConfig>;

    ComputeStageNode(const std::string& instanceName, NodeType* nodeType);
    ~ComputeStageNode() override = default;

protected:
    void TypedSetupImpl(TypedSetupContext& ctx) override;
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;
    void TypedCleanupImpl(TypedCleanupContext& ctx) override;

private:
    void RecordComputeCommands(Context& ctx, VkCommandBuffer cmdBuffer,
                               uint32_t imageIndex, uint32_t frameIndex, bool isConsumer);
    void BindComputePipeline(VkCommandBuffer cmdBuffer, VkPipeline pipeline,
                             VkPipelineLayout layout, VkDescriptorSet descriptorSet);
    void SetPushConstants(Context& ctx, VkCommandBuffer cmdBuffer, VkPipelineLayout layout);

    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    // Per-swapchain-image command buffers with dirty-state tracking.
    StatefulContainer<VkCommandBuffer> commandBuffers_;

    // Previous-frame inputs for dirty detection.
    VkPipeline lastPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout lastPipelineLayout_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> lastDescriptorSets_;

    // Task 0.1 (Baked-Content Perf Audit, top action #9): per-pass GPU timing, same
    // centralized-GPUQueryManager pattern ComputeDispatchNode/UIRenderNode already use — lets
    // direct_lighting/spatial_reuse/probe_update each get their own PerfCsvWriter column
    // instead of only the ESVO march pass being GPU-timed.
    std::shared_ptr<GPUPerformanceLogger> gpuPerfLogger_;

public:
    /// Get GPU performance logger for external metrics extraction (e.g. PerfCsvWriter).
    /// @return Pointer to GPUPerformanceLogger, or nullptr if not initialized.
    [[nodiscard]] GPUPerformanceLogger* GetGPUPerformanceLogger() const { return gpuPerfLogger_.get(); }
};

} // namespace Vixen::RenderGraph
