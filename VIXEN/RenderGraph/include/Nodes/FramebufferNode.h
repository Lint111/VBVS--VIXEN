#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "Core/VulkanLimits.h"
#include "Core/StackResourceHandle.h"
#include "Data/Nodes/FramebufferNodeConfig.h"
#include <array>
#include <optional>

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

    ~FramebufferNode() override = default;

    // Access framebuffers for external use
    VkFramebuffer GetFramebuffer(uint32_t index) const {
        return (framebuffers_.has_value() && index < framebufferCount) ? (*framebuffers_)[index] : VK_NULL_HANDLE;
    }
    uint32_t GetFramebufferCount() const { return framebufferCount; }

    // Phase H: Array-based access for zero-copy iteration (from URM handle)
    const VIXEN::StackResourceHandle<VkFramebuffer, MAX_SWAPCHAIN_IMAGES>* GetFramebufferHandle() const {
        return framebuffers_.has_value() ? &(*framebuffers_) : nullptr;
    }

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl(TypedSetupContext& ctx) override;
	void CompileImpl(TypedCompileContext& ctx) override;
	void ExecuteImpl(TypedExecuteContext& ctx) override;
	void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;

    // Phase H: URM-managed stack array (requested via hash-based identification)
    std::optional<VIXEN::StackResourceHandle<VkFramebuffer, MAX_SWAPCHAIN_IMAGES>> framebuffers_;
    uint32_t framebufferCount = 0;

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
