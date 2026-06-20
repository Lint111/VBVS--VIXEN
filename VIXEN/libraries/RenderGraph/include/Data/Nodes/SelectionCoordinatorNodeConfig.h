#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/InputState.h"
#include "Selection/SelectionCandidate.h"

#include <vector>

namespace Vixen::RenderGraph {

// Compile-time slot counts
namespace SelectionCoordinatorNodeCounts {
    static constexpr size_t INPUTS = 2;   // INPUT_STATE, PROVIDER_CANDIDATES (accumulation gather)
    static constexpr size_t OUTPUTS = 1;  // SELECTION_COUNT (status)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for SelectionCoordinatorNode (SEL-P2 — providers are nodes).
 *
 * The engine-wide selection coordinator. It no longer owns providers or performs any
 * GPU readback: provider NODES (VoxelSelectionProviderNode, UISelectionProviderNode, …)
 * each resolve their own domain on a click and emit a SelectionCandidate. The coordinator
 * GATHERS all wired providers' candidates (PROVIDER_CANDIDATES) and, on the left-click
 * down-edge it edge-detects from INPUT_STATE:
 *   1. keeps the candidate(s) that hit;
 *   2. picks the best — MAX priority, tie-break MIN depth (UI occludes world) — via
 *      pickBestCandidate over the gathered vector;
 *   3. maps the input modifier keys to a SelectionModifier (Shift→Add, Ctrl→Toggle, else
 *      Replace) and applies it to its owned SelectionSet (Replace-on-miss clears);
 *   4. broadcasts a SelectionChangedEvent (snapshot of the set) on the message bus.
 *
 * The SelectionSet stays the single source of truth; consumers (highlight, UI, gameplay)
 * subscribe to the event. Per-frame cost off the click edge is a couple of reads + an edge
 * comparison.
 *
 * FAN-IN (the candidate slot): PROVIDER_CANDIDATES is an ACCUMULATION gather slot
 * (ACCUMULATION_INPUT_SLOT_V2, element type SelectionCandidate). MANY provider nodes
 * MultiConnect their CANDIDATE output into it (the accumulation-connect path —
 * ConnectionMeta{}.With<AccumulationSortConfig>(key)); the engine's typed runtime
 * accumulation-gather (ctx.InAll) assembles std::vector<SelectionCandidate> from every
 * connected provider's CURRENT output at Execute. The coordinator resolves that vector
 * directly with pickBestCandidate — so adding a provider is a connect, not a code change.
 *
 * Inputs (2):
 *   - INPUT_STATE         (InputState*, Execute)                       mouse buttons + modifier keys
 *   - PROVIDER_CANDIDATES (accumulation: std::vector<SelectionCandidate>, Execute)
 *                                                                      every wired provider's per-click candidate.
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

    // The fan-in of provider candidates. ACCUMULATION gather slot (Value strategy): each wired
    // provider node MultiConnects its CANDIDATE output here, and ctx.InAll assembles
    // std::vector<SelectionCandidate> from all of them at Execute (one per provider, in
    // sort-key/connection order). Optional so the coordinator still runs before any provider is
    // wired (an unconnected accumulation slot gathers an empty vector). The macro fixes the role to
    // SlotRole::Execute (the gather reads each producer's per-frame output).
    ACCUMULATION_INPUT_SLOT_V2(PROVIDER_CANDIDATES, std::vector<SelectionCandidate>, SelectionCandidate, 1,
        SlotNullability::Optional,
        SlotStorageStrategy::Value);

    // ===== OUTPUTS (1) =====
    OUTPUT_SLOT(SELECTION_COUNT, uint32_t, 0,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Constructor for runtime descriptor initialization
    SelectionCoordinatorNodeConfig() {
        HandleDescriptor inputStateDesc{"InputState*"};
        INIT_INPUT_DESC(INPUT_STATE, "input_state", ResourceLifetime::Persistent, inputStateDesc);

        HandleDescriptor candidatesDesc{"std::vector<SelectionCandidate>"};
        INIT_INPUT_DESC(PROVIDER_CANDIDATES, "provider_candidates", ResourceLifetime::Transient, candidatesDesc);

        HandleDescriptor selectionCountDesc{"uint32_t"};
        INIT_OUTPUT_DESC(SELECTION_COUNT, "selection_count", ResourceLifetime::Transient, selectionCountDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(SelectionCoordinatorNodeConfig, SelectionCoordinatorNodeCounts);

    // Slot index validations
    static_assert(INPUT_STATE_Slot::index == 0, "INPUT_STATE must be at index 0");
    static_assert(PROVIDER_CANDIDATES_Slot::index == 1, "PROVIDER_CANDIDATES must be at index 1");
    static_assert(PROVIDER_CANDIDATES_Slot::isAccumulation, "PROVIDER_CANDIDATES must be an accumulation slot");
    static_assert(SELECTION_COUNT_Slot::index == 0, "SELECTION_COUNT must be at index 0");

    // Type validations
    static_assert(std::is_same_v<INPUT_STATE_Slot::Type, InputStatePtr>);
    static_assert(std::is_same_v<PROVIDER_CANDIDATES_Slot::Type, std::vector<SelectionCandidate>>);
    static_assert(std::is_same_v<SELECTION_COUNT_Slot::Type, uint32_t>);
};

} // namespace Vixen::RenderGraph
