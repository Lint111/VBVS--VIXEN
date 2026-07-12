#pragma once
// View<->Model Binding Inc-C (View-Model-Binding-Inc-C-Plan-2026-07.md, Task 1/2): the committed-
// selection Gaia tag component. Design doc section 6 ("selection is data"): a runtime tag
// component, NOT a value field -- add/remove is a structural change (archetype move/chunk
// relocation), which is acceptable because committed selection is low-frequency/user-intentional
// (a click that commits, or here a scripted/test-side commit), unlike transient hover/highlight
// (explicitly out of scope for Inc-C -- see the plan's scope boundary -- which per design §6 must
// be a value field or UI-side state, never a structural tag, to avoid per-frame archetype churn).
//
// Placement: NOT VoxelComponents.h. `Selected` is a view/binding-layer concept (which entities are
// committed to the UI-visible selection set), not voxel data -- VoxelComponents.h's registry
// (VOXEL_COMPONENT_SCALAR/VOXEL_COMPONENT_VEC3 + the FOR_EACH_VALUE_COMPONENT/FOR_EACH_REF_COMPONENT
// macros) is specifically the voxel-attribute registry (density/color/material/...); folding a
// UI-selection tag into it would blur that boundary for no benefit (VoxelComponents.h's own macros
// don't have a zero-field/tag variant -- the only precedent is the hand-written `struct Solid {}`
// tag at the bottom of that file, which this component mirrors in shape but not in file placement).
//
// TU placement: this header includes gaia.h (transitively, wherever a Gaia entity/query touches
// Selected) but declares NO RmlUi type -- safe to include from a gaia-only TU (mirrors
// GaiaLayerViewDataProvider.h's own placement note).
namespace Vixen::App {

// Zero-field Gaia tag component: an entity either has it (selected) or doesn't (not selected).
// Empty struct = zero memory overhead per entity, matching VoxelComponents.h's `Solid` tag.
struct Selected {};

}  // namespace Vixen::App
