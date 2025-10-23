#pragma once

#include "Core/NodeType.h"
#include "Core/NodeInstance.h"

namespace Vixen::RenderGraph {

/**
 * @brief Simple geometry rendering pass node type
 * 
 * This is an example node that demonstrates:
 * - Basic rendering operation
 * - Single color attachment output
 * - Optional depth attachment output
 * - Graphics pipeline usage
 */
class GeometryPassNode : public NodeInstance {
public:
    GeometryPassNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );

    virtual ~GeometryPassNode() = default;

    // NodeInstance interface
    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

private:
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

/**
 * @brief Type definition for GeometryPassNode
 */
class GeometryPassNodeType : public NodeType {
public:
    GeometryPassNodeType(const std::string& typeName = "GeometryPass");

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

} // namespace Vixen::RenderGraph
