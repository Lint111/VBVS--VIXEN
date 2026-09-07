#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/GPUPerformanceLogger.h"
#include "Data/Nodes/UIRenderNodeConfig.h"
#include "Ui/BlobView.h"
#include "Ui/IView.h"
#include "Ui/ViewBlobFile.h"
#include "Ui/VixenRmlRenderInterface.h"
#include "Ui/VixenRmlSystemInterface.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
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
class UIRenderNode : public TypedNode<UIRenderNodeConfig>, public IUiCompositionHost {
public:
    UIRenderNode(const std::string& instanceName, NodeType* nodeType);
    ~UIRenderNode() override = default;

    /// Renderer-agnostic view seam: the consumer hands in its view; the node hosts its data model
    /// (CreateDataModel(view->ModelName()) -> view->Register(c) -> LoadDocument(view->DocumentPath()))
    /// without knowing any field. Call before the first compile.
    void SetView(std::shared_ptr<IView> view);

    /// Poll the opt-in HUD .viewblob source and transactionally re-register its RmlUi model.
    /// Hosts call this once at the top of their update tick, before rendering begins.
    void PollHudHotReload();

    // --- IUiCompositionHost (relational-vertical-slice M-ui) ---
    // A second-document mount lifecycle over this node's one shared Rml::Context, beside the primary
    // (HUD) document_. Mounts are stored in mounts_ keyed by handle. See IView.h for the contract.
    MountHandle Mount(IView& view) override;
    void Unmount(MountHandle handle) override;
    void MarkMountedDirty(MountHandle handle, const char* field) override;
    [[nodiscard]] bool IsMounted(MountHandle handle) const override;

    /// Dirty a bound variable after the consumer mutated its storage (forwards to DataModelHandle).
    void MarkViewDirty(const char* field);

    /// Selection seam (additive): expose the owned Rml::Context so a selection provider
    /// (UISelectionProviderNode) can hit-test the HUD on a click. The context is created in
    /// CompileImpl and lives for this node's lifetime (RmlUi is reclaimed at process exit), so the
    /// pointer is stable after the first compile and null before it. READ-ONLY — the provider only
    /// calls Context::GetElementAtPoint; it never mutates the context, the GPU sync objects, the
    /// composite pass, or the live-reload state. Returns nullptr if the context failed to create.
    [[nodiscard]] Rml::Context* GetUiContext() const { return context_; }

    /// Get GPU performance logger for external metrics extraction (M5.1; mirrors ComputeDispatchNode).
    /// @return Pointer to GPUPerformanceLogger, or nullptr if not initialized.
    [[nodiscard]] GPUPerformanceLogger* GetGPUPerformanceLogger() const { return gpuPerfLogger_.get(); }

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void FreeCommandBuffers();  // free the per-image command buffers (no device wait)
    void DestroyCompositeSemaphores();  // destroy the owned per-image "ui complete" semaphores
    void RecordFrame(VkCommandBuffer cmd, VkFramebuffer framebuffer, uint32_t frameIndex);
    void ConfigureHudHotReload();
    bool RebindPrimaryView(const std::shared_ptr<IView>& candidate);
    bool RestorePrimaryView(const std::shared_ptr<IView>& previous);

    bool initialized_ = false;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    PFN_vkQueueSubmit2KHR fpQueueSubmit2_ = nullptr;  // cached from VulkanDevice each compile
    std::mutex* submitMutex_ = nullptr;  // VulkanDevice::SubmitMutex(queue_) — guards the per-frame submit (audit V-M11)
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

    // Renderer-agnostic view seam (Inc-2): the node hosts whatever IView the consumer sets, knowing
    // no field name. configuredView_ is the consumer's native/fallback view; view_ is the active view
    // registered into viewModel_. In the opt-in HUD blob path, view_ is a BlobView while configuredView_
    // remains available for a failed parse or failed transactional rebind.
    std::shared_ptr<IView> configuredView_;
    std::shared_ptr<IView>  view_;
    Rml::DataModelHandle    viewModel_;

    // T1.0: runtime HUD view-blob reload state. The file and BlobView are shared separately because
    // BlobView stores a reference to ViewBlob's backing storage; both must survive the registered model.
    bool hudHotReloadEnabled_ = false;
    uint32_t hotReloadPollTicks_ = 0;
    uint32_t registeredViewVersion_ = 0;
    bool hotReloadHasWriteTime_ = false;
    std::string hotReloadBlobPath_;
    std::filesystem::file_time_type hotReloadLastWriteTime_{};
    std::shared_ptr<ViewBlobFile> hotReloadBlobFile_;
    std::shared_ptr<BlobView> hotReloadView_;

    // --- IUiCompositionHost mounts (M-ui) ---
    // A live mounted fragment: its second document + its own isolated data model, in the shared
    // context_. Keyed by an increasing MountHandle. Kept beside document_/viewModel_ (the primary
    // HUD), never replacing them — the HUD document is byte-untouched when nothing is mounted.
    struct Mount_ {
        IView*               view = nullptr;   // borrowed (owned by the caller, like the HUD view)
        Rml::ElementDocument* doc = nullptr;
        Rml::DataModelHandle  model;
    };
    std::map<MountHandle, Mount_> mounts_;
    MountHandle nextMountHandle_ = 1;   // 0 is the invalid sentinel
    // Mount() can be called before the first CompileImpl (context_ still null); such requests park
    // here and are realized on the first frame the context exists (RealizePendingMounts in RecordFrame).
    std::vector<std::pair<MountHandle, IView*>> pendingMounts_;
    // Do the actual RmlUi CreateDataModel + LoadDocument + validation for one view; returns false
    // (leaving no partial state) on a namespace/degenerate-layout failure. Requires context_ != null.
    bool MountNow(MountHandle handle, IView& view);
    void RealizePendingMounts();   // drain pendingMounts_ once context_ exists

    // GPU timing (M5.1, mirrors ComputeDispatchNode's gpuPerfLogger_): times the render-pass
    // recording (BeginRenderPass..EndRenderPass) so a p99 hitch can be attributed to the UI pass
    // vs. the compute dispatch vs. neither (CPU/present).
    std::shared_ptr<GPUPerformanceLogger> gpuPerfLogger_;
};

} // namespace Vixen::RenderGraph
