#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Nodes/ComputePipelineNodeConfig.h"

// Forward declarations
namespace CashSystem {
    class ComputePipelineCacher;
    struct ComputePipelineWrapper;
}

namespace ShaderManagement {
    struct ShaderDataBundle;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for compute pipeline creation
 *
 * Creates VkComputePipeline from SPIRV shaders using ComputePipelineCacher.
 * Shares VkPipelineCache with graphics pipelines for optimal performance.
 */
class ComputePipelineNodeType : public TypedNodeType<ComputePipelineNodeConfig> {
public:
    ComputePipelineNodeType(const std::string& typeName = "ComputePipeline")
        : TypedNodeType<ComputePipelineNodeConfig>(typeName) {}
    virtual ~ComputePipelineNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
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
    ComputePipelineNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~ComputePipelineNode() override = default;

protected:
    void SetupImpl(Context& ctx) override;
    void CompileImpl(Context& ctx) override;
    void ExecuteImpl(Context& ctx) override;
    void CleanupImpl() override;

private:
    // Cached outputs (for multi-frame stability)
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;

    // Entry point name storage (must outlive GetOrCreate call)
    std::string entryPointName_;

    // Shared wrappers from cachers
    std::shared_ptr<CashSystem::ComputePipelineWrapper> pipelineWrapper_;

#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
    // Performance logging (debug only)
    std::unique_ptr<class ComputePerformanceLogger> perfLogger_;
#endif
};

} // namespace Vixen::RenderGraph
