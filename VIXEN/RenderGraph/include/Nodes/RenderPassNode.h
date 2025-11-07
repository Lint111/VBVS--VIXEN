#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "Data/Nodes/RenderPassNodeConfig.h"
#include <memory>

// Forward declarations
namespace CashSystem {
    struct RenderPassWrapper;
}

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

    ~RenderPassNode() override = default;

    // Access render pass for pipeline/framebuffer creation
    VkRenderPass GetRenderPass() const { return renderPass; }
    bool HasDepthAttachment() const { return hasDepth; }

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl(SetupContext& ctx) override;
	void CompileImpl(CompileContext& ctx) override;
	void ExecuteImpl(ExecuteContext& ctx) override;
	void CleanupImpl(CleanupContext& ctx) override;

private:
    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    bool hasDepth = false;

    // Cached wrapper from RenderPassCacher
    std::shared_ptr<CashSystem::RenderPassWrapper> cachedRenderPassWrapper;

    VkAttachmentLoadOp ConvertLoadOp(AttachmentLoadOp op);
    VkAttachmentStoreOp ConvertStoreOp(AttachmentStoreOp op);
    VkImageLayout ConvertImageLayout(ImageLayout layout);
    VkSampleCountFlagBits GetSampleCount(uint32_t samples);
};

/**
 * @brief Type definition for RenderPassNode
 */
class RenderPassNodeType : public TypedNodeType<RenderPassNodeConfig> {
public:
    RenderPassNodeType(const std::string& typeName = "RenderPass")
        : TypedNodeType<RenderPassNodeConfig>(typeName) {}

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

} // namespace Vixen::RenderGraph
