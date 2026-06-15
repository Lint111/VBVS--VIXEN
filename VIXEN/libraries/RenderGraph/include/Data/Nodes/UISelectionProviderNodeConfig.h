#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/InputState.h"
#include "Selection/SelectionCandidate.h"

namespace Vixen::RenderGraph {

// Compile-time slot counts
namespace UISelectionProviderNodeCounts {
    static constexpr size_t INPUTS = 1;   // INPUT_STATE
    static constexpr size_t OUTPUTS = 1;  // CANDIDATE (SelectionCandidate)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for UISelectionProviderNode (SEL-P3 — the UI selection provider).
 *
 * The UI-domain selection PROVIDER, mirroring VoxelSelectionProviderNode but CPU-only. On a
 * left-click down-edge it reads the cursor position from INPUT_STATE and hit-tests the HUD's
 * Rml::Context (Context::GetElementAtPoint) — if an element is under the cursor it emits a
 * SelectionCandidate{ hit=true, id={ProviderKind::Ui, hash(element id)}, priority=<param> };
 * otherwise it emits {hit=false}. The SelectionCoordinatorNode gathers this candidate (and the
 * voxel provider's) through its MultiConnect PROVIDER_CANDIDATES accumulation slot and resolves
 * by MAX priority — so a default priority of 10 makes the HUD OCCLUDE the voxel world (priority 0).
 *
 * The Rml::Context is NOT a graph slot (it is a raw RmlUi pointer created inside UIRenderNode's
 * CompileImpl): the provider takes the UIRenderNode by reference (SetUiRenderNode, wired at graph
 * build time) and reads UIRenderNode::GetUiContext() at Execute. This keeps a non-registrable
 * pointer out of the resource graph while still letting the provider hit-test the live context.
 * Off the click edge — or with no context / no hit — it emits {hit=false} cheaply.
 *
 * Inputs (1):
 *   - INPUT_STATE (InputState*, Execute)   mouse buttons (left-click edge) + cursor position.
 *
 * Outputs (1):
 *   - CANDIDATE (SelectionCandidate)   this provider's per-click result (hit/miss + id/priority).
 *
 * Param:
 *   - PARAM_PRIORITY (int, default 10 = UI layer). Stamped on the candidate; the coordinator
 *     resolves by max priority, so 10 > the voxel world's 0 ⇒ a HUD click wins over the pick.
 */
CONSTEXPR_NODE_CONFIG(UISelectionProviderNodeConfig,
                      UISelectionProviderNodeCounts::INPUTS,
                      UISelectionProviderNodeCounts::OUTPUTS,
                      UISelectionProviderNodeCounts::ARRAY_MODE) {

    // ===== PARAMETER NAMES =====
    /// Provider layer priority (int). Higher occludes lower. Default 10 = UI layer (beats voxel 0).
    static constexpr const char* PARAM_PRIORITY = "priority";

    // ===== INPUTS (1) =====
    // Per-frame InputState (click edge + cursor position) — SlotRole::Execute + ReadOnly
    // (mirrors VoxelSelectionProviderNode's INPUT_STATE).
    INPUT_SLOT(INPUT_STATE, InputStatePtr, 0,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (1) =====
    // CANDIDATE is emitted every Execute (hit=false off the click edge) so the coordinator's
    // accumulation slot always has a fresh value from this source. Optional mirrors the voxel
    // provider's CANDIDATE; the coordinator treats an absent candidate as a miss.
    OUTPUT_SLOT(CANDIDATE, SelectionCandidate, 0,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Constructor for runtime descriptor initialization
    UISelectionProviderNodeConfig() {
        HandleDescriptor inputStateDesc{"InputState*"};
        INIT_INPUT_DESC(INPUT_STATE, "input_state", ResourceLifetime::Persistent, inputStateDesc);

        HandleDescriptor candidateDesc{"SelectionCandidate"};
        INIT_OUTPUT_DESC(CANDIDATE, "candidate", ResourceLifetime::Transient, candidateDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(UISelectionProviderNodeConfig, UISelectionProviderNodeCounts);

    // Slot index validations
    static_assert(INPUT_STATE_Slot::index == 0, "INPUT_STATE must be at index 0");
    static_assert(CANDIDATE_Slot::index == 0, "CANDIDATE must be at index 0");

    // Type validations
    static_assert(std::is_same_v<INPUT_STATE_Slot::Type, InputStatePtr>);
    static_assert(std::is_same_v<CANDIDATE_Slot::Type, SelectionCandidate>);
};

} // namespace Vixen::RenderGraph
