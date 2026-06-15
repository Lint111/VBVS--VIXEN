#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/UIRenderNodeConfig.h"
#include "Ui/VixenRmlRenderInterface.h"
#include "Ui/VixenRmlSystemInterface.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>  // Rml::String

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct SwapChainPublicVariables;  // global (VulkanSwapChain.h); full include in the .cpp
namespace Rml { class Context; class ElementDocument; }

namespace Vixen::RenderGraph {

/// Host-facing input types for SetHudView. The host passes plain C data; the node copies to
/// Rml::String internally so the caller does not need to care about RmlUi types.
struct HudFactionIn { const char* name; float grievance; };
struct HudEventIn   { const char* kind; int tick; };

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

    /// Host-facing seam (S1b): push tick, bodyCount, faction list and event list.
    /// The node copies name/kind strings to Rml::String and dirties all four bound vars.
    void SetHudView(int tick, int bodyCount,
                    std::span<const HudFactionIn> factions,
                    std::span<const HudEventIn> events);

    /// S1a compatibility shim — delegates to SetHudView with empty lists.
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

    // Live hot-reload (dev only; gated on VIXEN_UI_LIVE). Cache the resolved document path + the newest
    // mtime across the RML and its sibling RCSS so CompileImpl's recompile branch can detect an on-disk
    // edit and swap the document CPU-side (never touching the persistent GPU sync objects).
    std::string resolvedDocPath_;
    std::filesystem::file_time_type lastUiWriteTime_{};

    // S1b: Rml data model members. Structs are registered with RegisterStruct<> / RegisterArray<>
    // in CompileImpl before LoadDocument. tick_ / bodyCount_ are bound as scalars; factions_ /
    // events_ are bound as arrays (data-for in the HUD document).
    struct HudFaction { Rml::String name; float grievance = 0.f; };
    struct HudEvent   { Rml::String kind; int tick = 0; };

    int tick_ = 0;
    int bodyCount_ = 0;
    std::vector<HudFaction> factions_;
    std::vector<HudEvent>   events_;
    Rml::DataModelHandle    hudModel_;
};

} // namespace Vixen::RenderGraph
