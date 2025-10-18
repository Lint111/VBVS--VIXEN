#pragma once
#include "RenderGraph/NodeInstance.h"
#include "RenderGraph/NodeType.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for creating framebuffers from render pass and attachments
 * 
 * Combines a render pass with color and depth attachments to create framebuffers.
 * Typically creates one framebuffer per swapchain image for rendering.
 * 
 * Type ID: 105
 */
class FramebufferNodeType : public NodeType {
public:
    FramebufferNodeType();
    virtual ~FramebufferNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

/**
 * @brief Node instance for framebuffer creation
 * 
 * Parameters:
 * - width (uint32_t): Framebuffer width
 * - height (uint32_t): Framebuffer height
 * - layers (uint32_t): Number of layers (default: 1)
 * - includeDepth (bool): Whether to include depth attachment (default: true)
 * - framebufferCount (uint32_t): Number of framebuffers to create (default: 1)
 * 
 * Inputs (via Set methods):
 * - renderPass: Render pass from RenderPassNode
 * - colorAttachments: Array of color image views
 * - depthAttachment: Depth image view (optional)
 * 
 * Outputs:
 * - framebuffers: Created framebuffer handles
 */
class FramebufferNode : public NodeInstance {
public:
    FramebufferNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );
    virtual ~FramebufferNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // Accessors for other nodes
    const std::vector<VkFramebuffer>& GetFramebuffers() const { return framebuffers; }
    VkFramebuffer GetFramebuffer(uint32_t index) const {
        return (index < framebuffers.size()) ? framebuffers[index] : VK_NULL_HANDLE;
    }
    uint32_t GetFramebufferCount() const { return static_cast<uint32_t>(framebuffers.size()); }

    // Set input references from other nodes
    void SetRenderPass(VkRenderPass renderPass);
    void SetColorAttachments(const std::vector<VkImageView>& colorViews);
    void SetDepthAttachment(VkImageView depthView);

private:
    // Framebuffer resources
    std::vector<VkFramebuffer> framebuffers;

    // Input references
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkImageView> colorAttachments;
    VkImageView depthAttachment = VK_NULL_HANDLE;

    // Configuration
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 1;
    bool includeDepth = true;
    uint32_t framebufferCount = 1;

    // Helper functions
    void CreateFramebuffers();
};

} // namespace Vixen::RenderGraph
