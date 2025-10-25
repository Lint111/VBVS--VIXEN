#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "RenderPassNodeConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Render pass definition node
 *
 * Responsibilities:
 * - Define render pass with attachments (color, depth/stencil)
 * - Configure load/store operations using typed enums
 * - Define subpass dependencies
 * - Handle layout transitions
 *
 * Uses TypedNode with RenderPassNodeConfig for compile-time type safety.
 *
 * Type ID: 104
 */
class RenderPassNode : public TypedNode<RenderPassNodeConfig> {
public:
    RenderPassNode(
        const std::string& instanceName,
        NodeType* nodeType
    );

    virtual ~RenderPassNode();

    // NodeInstance interface
    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // Access render pass for pipeline/framebuffer creation
    VkRenderPass GetRenderPass() const { return renderPass; }
    bool HasDepthAttachment() const { return hasDepth; }

private:
    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    bool hasDepth = false;

    VkAttachmentLoadOp ConvertLoadOp(AttachmentLoadOp op);
    VkAttachmentStoreOp ConvertStoreOp(AttachmentStoreOp op);
    VkImageLayout ConvertImageLayout(ImageLayout layout);
    VkSampleCountFlagBits GetSampleCount(uint32_t samples);
};

/**
 * @brief Type definition for RenderPassNode
 */
class RenderPassNodeType : public NodeType {
public:
    RenderPassNodeType(const std::string& typeName = "RenderPass");

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

} // namespace Vixen::RenderGraph
