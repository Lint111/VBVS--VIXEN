// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// auto-sync FrameGraph P5b M2: generic single-compute-pass submit node.
#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "State/StatefulContainer.h"
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
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void RecordComputeCommands(Context& ctx, VkCommandBuffer cmdBuffer,
                               uint32_t imageIndex, bool isConsumer);
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
};

} // namespace Vixen::RenderGraph
