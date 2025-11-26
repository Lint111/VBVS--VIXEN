#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "Core/VulkanLimits.h"
#include "Data/Nodes/FramebufferNodeConfig.h"
#include "BoundedArray.h"

namespace Vixen::RenderGraph {

/**
 * @brief Framebuffer creation node (Phase H optimized)
 *
 * Responsibilities:
 * - Create framebuffers from render pass and attachments
 * - Handle multiple framebuffers (one per swapchain image)
 * - Support optional depth attachment
 * - Manage framebuffer lifecycle
 *
 * Phase H: Uses stack-allocated BoundedArray for framebuffers and attachments.
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

    // Access framebuffers for external use (Phase-H: returns BoundedArray reference)
    const ResourceManagement::BoundedArray<VkFramebuffer, MAX_SWAPCHAIN_IMAGES>& GetFramebuffers() const { return framebuffers_; }
    VkFramebuffer GetFramebuffer(uint32_t index) const {
        return (index < framebuffers_.Size()) ? framebuffers_[index] : VK_NULL_HANDLE;
    }
    uint32_t GetFramebufferCount() const { return static_cast<uint32_t>(framebuffers_.Size()); }

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Extracted compile helpers
    void ValidateInputs(VulkanDevice* devicePtr, VkRenderPass renderPass);
    
    // Phase H: Stack-allocated attachment array (max 2: color + depth)
    static constexpr size_t MAX_ATTACHMENTS = 2;
    ResourceManagement::BoundedArray<VkImageView, MAX_ATTACHMENTS> BuildAttachmentArray(
        VkImageView colorView, VkImageView depthView);
    
    VkFramebuffer CreateSingleFramebuffer(
        VkRenderPass renderPass,
        const ResourceManagement::BoundedArray<VkImageView, MAX_ATTACHMENTS>& attachments,
        const VkExtent2D& extent,
        uint32_t layers
    );
    void CleanupPartialFramebuffers(size_t count);

    VulkanDevice* vulkanDevice = nullptr;
    
    // Phase H: Stack-allocated framebuffer storage
    ResourceManagement::BoundedArray<VkFramebuffer, MAX_SWAPCHAIN_IMAGES> framebuffers_;
    
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
