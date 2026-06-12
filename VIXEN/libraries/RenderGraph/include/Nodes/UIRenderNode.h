#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/UIRenderNodeConfig.h"
#include "Ui/VixenRmlRenderInterface.h"
#include "Ui/VixenRmlSystemInterface.h"

#include <memory>
#include <string>
#include <vector>

struct SwapChainPublicVariables;  // global (VulkanSwapChain.h); full include in the .cpp
namespace Rml { class Context; class ElementDocument; }

namespace Vixen::RenderGraph {

/**
 * @brief Node type for rendering an RmlUi document (data-driven UI) into the swapchain.
 */
class UIRenderNodeType : public TypedNodeType<UIRenderNodeConfig> {
public:
    UIRenderNodeType(const std::string& typeName = "UIRender")
        : TypedNodeType<UIRenderNodeConfig>(typeName) {}
    ~UIRenderNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Node instance that owns an Rml::Context and renders it through VixenRmlRenderInterface.
 *
 * Mirrors GeometryRenderNode: consumes RENDER_PASS (from RenderPassNode) + FRAMEBUFFERS (from
 * FramebufferNode) built off the swapchain, and re-records every frame (RmlUi replays draws via
 * Render()). The swapchain-derived resources are owned + recreated-on-resize by those nodes, not
 * here; this node owns only its one-time RmlUi pipeline/context/document + per-image command buffers.
 */
class UIRenderNode : public TypedNode<UIRenderNodeConfig> {
public:
    UIRenderNode(const std::string& instanceName, NodeType* nodeType);
    ~UIRenderNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void FreeCommandBuffers();  // free the per-image command buffers (no device wait)
    void RecordFrame(VkCommandBuffer cmd, VkFramebuffer framebuffer);

    bool initialized_ = false;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;   // consumed from RenderPassNode (not owned)
    VkExtent2D extent_{};
    std::vector<VkCommandBuffer> commandBuffers_;  // one per swapchain image (owned)

    Vixen::Ui::VixenRmlSystemInterface systemInterface_;
    Vixen::Ui::VixenRmlRenderInterface renderInterface_;
    Rml::Context* context_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
};

} // namespace Vixen::RenderGraph
