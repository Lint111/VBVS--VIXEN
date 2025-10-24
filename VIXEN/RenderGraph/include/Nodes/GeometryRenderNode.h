#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "GeometryRenderNodeConfig.h"
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
    GeometryRenderNodeType(const std::string& typeName = "GeometryRender");
    virtual ~GeometryRenderNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Node instance for recording geometry render commands
 * 
 * Now uses TypedNode<GeometryRenderNodeConfig> for compile-time type safety.
 * All inputs/outputs are accessed via the typed config slot API.
 * 
 * See GeometryRenderNodeConfig.h for slot definitions and parameters.
 */
class GeometryRenderNode : public TypedNode<GeometryRenderNodeConfig> {
public:
    GeometryRenderNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    virtual ~GeometryRenderNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // Record draw commands for a specific framebuffer
    void RecordDrawCommands(VkCommandBuffer cmdBuffer, uint32_t framebufferIndex);

private:
    // Draw parameters (from node parameters)
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
