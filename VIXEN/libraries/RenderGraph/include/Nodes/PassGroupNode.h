// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <functional>
#include "Core/TypedNodeInstance.h"
#include "Core/VariadicTypedNode.h"   // variadic compile-ordering inputs
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
 * Compile ordering: subclasses VariadicTypedNode so the host can wire an
 * arbitrary number of compile-ordering dependency edges (one per handle-source
 * node) — see PassGroupNodeConfig. These variadic inputs are NEVER read; they
 * exist only to make TopologicalSort place this node after all its sources, so
 * the pass assembler (SetPassAssembler) can read their compiled outputs when this
 * node compiles.
 *
 * auto-sync P4 M3 (M4: variadic compile-ordering inputs)
 */
class PassGroupNode : public VariadicTypedNode<PassGroupNodeConfig> {
public:
    using Base = VariadicTypedNode<PassGroupNodeConfig>;

    PassGroupNode(const std::string& instanceName, NodeType* nodeType);
    ~PassGroupNode() override = default;

    // =========================================================================
    // Host assembly API
    // =========================================================================
    void SetPasses(std::vector<PassStep> passes);
    void AddComputePass(ComputePassStep step);
    void AddRenderPass(RenderPassStep step);

    /**
     * Fill the pass list from this node's own compile, before the schedule is baked, while it is
     * still empty. The compile-ordering inputs guarantee every handle source has compiled, so the
     * assembler reads their outputs directly; it runs once, like the host's own AddComputePass calls.
     */
    void SetPassAssembler(std::function<void(PassGroupNode&)> assembler) { passAssembler_ = std::move(assembler); }

    /// Accessor for smoke test (no GPU needed)
    [[nodiscard]] size_t PassCount() const { return passes_.size(); }

protected:
    void VariadicCompileImpl(VariadicCompileContext& ctx) override;
    void VariadicExecuteImpl(VariadicExecuteContext& ctx) override;
    void VariadicCleanupImpl(VariadicCleanupContext& ctx) override;

private:
    std::vector<PassStep>  passes_;
    std::function<void(PassGroupNode&)> passAssembler_;
    FrameSyncSchedule      intraSchedule_;   // baked in CompileImpl

    // Device and command pool references (follow ComputeDispatchNode pattern)
    VulkanDevice*  vulkanDevice = nullptr;
    VkCommandPool  commandPool  = VK_NULL_HANDLE;

    // Per-swapchain-image command buffers
    StatefulContainer<VkCommandBuffer> commandBuffers_;
};

} // namespace Vixen::RenderGraph
