#include "Nodes/UISelectionProviderNode.h"
#include "Core/NodeRegistration.h"
#include "Nodes/UIRenderNode.h"      // GetUiContext() — the Rml::Context to hit-test
#include "Core/NodeLogging.h"
#include "Selection/SelectionCandidate.h"
#include "Ui/UiHitMask.h"           // per-element hit-mask test (custom shapes / image masks)
#include "InputEvents.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Box.h>    // Rml::BoxArea, Element::GetBox().GetSize()
#include <RmlUi/Core/Types.h>  // Rml::Vector2f, Rml::String

#include <glm/glm.hpp>
#include <functional>  // std::hash
#include <string>

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> UISelectionProviderNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<UISelectionProviderNode>(
        instanceName, const_cast<UISelectionProviderNodeType*>(this));
}

// ============================================================================
// UI SELECTION PROVIDER NODE IMPLEMENTATION
// ============================================================================

UISelectionProviderNode::UISelectionProviderNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<UISelectionProviderNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("[UISelectionProvider] constructor");
}

void UISelectionProviderNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[UISelectionProvider] setup");
    // Provider layer priority (UI layer = 10 by default, > the voxel world's 0 so the HUD occludes
    // the world). Read once at graph-scope setup.
    priority_ = GetParameterValue<int>(UISelectionProviderNodeConfig::PARAM_PRIORITY, 10);
}

void UISelectionProviderNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // A provider emits a candidate EVERY frame so the coordinator's accumulation slot always has a
    // fresh value from this source. Default = miss; only a click-edge hit overwrites it.
    SelectionCandidate candidate{};
    candidate.hit      = false;
    candidate.id       = kInvalidSelectionId;
    candidate.depth    = 0.0f;
    candidate.priority = priority_;
    candidate.worldPos = glm::vec3(0.0f);

    InputStatePtr input = ctx.In(UISelectionProviderNodeConfig::INPUT_STATE);
    if (!input) {
        ctx.Out(UISelectionProviderNodeConfig::CANDIDATE, candidate);
        return;  // no input state this frame
    }

    // Find the left-button press entry to hit-test (input-rework slice 1 M3: the shared click list
    // replaces the old private lastLeftDown_ edge detector). If several presses land in one frame,
    // the LAST one wins — matches the old single-poll semantics (only the final down-state before
    // Execute was ever observable), just extended to not lose the intermediate ones' bus events.
    const ClickEvent* pressEntry = nullptr;
    for (const ClickEvent& click : input->clicksThisFrame) {
        if (click.button == static_cast<int>(EventBus::MouseButton::Left) && click.pressed) {
            pressEntry = &click;
        }
    }
    if (!pressEntry) {
        ctx.Out(UISelectionProviderNodeConfig::CANDIDATE, candidate);
        return;  // cheap: only hit-test on a click edge
    }

    // The Rml::Context is owned by the wired UIRenderNode (created in its CompileImpl). It is null
    // before the UI node compiles, or if this provider was never wired — treat either as a miss.
    Rml::Context* context = uiNode_ ? uiNode_->GetUiContext() : nullptr;
    if (!context) {
        NODE_LOG_INFO("[UISelectionProvider] click — no UI context (not wired / not yet compiled); miss");
        ctx.Out(UISelectionProviderNodeConfig::CANDIDATE, candidate);
        return;
    }

    // Hit-test the cursor position AT THE PRESS EVENT (not the end-of-frame cursor position) against
    // the live HUD. GetElementAtPoint does NOT reliably return nullptr over empty/transparent UI
    // space: every Context has a hidden "#root" pseudo-element (Context::SetDimensions sizes it to
    // the full viewport) that RCSS can never select — its pointer-events stays at the library
    // default (Auto) regardless of the body { pointer-events: none } rule below it, since
    // inheritance only flows parent->child and #root is body's PARENT. So any click that "misses"
    // every real, styled element still hits #root and comes back non-null. #root's own `id` is set
    // to the Context's name (Context::Context, "root->SetId(name)") — here "vixen_ui" (the name
    // passed to Rml::CreateContext in UIRenderNode.cpp) — which is why every click was logging
    // "HIT element id='vixen_ui'" even over visually-empty area, silently swallowing clicks that
    // should have passed through to the 3D voxel pick.
    const glm::vec2 cursor(pressEntry->x, pressEntry->y);
    Rml::Element* hitElement = context->GetElementAtPoint(
        Rml::Vector2f(cursor.x, cursor.y));

    if (!hitElement || hitElement == context->GetRootElement()) {
        NODE_LOG_INFO("[UISelectionProvider] click — miss (no UI element under cursor)");
        ctx.Out(UISelectionProviderNodeConfig::CANDIDATE, candidate);
        return;
    }

    // Per-element hit-mask: GetElementAtPoint is AABB-only (RmlUi already honours `pointer-events:
    // none`, so non-interactive HUD areas never reach here). For an interactive element with a
    // `hit-mask` attribute, run the mask test so a click on a transparent / non-shape pixel passes
    // THROUGH to the voxel pick instead of being swallowed. `none` / no attribute keeps the plain
    // box behaviour (always a hit).
    const Rml::String maskAttr = hitElement->GetAttribute("hit-mask", Rml::String("none"));
    const HitMaskSpec maskSpec = ParseHitMask(std::string(maskAttr));
    if (maskSpec.shape != HitMaskShape::Box) {
        // Map the cursor to element-local pixels (border box: matches GetElementAtPoint's geometry).
        const Rml::Vector2f origin = hitElement->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f size   = hitElement->GetBox().GetSize(Rml::BoxArea::Border);
        const float localX = cursor.x - origin.x;
        const float localY = cursor.y - origin.y;
        if (!HitMaskContains(maskSpec, localX, localY, size.x, size.y)) {
            NODE_LOG_INFO("[UISelectionProvider] click — hit-mask MISS on element id='" +
                          std::string(hitElement->GetId()) + "'; passing through to the world");
            ctx.Out(UISelectionProviderNodeConfig::CANDIDATE, candidate);  // candidate.hit == false
            return;
        }
    }

    // Hit. payload is a STABLE handle: a hash of the element's RML `id` attribute (NOT the raw
    // pointer, which is not stable across document reloads), so it maps back to a ui_binding later.
    const Rml::String elementId = hitElement->GetId();
    const uint64_t payload = static_cast<uint64_t>(std::hash<Rml::String>{}(elementId));

    // S4: also stash the human-readable id for the HOST to drain this frame (DrainClickedElementId).
    // The host forwards it into the feedback slice, where the matching ui_binding ('#id') fires. Set only
    // on this confirmed, mask-passing down-edge hit — so a fresh click is reported exactly once.
    clickedElementId_ = std::string(elementId);

    candidate.hit      = true;
    candidate.id       = SelectionId{ ProviderKind::Ui, payload };
    candidate.depth    = 0.0f;  // UI is a flat layer; priority (not depth) settles vs the world.
    candidate.worldPos = glm::vec3(0.0f);

    NODE_LOG_INFO("[UISelectionProvider] click — HIT element id='" +
                  (elementId.empty() ? std::string("<none>") : std::string(elementId)) +
                  "' payload=" + std::to_string(payload) +
                  " priority=" + std::to_string(priority_));

    ctx.Out(UISelectionProviderNodeConfig::CANDIDATE, candidate);
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::UISelectionProviderNodeType);
