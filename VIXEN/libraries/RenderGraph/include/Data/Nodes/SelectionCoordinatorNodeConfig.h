#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/InputState.h"
#include "Selection/SelectionCandidate.h"

namespace Vixen::RenderGraph {

// Compile-time slot counts
namespace SelectionCoordinatorNodeCounts {
    static constexpr size_t INPUTS = 2;   // INPUT_STATE, PROVIDER_CANDIDATE
    static constexpr size_t OUTPUTS = 1;  // SELECTION_COUNT (status)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for SelectionCoordinatorNode (SEL-P2 — providers are nodes).
 *
 * The engine-wide selection coordinator. It no longer owns providers or performs any
 * GPU readback: provider NODES (VoxelSelectionProviderNode, a future UiSelectionProvider
 * node, …) each resolve their own domain on a click and emit a SelectionCandidate. The
 * coordinator consumes a provider candidate (PROVIDER_CANDIDATE) and, on the left-click
 * down-edge it edge-detects from INPUT_STATE:
 *   1. keeps the candidate(s) that hit;
 *   2. picks the best — MAX priority, tie-break MIN depth (UI occludes world); the
 *      resolution (pickBestCandidate) already takes a vector and is ready for N candidates;
 *   3. maps the input modifier keys to a SelectionModifier (Shift→Add, Ctrl→Toggle, else
 *      Replace) and applies it to its owned SelectionSet (Replace-on-miss clears);
 *   4. broadcasts a SelectionChangedEvent (snapshot of the set) on the message bus.
 *
 * The SelectionSet stays the single source of truth; consumers (highlight, UI, gameplay)
 * subscribe to the event. Per-frame cost off the click edge is a couple of reads + an edge
 * comparison.
 *
 * FAN-IN NOTE (the candidate slot): the design calls for a MultiConnect/Accumulation slot
 * so MANY provider nodes can feed ONE coordinator. The RenderGraph engine, however, has no
 * runtime accumulation-gather: AccumulationConnectionRule only records the source list + an
 * ordering edge and never assembles the std::vector<T> onto the consumer input (its value is
 * always read empty at Execute — std::any-based Resource storage makes a type-erased gather
 * infeasible without a per-element-type registry; that is a separate engine feature). So this
 * slot is a single-source Execute input today (DirectConnectionRule, which DOES wire value
 * structs each frame — same path CameraData uses). The coordinator still resolves through a
 * vector (pickBestCandidate), so the day the engine grows an accumulation-gather (or a small
 * candidate-merge node lands), this flips to the vector slot with NO resolution-logic change.
 *
 * Inputs (2):
 *   - INPUT_STATE        (InputState*, Execute)        mouse buttons + modifier keys
 *   - PROVIDER_CANDIDATE (SelectionCandidate, Execute) a provider node's per-click candidate.
 *
 * Outputs (1):
 *   - SELECTION_COUNT (uint32_t)   size of the current SelectionSet after the last pick.
 *
 * NOTE on output count: the graph's topological sort visits ALL added nodes, so a pure
 * 0-output sink WOULD still execute, and VALIDATE_NODE_CONFIG permits OUTPUTS == 0. We
 * nonetheless expose SELECTION_COUNT so the selection size is a first-class graph value a
 * future node (e.g. a highlight pass) can consume directly. The authoritative selection
 * still flows via SelectionChangedEvent on the bus (the coordinator owns the set).
 */
CONSTEXPR_NODE_CONFIG(SelectionCoordinatorNodeConfig,
                      SelectionCoordinatorNodeCounts::INPUTS,
                      SelectionCoordinatorNodeCounts::OUTPUTS,
                      SelectionCoordinatorNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (2) =====
    // Per-frame InputState (click edge + modifier keys) — SlotRole::Execute + ReadOnly
    // (mirrors how CameraNode declares its Execute inputs).
    INPUT_SLOT(INPUT_STATE, InputStatePtr, 0,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // A provider node's per-frame SelectionCandidate (Execute role — read each frame, like the
    // provider emits it each frame). Optional so the coordinator still runs before a provider is
    // wired. See the FAN-IN NOTE above for why this is single-source (engine accumulation gap).
    INPUT_SLOT(PROVIDER_CANDIDATE, SelectionCandidate, 1,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (1) =====
    OUTPUT_SLOT(SELECTION_COUNT, uint32_t, 0,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Constructor for runtime descriptor initialization
    SelectionCoordinatorNodeConfig() {
        HandleDescriptor inputStateDesc{"InputState*"};
        INIT_INPUT_DESC(INPUT_STATE, "input_state", ResourceLifetime::Persistent, inputStateDesc);

        HandleDescriptor candidateDesc{"SelectionCandidate"};
        INIT_INPUT_DESC(PROVIDER_CANDIDATE, "provider_candidate", ResourceLifetime::Transient, candidateDesc);

        HandleDescriptor selectionCountDesc{"uint32_t"};
        INIT_OUTPUT_DESC(SELECTION_COUNT, "selection_count", ResourceLifetime::Transient, selectionCountDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(SelectionCoordinatorNodeConfig, SelectionCoordinatorNodeCounts);

    // Slot index validations
    static_assert(INPUT_STATE_Slot::index == 0, "INPUT_STATE must be at index 0");
    static_assert(PROVIDER_CANDIDATE_Slot::index == 1, "PROVIDER_CANDIDATE must be at index 1");
    static_assert(SELECTION_COUNT_Slot::index == 0, "SELECTION_COUNT must be at index 0");

    // Type validations
    static_assert(std::is_same_v<INPUT_STATE_Slot::Type, InputStatePtr>);
    static_assert(std::is_same_v<PROVIDER_CANDIDATE_Slot::Type, SelectionCandidate>);
    static_assert(std::is_same_v<SELECTION_COUNT_Slot::Type, uint32_t>);
};

} // namespace Vixen::RenderGraph
