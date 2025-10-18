#pragma once

#include "../NodeType.h"
#include "../NodeInstance.h"

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
 * Inputs: None
 * Outputs:
 *   [0] Depth image (VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
 * 
 * Parameters:
 *   - width: uint32_t - Depth buffer width
 *   - height: uint32_t - Depth buffer height
 *   - format: std::string - "D32" (default), "D24S8", "D16"
 */
class DepthBufferNode : public NodeInstance {
public:
    DepthBufferNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
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

    VkFormat GetFormatFromString(const std::string& formatStr);
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
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

} // namespace Vixen::RenderGraph
