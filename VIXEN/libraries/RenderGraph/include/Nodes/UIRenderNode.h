#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/GPUPerformanceLogger.h"
#include "Data/Nodes/UIRenderNodeConfig.h"
#include "Ui/IView.h"
#include "Ui/VixenRmlRenderInterface.h"
#include "Ui/VixenRmlSystemInterface.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <filesystem>
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
class UIRenderNode : public TypedNode<UIRenderNodeConfig> {
public:
    UIRenderNode(const std::string& instanceName, NodeType* nodeType);
    ~UIRenderNode() override = default;

    /// Renderer-agnostic view seam: the consumer hands in its view; the node hosts its data model
    /// (CreateDataModel(view->ModelName()) -> view->Register(c) -> LoadDocument(view->DocumentPath()))
    /// without knowing any field. Call before the first compile.
    void SetView(std::shared_ptr<IView> view);

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
    // no field name. view_ is created into viewModel_ in CompileImpl (CreateDataModel(view_->ModelName())
    // -> view_->Register(c)); MarkViewDirty forwards to viewModel_.DirtyVariable.
    std::shared_ptr<IView>  view_;
    Rml::DataModelHandle    viewModel_;

    // GPU timing (M5.1, mirrors ComputeDispatchNode's gpuPerfLogger_): times the render-pass
    // recording (BeginRenderPass..EndRenderPass) so a p99 hitch can be attributed to the UI pass
    // vs. the compute dispatch vs. neither (CPU/present).
    std::shared_ptr<GPUPerformanceLogger> gpuPerfLogger_;
};

} // namespace Vixen::RenderGraph
