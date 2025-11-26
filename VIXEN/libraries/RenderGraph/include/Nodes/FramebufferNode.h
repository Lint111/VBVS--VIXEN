#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "Core/VulkanLimits.h"
#include "Core/ResourceManagerBase.h"
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
 * Phase H: Uses RequestAllocation API for automatic resource tracking.
 * Storage is via AllocationResult which handles stack/heap fallback.
 *
 * Uses TypedNode with FramebufferNodeConfig for compile-time type safety.
 *
 * Type ID: 105
 */
class FramebufferNode : public TypedNode<FramebufferNodeConfig> {
public:
    // Type aliases for cleaner code
    using FramebufferAllocation = AllocationResult<VkFramebuffer, MAX_SWAPCHAIN_IMAGES>;

    FramebufferNode(
        const std::string& instanceName,
        NodeType* nodeType
    );

    ~FramebufferNode() override = default;

    // Access framebuffers for external use
    // Returns reference to underlying BoundedArray (throws if heap-allocated)
    const ResourceManagement::BoundedArray<VkFramebuffer, MAX_SWAPCHAIN_IMAGES>& GetFramebuffers() const {
        if (!framebuffers_.IsStack()) {
            throw std::runtime_error("FramebufferNode: Cannot get BoundedArray reference from heap allocation");
        }
        return framebuffers_.GetStack().data;
    }
    VkFramebuffer GetFramebuffer(uint32_t index) const {
        return (index < framebuffers_.Size()) ? framebuffers_.Data()[index] : VK_NULL_HANDLE;
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

    // Phase H: Resource allocation result (handles stack/heap with automatic tracking)
    FramebufferAllocation framebuffers_;

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
