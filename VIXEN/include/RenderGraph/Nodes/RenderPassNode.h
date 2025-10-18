#pragma once

#include "../NodeType.h"
#include "../NodeInstance.h"

namespace Vixen::RenderGraph {

/**
 * @brief Render pass definition node
 * 
 * Responsibilities:
 * - Define render pass with attachments (color, depth/stencil)
 * - Configure load/store operations
 * - Define subpass dependencies
 * - Handle layout transitions
 * 
 * Inputs: None
 * Outputs:
 *   [0] RenderPass handle (opaque, accessed via GetRenderPass())
 * 
 * Parameters:
 *   - colorFormat: VkFormat - Color attachment format
 *   - depthFormat: VkFormat - Depth attachment format (VK_FORMAT_UNDEFINED = no depth)
 *   - colorLoadOp: std::string - "Clear", "Load", "DontCare"
 *   - colorStoreOp: std::string - "Store", "DontCare"
 *   - depthLoadOp: std::string - "Clear", "Load", "DontCare"
 *   - depthStoreOp: std::string - "Store", "DontCare"
 *   - samples: uint32_t - MSAA sample count (1 = no MSAA)
 *   - initialLayout: std::string - "Undefined", "ColorAttachment", etc.
 *   - finalLayout: std::string - "PresentSrc", "ColorAttachment", etc.
 */
class RenderPassNode : public NodeInstance {
public:
    RenderPassNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
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
    VkRenderPass renderPass = VK_NULL_HANDLE;
    bool hasDepth = false;

    VkAttachmentLoadOp ParseLoadOp(const std::string& op);
    VkAttachmentStoreOp ParseStoreOp(const std::string& op);
    VkImageLayout ParseImageLayout(const std::string& layout);
    VkSampleCountFlagBits GetSampleCount(uint32_t samples);
};

/**
 * @brief Type definition for RenderPassNode
 */
class RenderPassNodeType : public NodeType {
public:
    RenderPassNodeType();

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

} // namespace Vixen::RenderGraph
