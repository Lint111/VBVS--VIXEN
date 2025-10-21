#pragma once
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include <memory>

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
    GraphicsPipelineNodeType();
    virtual ~GraphicsPipelineNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

/**
 * @brief Node instance for graphics pipeline creation
 * 
 * Parameters:
 * - enableDepthTest (bool): Enable depth testing (default: true)
 * - enableDepthWrite (bool): Enable depth writes (default: true)
 * - enableVertexInput (bool): Enable vertex input (default: true)
 * - cullMode (string): Cull mode - "None", "Front", "Back", "FrontAndBack" (default: "Back")
 * - polygonMode (string): Polygon mode - "Fill", "Line", "Point" (default: "Fill")
 * - topology (string): Primitive topology - "TriangleList", "TriangleStrip", etc. (default: "TriangleList")
 * - frontFace (string): Front face - "Clockwise", "CounterClockwise" (default: "CounterClockwise")
 * 
 * Inputs (opaque references):
 * - shaderStages: Shader modules from ShaderLibraryNode
 * - renderPass: Render pass from RenderPassNode
 * - vertexInputDescription: From VertexBufferNode
 * - descriptorSetLayout: From DescriptorSetNode
 * - viewport: Viewport configuration
 * - scissor: Scissor rectangle
 * 
 * Outputs:
 * - pipeline: Created graphics pipeline handle
 * - pipelineLayout: Pipeline layout with descriptor set layouts
 */
class GraphicsPipelineNode : public NodeInstance {
public:
    GraphicsPipelineNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );
    virtual ~GraphicsPipelineNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // Accessors for other nodes
    VkPipeline GetPipeline() const { return pipeline; }
    VkPipelineLayout GetPipelineLayout() const { return pipelineLayout; }
    VkPipelineCache GetPipelineCache() const { return pipelineCache; }

    // Set input references from other nodes
    void SetShaderStages(const VkPipelineShaderStageCreateInfo* stages, uint32_t count);
    void SetRenderPass(VkRenderPass renderPass);
    void SetVertexInput(const VkVertexInputBindingDescription* binding,
                        const VkVertexInputAttributeDescription* attributes,
                        uint32_t attributeCount);
    void SetDescriptorSetLayout(VkDescriptorSetLayout layout);
    void SetViewport(const VkViewport& viewport);
    void SetScissor(const VkRect2D& scissor);

private:
    // Pipeline resources
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;

    // Input references
    const VkPipelineShaderStageCreateInfo* shaderStages = nullptr;
    uint32_t shaderStageCount = 0;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    const VkVertexInputBindingDescription* vertexBinding = nullptr;
    const VkVertexInputAttributeDescription* vertexAttributes = nullptr;
    uint32_t vertexAttributeCount = 0;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkViewport viewport{};
    VkRect2D scissor{};

    // Configuration
    bool enableDepthTest = true;
    bool enableDepthWrite = true;
    bool enableVertexInput = true;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // Helper functions
    void CreatePipelineCache();
    void CreatePipelineLayout();
    void CreatePipeline();
    
    VkCullModeFlags ParseCullMode(const std::string& mode);
    VkPolygonMode ParsePolygonMode(const std::string& mode);
    VkPrimitiveTopology ParseTopology(const std::string& topo);
    VkFrontFace ParseFrontFace(const std::string& face);
};

} // namespace Vixen::RenderGraph
