#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "FramebufferNodeConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Framebuffer creation node
 *
 * Responsibilities:
 * - Create framebuffers from render pass and attachments
 * - Handle multiple framebuffers (one per swapchain image)
 * - Support optional depth attachment
 * - Manage framebuffer lifecycle
 *
 * Uses TypedNode with FramebufferNodeConfig for compile-time type safety.
 *
 * Type ID: 105
 */
class FramebufferNode : public TypedNode<FramebufferNodeConfig> {
public:
    FramebufferNode(
        const std::string& instanceName,
        NodeType* nodeType
    );

    virtual ~FramebufferNode();

    // NodeInstance interface
    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;

    // Access framebuffers for external use (legacy compatibility)
    const std::vector<VkFramebuffer>& GetFramebuffers() const { return framebuffers; }
    VkFramebuffer GetFramebuffer(uint32_t index) const {
        return (index < framebuffers.size()) ? framebuffers[index] : VK_NULL_HANDLE;
    }
    uint32_t GetFramebufferCount() const { return static_cast<uint32_t>(framebuffers.size()); }

protected:
	void CleanupImpl() override;

private:
    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    bool hasDepth = false;
};

/**
 * @brief Type definition for FramebufferNode
 */
class FramebufferNodeType : public NodeType {
public:
    FramebufferNodeType(const std::string& typeName = "Framebuffer");

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

} // namespace Vixen::RenderGraph
