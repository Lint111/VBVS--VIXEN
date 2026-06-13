#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/UIRenderNodeConfig.h"
#include "Ui/VixenRmlRenderInterface.h"
#include "Ui/VixenRmlSystemInterface.h"

#include <RmlUi/Core/DataModelHandle.h>

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

    /// Host-facing seam: push the latest sim values; the bound HUD elements refresh on next Update().
    void SetHudData(int tick, int bodyCount);

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void FreeCommandBuffers();  // free the per-image command buffers (no device wait)
    void DestroyCompositeSemaphores();  // destroy the owned per-image "ui complete" semaphores
    void RecordFrame(VkCommandBuffer cmd, VkFramebuffer framebuffer);

    bool initialized_ = false;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;   // consumed from RenderPassNode (not owned)
    VkExtent2D extent_{};
    std::vector<VkCommandBuffer> commandBuffers_;  // one per swapchain image (owned)
    uint32_t syncImageCount_ = 0;  // image count the owned cmd buffers + composite semaphores were sized to

    // Composite mode (compositing over the voxel compute): layered over an upstream producer. The
    // node waits on the per-IMAGE compute→UI handoff and signals its own per-image semaphore (so the
    // present-wait semaphore is distinct from the handoff — a binary semaphore is one-signal/one-wait).
    bool composite_ = false;
    std::vector<VkSemaphore> uiCompleteSemaphores_;  // one per swapchain image (owned; composite only)

    Vixen::Ui::VixenRmlSystemInterface systemInterface_;
    Vixen::Ui::VixenRmlRenderInterface renderInterface_;
    Rml::Context* context_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;

    // S1: Rml data model for live sim data bound to the HUD document.
    struct HudData { int tick = 0; int bodyCount = 0; };
    HudData hud_{};
    Rml::DataModelHandle hudModel_;
};

} // namespace Vixen::RenderGraph
