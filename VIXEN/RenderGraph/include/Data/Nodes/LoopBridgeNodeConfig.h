#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Core/LoopManager.h"



namespace Vixen::RenderGraph {

// Compile-time slot counts
namespace LoopBridgeNodeCounts {
    static constexpr size_t INPUTS = 1;   // LOOP_ID input
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for LoopBridgeNode
 *
 * Phase 0.4: Graph-native loop system bridge
 * Accesses graph-owned LoopManager and publishes loop state
 *
 * Inputs: 1 (LOOP_ID: uint32_t - from ConstantNode)
 * Outputs: 2 (LOOP_OUT: LoopReference*, SHOULD_EXECUTE: bool)
 */
CONSTEXPR_NODE_CONFIG(LoopBridgeNodeConfig,
                      LoopBridgeNodeCounts::INPUTS,
                      LoopBridgeNodeCounts::OUTPUTS,
                      LoopBridgeNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (1) =====
    INPUT_SLOT(LOOP_ID, uint32_t, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====
    OUTPUT_SLOT(LOOP_OUT, const LoopReference*, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(SHOULD_EXECUTE, bool, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Constructor for runtime descriptor initialization
    LoopBridgeNodeConfig() {
        // Initialize input descriptor
        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(LOOP_ID, "loop_id", ResourceLifetime::Transient, uint32Desc);

        // Initialize output descriptors
        HandleDescriptor loopRefDesc{"const LoopReference*"};
        INIT_OUTPUT_DESC(LOOP_OUT, "loop_out", ResourceLifetime::Transient, loopRefDesc);

        HandleDescriptor boolDesc{"bool"};
        INIT_OUTPUT_DESC(SHOULD_EXECUTE, "should_execute", ResourceLifetime::Transient, boolDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(LoopBridgeNodeConfig, LoopBridgeNodeCounts);

    static_assert(LOOP_ID_Slot::index == 0, "LOOP_ID must be at index 0");
    static_assert(!LOOP_ID_Slot::nullable, "LOOP_ID is required");

    static_assert(LOOP_OUT_Slot::index == 0, "LOOP_OUT must be at index 0");
    static_assert(SHOULD_EXECUTE_Slot::index == 1, "SHOULD_EXECUTE must be at index 1");
    static_assert(!LOOP_OUT_Slot::nullable, "LOOP_OUT is not nullable");
    static_assert(!SHOULD_EXECUTE_Slot::nullable, "SHOULD_EXECUTE is not nullable");

    // Type validations
    static_assert(std::is_same_v<LOOP_ID_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<LOOP_OUT_Slot::Type, const LoopReference*>);
    static_assert(std::is_same_v<SHOULD_EXECUTE_Slot::Type, bool>);
};

} // namespace Vixen::RenderGraph
