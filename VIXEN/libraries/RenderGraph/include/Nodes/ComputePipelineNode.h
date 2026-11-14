#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/ComputePipelineNodeConfig.h"

#include <functional>
#include <memory>

// Forward declarations
namespace CashSystem {
    class ComputePipelineCacher;
    struct ComputePipelineWrapper;
    class PipelineLayoutCacher;
    struct PipelineLayoutWrapper;
    struct ShaderModuleWrapper;
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

    /**
     * @brief R864.4: a callable that creates this node's pipeline for a NEW shader bundle on a
     * worker thread, through the same ComputePipelineCacher key the node's next Compile uses.
     *
     * Resolved here, on the frame thread: the device, the cachers, the current descriptor set
     * layout, the workgroup size and the current descriptor-interface uuid. The returned callable
     * touches no node state. It creates nothing when the new bundle's interface differs (its
     * pipeline would be built against a stale layout; the swap Compile builds it instead), and
     * nothing before the node's first Compile (empty function). The swap Compile then finds the
     * pipeline in the cacher instead of compiling it on the frame thread.
     */
    std::function<void(const std::shared_ptr<::ShaderManagement::ShaderDataBundle>&)> MakePipelineWarmer();

protected:
    void TypedSetupImpl(TypedSetupContext& ctx) override;
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;
    void TypedCleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Descriptor-interface uuid of the bundle the pipeline was last compiled from (R864.4 warmer guard)
    std::string compiledInterfaceUuid_;
    VkDescriptorSetLayout compiledDescriptorSetLayout_ = VK_NULL_HANDLE;
    uint32_t compiledWorkgroup_[3] = {8, 8, 1};

    // Cached outputs (for multi-frame stability)
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;

    // Entry point name storage (must outlive GetOrCreate call)
    std::string entryPointName_;

    // Shared wrappers from cachers (the module is content-keyed by its SPIR-V)
    std::shared_ptr<CashSystem::ComputePipelineWrapper> pipelineWrapper_;
    std::shared_ptr<CashSystem::ShaderModuleWrapper> shaderModuleWrapper_;
    // Performance logging (disabled by default)
    std::shared_ptr<class ComputePerformanceLogger> perfLogger_;  // Shared ownership for hierarchy

    // Task profile for compile-time cost estimation (Sprint 6.5)
    ITaskProfile* compileProfile_ = nullptr;

    // Helper methods
    VkShaderModule CreateShaderModule(VulkanDevice* device, const std::vector<uint32_t>& spirv,
                                      const std::string& programName, const std::string& entryPoint);
    std::shared_ptr<CashSystem::PipelineLayoutWrapper> CreatePipelineLayout(
        VulkanDevice* device,
        const std::shared_ptr<::ShaderManagement::ShaderDataBundle>& shaderBundle,
        VkDescriptorSetLayout descriptorSetLayout
    );
    void CreateComputePipeline(
        VulkanDevice* device,
        VkShaderModule shaderModule,
        const std::shared_ptr<::ShaderManagement::ShaderDataBundle>& shaderBundle,
        std::shared_ptr<CashSystem::PipelineLayoutWrapper> layoutWrapper,
        const std::string& layoutKey,
        uint32_t workgroupX,
        uint32_t workgroupY,
        uint32_t workgroupZ
    );
};

} // namespace Vixen::RenderGraph
