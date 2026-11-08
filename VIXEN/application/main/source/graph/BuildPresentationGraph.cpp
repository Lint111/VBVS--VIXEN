#include "VulkanGraphApplication.h"

#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "Data/Nodes/FramebufferNodeConfig.h"
#include "Data/Nodes/RenderPassNodeConfig.h"
#include "Data/Nodes/UIRenderNodeConfig.h"
#include "Nodes/ComputeDispatchNode.h"
#include "Nodes/FramebufferNode.h"
#include "Nodes/RenderPassNode.h"
#include "Nodes/UIRenderNode.h"

void VulkanGraphApplication::BuildPresentationGraph(
    NodeHandle computeDispatch,
    bool skyProjectionFixtureEnabled,
    NodeHandle skyProjectionRenderPassNode,
    NodeHandle skyProjectionFramebufferNode,
    NodeHandle uiRenderPassNode,
    NodeHandle uiFramebufferNode,
    NodeHandle uiCompositeNode,
    bool usesWsi) {
    using namespace Vixen::RenderGraph;

    // --- Optional sky-projection fixture parameters ---
    // When enabled, LOAD the compute output and leave it in GENERAL for the UI composite pass.
    // The fixture does not transition to PresentSrc; the UI owns that as the final pass.
    if (skyProjectionFixtureEnabled) {
        auto* skyProjectionRenderPass = static_cast<RenderPassNode*>(renderGraph->GetInstance(skyProjectionRenderPassNode));
        skyProjectionRenderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::Load);
        skyProjectionRenderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::Store);
        skyProjectionRenderPass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::General);
        skyProjectionRenderPass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT, ImageLayout::General);
        skyProjectionRenderPass->SetParameter(RenderPassNodeConfig::PARAM_SAMPLES, 1u);

        auto* skyProjectionFramebuffer = static_cast<FramebufferNode*>(renderGraph->GetInstance(skyProjectionFramebufferNode));
        skyProjectionFramebuffer->SetParameter(FramebufferNodeConfig::PARAM_LAYERS, 1u);
    }

    // --- UI composite pass parameters ---
    // The chain is march -> DirectLighting -> BlitNode -> [optional sky fixture] -> UI.
    auto* dispatch = static_cast<ComputeDispatchNode*>(renderGraph->GetInstance(computeDispatch));
    dispatch->SetParameter(ComputeDispatchNodeConfig::PARAM_LEAVE_IMAGE_IN_GENERAL, true);
    dispatch->SetParameter(ComputeDispatchNodeConfig::PARAM_WRITES_NO_IMAGE, true);

    auto* uiRenderPass = static_cast<RenderPassNode*>(renderGraph->GetInstance(uiRenderPassNode));
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::Load);
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::Store);
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::General);
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT,
                               usesWsi ? ImageLayout::PresentSrc : ImageLayout::TransferSrc);
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_SAMPLES, 1u);

    auto* uiFramebuffer = static_cast<FramebufferNode*>(renderGraph->GetInstance(uiFramebufferNode));
    uiFramebuffer->SetParameter(FramebufferNodeConfig::PARAM_LAYERS, 1u);

    auto* uiComposite = static_cast<UIRenderNode*>(renderGraph->GetInstance(uiCompositeNode));
    uiComposite->SetParameter(UIRenderNodeConfig::PARAM_COMPOSITE, usesWsi);
    // R766: the app's declared panels are mounted by the panel dock; no hard-wired document.
    uiComposite->SetParameter(UIRenderNodeConfig::RML_DOCUMENT_PATH, std::string());
    {
        auto& dock = panelDock_->Dock();
        DeclarePanels(dock);
        if (const char* layoutPath = std::getenv("VIXEN_PANEL_LAYOUT"); layoutPath && *layoutPath) {
            panelDock_->SetLayoutPath(layoutPath);
        } else {
            panelDock_->SetLayoutPath(Vixen::UserSettingsDirectory() / "layouts" / (PanelLayoutName() + ".json"));
        }
        std::string layoutError;
        if (!panelDock_->LoadLayout(layoutError)) {
            mainLogger->Warning("[panels] ignoring saved layout '" + panelDock_->LayoutPath().string() + "': " + layoutError);
            dock.ResetToDefaults();
        }
        panelDock_->Attach(*uiComposite);
        std::string declared;
        for (const auto& decl : dock.Declarations()) declared += (declared.empty() ? "" : ",") + decl.id;
        mainLogger->Info("[panels] " + PanelLayoutName() + " mounted [" + declared + "] layout=" +
                         panelDock_->LayoutPath().string());
    }

    mainLogger->Info("Configured all node parameters (including camera, voxel grid, and UI composite pass)");
}
