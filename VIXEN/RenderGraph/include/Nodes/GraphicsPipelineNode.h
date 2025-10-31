#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Nodes/GraphicsPipelineNodeConfig.h"
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
class GraphicsPipelineNodeType : public NodeType {
public:
    GraphicsPipelineNodeType(const std::string& typeName = "GraphicsPipeline");
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
    virtual ~GraphicsPipelineNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;

protected:
    void CleanupImpl() override;

private:
    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;
    
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
    std::unordered_map<ShaderManagement::ShaderStage, std::shared_ptr<CashSystem::ShaderModuleWrapper>> shaderModules;
    std::shared_ptr<ShaderManagement::ShaderDataBundle> currentShaderBundle;  // Current bundle for reflection

    // Helper functions
    void BuildShaderStages(std::shared_ptr<ShaderManagement::ShaderDataBundle> bundle);
    void BuildVertexInputsFromReflection(
        std::shared_ptr<ShaderManagement::ShaderDataBundle> bundle,
        std::vector<VkVertexInputBindingDescription>& outBindings,
        std::vector<VkVertexInputAttributeDescription>& outAttributes);
    void CreatePipelineCache();
    void CreatePipelineLayout();
    void CreatePipeline();
    void CreatePipelineWithCache();

    VkCullModeFlags ParseCullMode(const std::string& mode);
    VkPolygonMode ParsePolygonMode(const std::string& mode);
    VkPrimitiveTopology ParseTopology(const std::string& topo);
    VkFrontFace ParseFrontFace(const std::string& face);

    // CashSystem integration - cached during Compile()
    CashSystem::PipelineCacher* pipelineCacher = nullptr;
    std::shared_ptr<CashSystem::PipelineWrapper> cachedPipelineWrapper;
};

} // namespace Vixen::RenderGraph
