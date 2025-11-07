#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "Data/Nodes/FramebufferNodeConfig.h"

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
    using Base = TypedNode<FramebufferNodeConfig>;

    FramebufferNode(
        const std::string& instanceName,
        NodeType* nodeType
    );

    ~FramebufferNode() override = default;

    // Access framebuffers for external use (legacy compatibility)
    const std::vector<VkFramebuffer>& GetFramebuffers() const { return framebuffers; }
    VkFramebuffer GetFramebuffer(uint32_t index) const {
        return (index < framebuffers.size()) ? framebuffers[index] : VK_NULL_HANDLE;
    }
    uint32_t GetFramebufferCount() const { return static_cast<uint32_t>(framebuffers.size()); }

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl(TypedSetupContext& ctx) override;
	void CompileImpl(TypedCompileContext& ctx) override;
	void ExecuteImpl(TypedExecuteContext& ctx) override;
	void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    bool hasDepth = false;
};

/**
 * @brief Type definition for FramebufferNode
 */
class FramebufferNodeType : public TypedNodeType<FramebufferNodeConfig> {
public:
    FramebufferNodeType(const std::string& typeName = "Framebuffer")
        : TypedNodeType<FramebufferNodeConfig>(typeName) {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

} // namespace Vixen::RenderGraph
