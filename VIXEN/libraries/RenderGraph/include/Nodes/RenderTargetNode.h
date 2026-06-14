#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/RenderTargetNodeConfig.h"
#include "IRenderTarget.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for offscreen color render target allocation
 * Type ID: 115
 */
class RenderTargetNodeType : public TypedNodeType<RenderTargetNodeConfig> {
public:
    RenderTargetNodeType(const std::string& typeName = "RenderTarget")
        : TypedNodeType<RenderTargetNodeConfig>(typeName) {}
    virtual ~RenderTargetNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Allocates an offscreen color render target (color-only; compose with DepthBufferNode +
 * FramebufferNode like the swapchain path). Outputs an IRenderTarget* (its RenderTargetData).
 *
 * FR-7 lifecycle: images persist across graph recompile; only released on FinalTeardown.
 *
 * Note: followSwapchainExtent / resize is deliberately out of scope for this first iteration.
 * Use explicit width / height parameters (PARAM_WIDTH, PARAM_HEIGHT). See AR#28 follow-ups.
 */
class RenderTargetNode : public TypedNode<RenderTargetNodeConfig> {
public:
    using Base = TypedNode<RenderTargetNodeConfig>;

    RenderTargetNode(const std::string& instanceName, NodeType* nodeType);
    ~RenderTargetNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void CreateTarget(Vixen::Vulkan::Resources::VulkanDevice* device);
    void DestroyTarget();

    Vixen::Vulkan::Resources::RenderTargetData target_;
    Vixen::Vulkan::Resources::VulkanDevice*    device_     = nullptr;  // cached for cleanup
    uint32_t           width_      = 0;
    uint32_t           height_     = 0;
    uint32_t           imageCount_ = 0;
    VkFormat           format_     = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags  usage_      = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
};

} // namespace Vixen::RenderGraph
