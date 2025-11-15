#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/ComputePipelineNodeConfig.h"

// Forward declarations
namespace CashSystem {
    class ComputePipelineCacher;
    struct ComputePipelineWrapper;
    class PipelineLayoutCacher;
    struct PipelineLayoutWrapper;
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
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

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
    // Performance logging (disabled by default)
    std::shared_ptr<class ComputePerformanceLogger> perfLogger_;  // Shared ownership for hierarchy

    // Helper methods
    VkShaderModule CreateShaderModule(VulkanDevice* device, const std::vector<uint32_t>& spirv);
    std::shared_ptr<CashSystem::PipelineLayoutWrapper> CreatePipelineLayout(
        VulkanDevice* device,
        ::ShaderManagement::ShaderDataBundle* shaderBundle,
        VkDescriptorSetLayout descriptorSetLayout
    );
    void CreateComputePipeline(
        VulkanDevice* device,
        VkShaderModule shaderModule,
        ::ShaderManagement::ShaderDataBundle* shaderBundle,
        std::shared_ptr<CashSystem::PipelineLayoutWrapper> layoutWrapper,
        const std::string& layoutKey,
        uint32_t workgroupX,
        uint32_t workgroupY,
        uint32_t workgroupZ
    );
};

} // namespace Vixen::RenderGraph
