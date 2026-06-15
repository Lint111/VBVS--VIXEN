#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/UISelectionProviderNodeConfig.h"
#include <memory>
#include <string>

namespace Vixen::RenderGraph {

class UIRenderNode;  // provides the Rml::Context to hit-test (set at wiring via SetUiRenderNode)

/**
 * @brief Node type for the UI-domain selection provider (SEL-P3).
 */
class UISelectionProviderNodeType : public TypedNodeType<UISelectionProviderNodeConfig> {
public:
    UISelectionProviderNodeType(const std::string& typeName = "UISelectionProvider")
        : TypedNodeType<UISelectionProviderNodeConfig>(typeName) {}
    ~UISelectionProviderNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief UI-domain selection provider — a graph NODE (SEL-P3), mirroring VoxelSelectionProviderNode.
 *
 * On a left-click down-edge it hit-tests the HUD's Rml::Context at the cursor position and emits a
 * SelectionCandidate on its CANDIDATE output; the SelectionCoordinatorNode gathers it (plus the
 * voxel provider's candidate) through a MultiConnect accumulation slot and priority-resolves. With
 * the default PARAM_PRIORITY of 10 (> the voxel world's 0) a HUD element OCCLUDES the world, so a
 * click on the UI wins over the voxel pick.
 *
 * Unlike the voxel provider this is CPU-only — no device, command pool, or GPU readback. The
 * Rml::Context is not a graph slot (it is a raw RmlUi pointer owned by UIRenderNode): the provider
 * holds the UIRenderNode by reference (SetUiRenderNode, wired at graph build) and reads
 * UIRenderNode::GetUiContext() at Execute. It emits a candidate EVERY Execute (hit=false off the
 * click edge / on a miss / with no context), so the coordinator's accumulation slot always has a
 * fresh value from this source. Off the click edge the per-frame cost is an edge comparison and one
 * slot write.
 *
 * The hit payload is a STABLE handle for the hit element: a hash of its RML `id` attribute
 * (Element::GetId()), so the selection maps back to a ui_binding later — NOT the raw element
 * pointer (which is not stable across reloads).
 */
class UISelectionProviderNode : public TypedNode<UISelectionProviderNodeConfig> {
public:
    UISelectionProviderNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~UISelectionProviderNode() override = default;

    /// Wire the UIRenderNode whose Rml::Context this provider hit-tests. Called once at graph build
    /// (the context is created later in UIRenderNode::CompileImpl; the provider reads it at Execute,
    /// tolerating a null context until then). Not a graph slot — see the class doc.
    void SetUiRenderNode(UIRenderNode* uiNode) { uiNode_ = uiNode; }

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;

private:
    // The UIRenderNode owning the Rml::Context to hit-test (set at wiring; null until then).
    UIRenderNode* uiNode_ = nullptr;

    // ----- Provider config -----
    int priority_ = 10;  ///< Layer priority (PARAM_PRIORITY) stamped on every candidate (UI > world).

    // Edge detection for the left mouse button (resolve on the down-edge only).
    bool lastLeftDown_ = false;
};

} // namespace Vixen::RenderGraph
