// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "State/StatefulContainer.h"
#include "Data/Nodes/PassGroupNodeConfig.h"
#include "Core/FrameSyncSchedule.h"
#include "Data/PassStep.h"
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for PassGroupNode (auto-sync P4 M3)
 */
class PassGroupNodeType : public TypedNodeType<PassGroupNodeConfig> {
public:
    PassGroupNodeType(const std::string& typeName = "PassGroup")
        : TypedNodeType<PassGroupNodeConfig>(typeName) {}
    virtual ~PassGroupNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Generic multi-pass node: assembles an ordered list of heterogeneous passes
 * (compute and/or graphics) into ONE command buffer + ONE submit, with intra-pass
 * barriers auto-baked by BuildPassGroupSchedule (P4 core).
 *
 * Host assembly API: the builder fills concrete Vulkan handles after
 * pipelines/render passes/framebuffers are created.
 *
 * auto-sync P4 M3
 */
class PassGroupNode : public TypedNode<PassGroupNodeConfig> {
public:
    PassGroupNode(const std::string& instanceName, NodeType* nodeType);
    ~PassGroupNode() override = default;

    // =========================================================================
    // Host assembly API
    // =========================================================================
    void SetPasses(std::vector<PassStep> passes);
    void AddComputePass(ComputePassStep step);
    void AddRenderPass(RenderPassStep step);

    /// Accessor for smoke test (no GPU needed)
    [[nodiscard]] size_t PassCount() const { return passes_.size(); }

protected:
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    std::vector<PassStep>  passes_;
    FrameSyncSchedule      intraSchedule_;   // baked in CompileImpl

    // Device and command pool references (follow ComputeDispatchNode pattern)
    VulkanDevice*  vulkanDevice = nullptr;
    VkCommandPool  commandPool  = VK_NULL_HANDLE;

    // Per-swapchain-image command buffers
    StatefulContainer<VkCommandBuffer> commandBuffers_;
};

} // namespace Vixen::RenderGraph
