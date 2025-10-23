#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "DepthBufferNodeConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Depth buffer creation node
 *
 * Responsibilities:
 * - Create depth/stencil image
 * - Allocate device memory
 * - Create image view
 * - Transition to depth-stencil attachment optimal layout
 *
 * Uses TypedNode with DepthBufferNodeConfig for compile-time type safety.
 *
 * Type ID: 101
 */
class DepthBufferNode : public TypedNode<DepthBufferNodeConfig> {
public:
    DepthBufferNode(
        const std::string& instanceName,
        NodeType* nodeType
    );

    virtual ~DepthBufferNode();

    // NodeInstance interface
    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // Accessor for depth image view
    VkImageView GetDepthImageView() const { return depthImage.view; }

private:
    struct DepthImage {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    } depthImage;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    bool isCreated = false;

    VkFormat ConvertDepthFormat(DepthFormat format);
    void CreateDepthImageAndView(uint32_t width, uint32_t height, VkFormat format);
    void TransitionImageLayout(VkCommandBuffer cmdBuffer, VkImage image,
                               VkImageLayout oldLayout, VkImageLayout newLayout);
};

/**
 * @brief Type definition for DepthBufferNode
 */
class DepthBufferNodeType : public NodeType {
public:
    DepthBufferNodeType();

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

} // namespace Vixen::RenderGraph
