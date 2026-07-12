#pragma once
#include <cstddef>
#include <vector>

// View<->Model Binding Inc-C (View-Model-Binding-Inc-C-Plan-2026-07.md): the multi-instance
// selection seam. Mirrors IViewDataProvider.h's shape exactly (a small provider-swappable
// interface), but answers a different question: not "read/write noun X for entity E" but "which
// entities make up the currently committed selection, and what is the Nth one." A view/action
// bound "to selection index N" resolves through this seam to a concrete EntityID, then hands that
// entity to the EXISTING (unchanged) IViewDataProvider machinery to actually read/write a noun.
//
// NAMING (see View-Model-Binding-Inc-C-Plan-2026-07.md's "Naming disambiguation" section): this is
// NOT the same "selection" as SelectionCoordinatorNode/VoxelSelectionProviderNode/
// UISelectionProviderNode (libraries/RenderGraph/include/Selection/, Nodes/) -- that system answers
// "what did the user click on" (a packed pick-ID or RmlUi element-id hash, via GPU/hit-test render-
// graph nodes, deliberately with NO C++ provider interface per its own 2026-06-15 design). This
// seam's identity space is a Gaia EntityID, its trigger is a durable ECS query (`all<Selected>()`),
// and its consumer is View<->Model-Binding set-indexing/mutation -- named `IViewSelectionProvider`
// (not bare `ISelectionProvider`, which the framework design doc's §9 used before this plan
// deliberately renamed it) specifically so it is never confused with the render-graph pick system.
namespace Vixen::AppFlow {

// Opaque entity identity for the selection seam. Kept as a plain uint64_t (not gaia::ecs::Entity)
// so this header has zero Gaia dependency -- mirrors ViewNounKey::instance's own "provider-
// interpreted opaque handle" contract in IViewDataProvider.h. A Gaia-backed implementation converts
// to/from gaia::ecs::Entity at its own boundary (see GaiaViewSelectionProvider.h).
using SelectionEntityID = uint64_t;

struct IViewSelectionProvider {
    virtual ~IViewSelectionProvider() = default;

    // Fills `out` with the current committed selection set, in the provider's stable order
    // (append/creation order for the shipped implementations -- see each implementation's own
    // header for its exact ordering guarantee). Returns the count (== out.size() after the call).
    virtual size_t ids(std::vector<SelectionEntityID>& out) const = 0;

    // Resolves the Nth selected entity (0-based). Returns false if index is out of range
    // (fallible, mirroring IViewDataProvider::ReadU32's "false = absent" contract) -- `out` is
    // untouched on false so a caller can't mistake an out-of-range query for entity 0.
    virtual bool at(size_t index, SelectionEntityID& out) const = 0;
};

}  // namespace Vixen::AppFlow
