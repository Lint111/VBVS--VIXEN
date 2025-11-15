#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/GraphicsPipelineNodeConfig.h"
#include <memory>
#include <unordered_map>

// Forward declarations
namespace CashSystem {
    class PipelineCacher;
    struct PipelineWrapper;
    struct ShaderModuleWrapper;
}

namespace ShaderManagement {
    struct ShaderDataBundle;
    enum class ShaderStage : uint32_t;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for assembling graphics pipelines
 *
 * Combines shaders, render pass, vertex input description, and state configuration
 * to create a complete Vulkan graphics pipeline.
 *
 * Type ID: 108
 */
class GraphicsPipelineNodeType : public TypedNodeType<GraphicsPipelineNodeConfig> {
public:
    GraphicsPipelineNodeType(const std::string& typeName = "GraphicsPipeline")
        : TypedNodeType<GraphicsPipelineNodeConfig>(typeName) {}
    virtual ~GraphicsPipelineNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Node instance for graphics pipeline creation
 * 
 * Now uses TypedNode<GraphicsPipelineNodeConfig> for compile-time type safety.
 * All inputs/outputs are accessed via the typed config slot API.
 * 
 * See GraphicsPipelineNodeConfig.h for slot definitions and parameters.
 */
class GraphicsPipelineNode : public TypedNode<GraphicsPipelineNodeConfig> {
public:

    GraphicsPipelineNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~GraphicsPipelineNode() override = default;

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:    
    // Pipeline resources (outputs)
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;  // Auto-generated or manual
    std::vector<VkPushConstantRange> pushConstantRanges;  // Extracted from reflection (Phase 5)

    // Configuration from parameters
    bool enableDepthTest = true;
    bool enableDepthWrite = true;
    bool enableVertexInput = true;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // Shader stage data (built from reflection)
    std::vector<VkPipelineShaderStageCreateInfo> shaderStageInfos;
    std::unordered_map<::ShaderManagement::ShaderStage, std::shared_ptr<CashSystem::ShaderModuleWrapper>> shaderModules;
    ::ShaderManagement::ShaderDataBundle* currentShaderBundle = nullptr;  // Current bundle for reflection (not owned)

    // Pipeline assembly orchestration
    void CreatePipelineWithCache(TypedNode<GraphicsPipelineNodeConfig>::TypedCompileContext& ctx);

    // Pipeline setup steps (extracted from CreatePipeline)
    void CreatePipelineCache();
    void CreatePipelineLayout();
    void CreatePipeline(TypedNode<GraphicsPipelineNodeConfig>::TypedCompileContext& ctx);

    // Pipeline state builder methods (extracted from CreatePipeline)
    void BuildDynamicStateInfo(VkPipelineDynamicStateCreateInfo& outState);
    void BuildVertexInputState(VkPipelineVertexInputStateCreateInfo& outState);
    void BuildInputAssemblyState(VkPipelineInputAssemblyStateCreateInfo& outState);
    void BuildRasterizationState(VkPipelineRasterizationStateCreateInfo& outState);
    void BuildMultisampleState(VkPipelineMultisampleStateCreateInfo& outState);
    void BuildDepthStencilState(VkPipelineDepthStencilStateCreateInfo& outState);
    void BuildColorBlendState(VkPipelineColorBlendStateCreateInfo& outState);
    void BuildViewportState(VkPipelineViewportStateCreateInfo& outState);

    // Shader and vertex input reflection
    void BuildShaderStages(std::shared_ptr<ShaderManagement::ShaderDataBundle> bundle);
    void BuildVertexInputsFromReflection(
        std::shared_ptr<ShaderManagement::ShaderDataBundle> bundle,
        std::vector<VkVertexInputBindingDescription>& outBindings,
        std::vector<VkVertexInputAttributeDescription>& outAttributes);

    // CashSystem integration - cached during Compile()
    CashSystem::PipelineCacher* pipelineCacher = nullptr;
    std::shared_ptr<CashSystem::PipelineWrapper> cachedPipelineWrapper;
};

} // namespace Vixen::RenderGraph
