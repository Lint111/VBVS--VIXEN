#pragma once
// View Contract Inc-2 Task 5 fix: keeps HudView.h (RmlUi's Generated/Hud.g.h -> RmlUi's bundled
// robin_hood.h) OUT of any translation unit that also sees gaia.h (BodyOctreeSceneNode.h's
// transitive include) -- gaia vendors its OWN, DIFFERENT-VERSION copy of robin_hood.h under the
// SAME include guard (ROBIN_HOOD_H_INCLUDED). Whichever copy a TU includes FIRST silently wins the
// header for the rest of that TU (the second #include is a no-op), so a TU that sees gaia's
// robin_hood.h before RmlUi's compiles RmlUi's inline data-model template code (RegisterStruct/
// RegisterDefinition, etc.) against the WRONG struct layout -- an ODR/ABI mismatch against the
// object RmlUi's own .cpp constructed, manifesting as a null-pointer access violation the first
// time that mismatched code touches the type registry. (Root-caused: gaia's copy is robin_hood
// v3.11.5, RmlUi's is v3.9.0 -- different Table<> layouts, 64 vs 56 bytes for the same
// instantiation, confirmed by measuring sizeof() in both TUs.)
//
// This bridge is the ONLY seam BuildRenderGraph.cpp/VulkanGraphApplication.cpp (both gaia-touching
// TUs, via BodyOctreeSceneNode.h) use to reach HudView -- they never #include "graph/HudView.h"
// themselves. HudViewBridge.cpp (gaia-free) is where HudView.h and Ui/IView.h/Nodes/UIRenderNode.h
// actually get included and HudView's inline methods actually instantiate.
#include "graph/HudFactionEventTypes.h"  // HudFactionIn/HudEventIn (RmlUi-free)
#include <span>

namespace Vixen::App { class HudView; }
namespace Vixen::RenderGraph { class UIRenderNode; }

namespace Vixen::App {

// Constructs a HudView. Returns a raw, owning pointer (not std::unique_ptr<HudView>) -- the owner
// (VulkanGraphApplication) holds it as a raw HudView* and destroys it via DestroyHudView(), never
// via ~unique_ptr(), because forward-declared-only HudView (see this header's file rationale) means
// std::unique_ptr<HudView>'s implicit destructor would need the complete type at ITS instantiation
// site (the owner's own destructor, in the gaia-touching VulkanGraphApplication.cpp) -- an
// incomplete-type-delete compile error that a raw pointer + explicit bridge-call sidesteps entirely.
HudView* MakeHudView();

// Destroys a HudView through a complete-type call site (HudViewBridge.cpp).
void DestroyHudView(HudView* view);

// Wires view onto node via UIRenderNode::SetView (non-owning aliased shared_ptr -- view is
// owned by the caller, e.g. VulkanGraphApplication::hudView_, which outlives the graph).
void WireHudView(Vixen::RenderGraph::UIRenderNode& node, HudView& view);

// Forwards to HudView::SetHudView (the relocated tick/bodyCount/lens/factions/events projection).
void PushHudView(HudView& view, int tick, int bodyCount, int activeLens, int activeLensCount,
                 std::span<const HudFactionIn> factions, std::span<const HudEventIn> events);

}  // namespace Vixen::App
