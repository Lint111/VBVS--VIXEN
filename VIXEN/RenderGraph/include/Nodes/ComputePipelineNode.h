#pragma once

#include "Core/TypedNode.h"
#include "Nodes/ComputePipelineNodeConfig.h"
#include "CashSystem/ComputePipelineCacher.h"

namespace Vixen::RenderGraph {

/**
 * @brief Node type for ComputePipelineNode
 */
class ComputePipelineNodeType : public NodeType {
public:
    ComputePipelineNodeType() : NodeType("ComputePipelineNode") {}

    std::unique_ptr<INode> CreateInstance() const override;
};

/**
 * @brief Node for creating Vulkan compute pipelines from SPIRV shaders
 *
 * Creates VkComputePipeline from ShaderDataBundle using ComputePipelineCacher.
 * Shares VkPipelineCache with graphics pipelines for optimal performance.
 *
 * Phase: G.1 (Compute Pipeline Setup)
 * Dependencies: ShaderLibraryNode (shader reflection)
 * Consumers: ComputeDispatchNode (dispatch compute work)
 *
 * Features:
 * - Auto-generates descriptor set layout from shader reflection (if not provided)
 * - Extracts push constants from shader reflection
 * - Extracts workgroup size from shader reflection (if not specified in parameters)
 * - Uses shared VkPipelineCache for memory efficiency
 */
class ComputePipelineNode : public TypedNode<ComputePipelineNodeConfig> {
public:
    ComputePipelineNode();
    ~ComputePipelineNode() override = default;

    const char* GetTypeName() const override { return "ComputePipelineNode"; }

protected:
    void SetupImpl(Context& ctx) override;
    void CompileImpl(Context& ctx) override;
    void ExecuteImpl(TaskContext& ctx) override;
    void CleanupImpl() override;

private:
    // Cached outputs (for multi-frame stability)
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;

    // Shared wrappers from cachers
    std::shared_ptr<CashSystem::ComputePipelineWrapper> pipelineWrapper_;
};

} // namespace Vixen::RenderGraph
