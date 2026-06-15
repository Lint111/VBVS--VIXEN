#include "Nodes/UISelectionProviderNode.h"
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
    priority_     = GetParameterValue<int>(UISelectionProviderNodeConfig::PARAM_PRIORITY, 10);
    lastLeftDown_ = false;
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

    // Edge-detect the left-button press: hit-test only on the down-edge (no per-frame hit-test).
    const bool leftDown = input->mouseButtons[0];
    const bool pressedThisFrame = leftDown && !lastLeftDown_;
    lastLeftDown_ = leftDown;

    if (!pressedThisFrame) {
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

    // Hit-test the cursor position against the live HUD. GetElementAtPoint returns the topmost
    // element under the point, or nullptr on a miss (cursor over empty/transparent UI area).
    const glm::vec2 cursor = input->mousePosition;
    Rml::Element* hitElement = context->GetElementAtPoint(
        Rml::Vector2f(cursor.x, cursor.y));

    if (!hitElement) {
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
