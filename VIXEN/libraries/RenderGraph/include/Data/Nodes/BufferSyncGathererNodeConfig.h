#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"

namespace Vixen::RenderGraph {

/**
 * @brief Configuration for BufferSyncGathererNode
 *
 * Sampled Lighting Inc3 M5: mirrors DescriptorResourceGathererNode/
 * PushConstantGathererNode's own "variadic-input, single-array-output" shape
 * (see those nodes' own file headers) — this is the buffer-sync-hazard
 * analogue: gathers a manually-pre-registered, FIXED count of VkBuffer
 * handles (no shader-reflection discovery, unlike the other two gatherers —
 * there is no shader to reflect against; the buffer COUNT is known at
 * graph-build time, e.g. "2" for a ping-pong pair) into ONE array-typed
 * output.
 *
 * WHY THIS EXISTS (replaces ComputeStageNodeConfig's old fixed named slots
 * BUFFER_WRITE/BUFFER_READ_A/BUFFER_READ_B): a fixed named slot per buffer
 * does not scale — each new multi-buffer hazard shape (M1's HitRecord pair,
 * M4's reservoir ping-pong) previously meant adding MORE named slots to
 * ComputeStageNodeConfig, a shared load-bearing file every ComputeStageNode
 * consumer depends on. This node is the "governor"/accumulator: a dedicated
 * gatherer collects a VARIABLE number of VkBuffer inputs and produces ONE
 * array-typed output that ComputeStageNode consumes via a single array-mode
 * INPUT_SLOT_SYNC (BUFFER_WRITE_ARRAY or BUFFER_READ_ARRAY) — the same
 * "gatherer produces one array slot, consumer takes one slot" shape
 * DescriptorResourceGathererNode already established for descriptor
 * binding, now applied to the sync-hazard declaration itself.
 *
 * AccessKind (ComputeStorageWrite vs ComputeStorageRead) is declared on the
 * CONSUMING slot (ComputeStageNodeConfig's BUFFER_WRITE_ARRAY/
 * BUFFER_READ_ARRAY), NOT here — this node is a pure buffer-handle
 * collector with no opinion about read vs write; the SAME gatherer type is
 * reused for both the producer's write-array and the consumer's read-array
 * (two separate node instances, each wired to a DIFFERENT set of source
 * buffers but going through the same node type).
 *
 * Inputs:
 * - (0 static) BUFFER_ENTRIES (variadic) - N VkBuffer connections, pre-
 *   registered via PreRegisterBufferSlots(count) before Setup.
 *
 * Outputs:
 * - BUFFER_ARRAY (std::vector<VkBuffer>) - gathered buffer handles, in
 *   connection order.
 */

namespace BufferSyncGathererNodeCounts {
    static constexpr size_t INPUTS = 0;   // purely variadic — no static inputs
    static constexpr size_t OUTPUTS = 1;  // BUFFER_ARRAY
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(BufferSyncGathererNodeConfig,
                      BufferSyncGathererNodeCounts::INPUTS,
                      BufferSyncGathererNodeCounts::OUTPUTS,
                      BufferSyncGathererNodeCounts::ARRAY_MODE) {

    // ===== INPUTS (0 static + dynamic) =====
    // Variadic buffer entries are added dynamically via PreRegisterBufferSlots(count),
    // then connected in order via batch.Connect(source, sourceSlot, gatherer, /*index*/ i).

    // ===== OUTPUTS (1) =====
    OUTPUT_SLOT(BUFFER_ARRAY, std::vector<VkBuffer>, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    BufferSyncGathererNodeConfig() {
        HandleDescriptor bufferArrayDesc{"std::vector<VkBuffer>"};
        INIT_OUTPUT_DESC(BUFFER_ARRAY, "buffer_array",
            ResourceLifetime::Transient, bufferArrayDesc);
    }

    VALIDATE_NODE_CONFIG(BufferSyncGathererNodeConfig, BufferSyncGathererNodeCounts);

    static_assert(BUFFER_ARRAY_Slot::index == 0, "BUFFER_ARRAY must be at index 0");
    static_assert(!BUFFER_ARRAY_Slot::nullable, "BUFFER_ARRAY is required");
    static_assert(std::is_same_v<BUFFER_ARRAY_Slot::Type, std::vector<VkBuffer>>);
};

} // namespace Vixen::RenderGraph
