#pragma once
#include "RenderGraph/NodeInstance.h"
#include "RenderGraph/NodeType.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for recording geometry rendering commands
 * 
 * Records draw commands into command buffers including:
 * - Begin render pass
 * - Bind pipeline
 * - Bind descriptor sets
 * - Bind vertex/index buffers
 * - Set viewport and scissor
 * - Draw commands
 * - End render pass
 * 
 * Type ID: 109
 */
class GeometryRenderNodeType : public NodeType {
public:
    GeometryRenderNodeType();
    virtual ~GeometryRenderNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

/**
 * @brief Node instance for recording geometry render commands
 * 
 * Parameters:
 * - vertexCount (uint32_t): Number of vertices to draw
 * - instanceCount (uint32_t): Number of instances (default: 1)
 * - firstVertex (uint32_t): First vertex index (default: 0)
 * - firstInstance (uint32_t): First instance index (default: 0)
 * - useIndexBuffer (bool): Whether to use indexed rendering (default: false)
 * - indexCount (uint32_t): Number of indices (if using index buffer)
 * - clearColorR/G/B/A (float): Clear color values (default: 0,0,0,1)
 * - clearDepth (float): Clear depth value (default: 1.0)
 * - clearStencil (uint32_t): Clear stencil value (default: 0)
 * 
 * Inputs (via Set methods):
 * - renderPass: Render pass to use
 * - framebuffers: Framebuffers to render into
 * - pipeline: Graphics pipeline to bind
 * - pipelineLayout: Pipeline layout for descriptor sets
 * - descriptorSets: Descriptor sets to bind
 * - vertexBuffer: Vertex buffer to bind
 * - indexBuffer: Index buffer to bind (optional)
 * - viewport: Viewport to set
 * - scissor: Scissor rectangle to set
 */
class GeometryRenderNode : public NodeInstance {
public:
    GeometryRenderNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );
    virtual ~GeometryRenderNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // Set input references from other nodes
    void SetRenderPass(VkRenderPass renderPass);
    void SetFramebuffers(const std::vector<VkFramebuffer>& framebuffers);
    void SetPipeline(VkPipeline pipeline, VkPipelineLayout layout);
    void SetDescriptorSets(const std::vector<VkDescriptorSet>& sets);
    void SetVertexBuffer(VkBuffer buffer);
    void SetIndexBuffer(VkBuffer buffer);
    void SetViewport(const VkViewport& viewport);
    void SetScissor(const VkRect2D& scissor);
    void SetRenderArea(uint32_t width, uint32_t height);

    // Record draw commands for a specific framebuffer
    void RecordDrawCommands(VkCommandBuffer cmdBuffer, uint32_t framebufferIndex);

private:
    // Input references
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkViewport viewport{};
    VkRect2D scissor{};

    // Render area
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;

    // Draw parameters
    uint32_t vertexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstVertex = 0;
    uint32_t firstInstance = 0;
    bool useIndexBuffer = false;
    uint32_t indexCount = 0;

    // Clear values
    VkClearValue clearColor{};
    VkClearValue clearDepthStencil{};
};

} // namespace Vixen::RenderGraph
