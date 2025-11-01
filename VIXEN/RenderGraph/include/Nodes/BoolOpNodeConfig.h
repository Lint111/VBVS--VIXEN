#pragma once
#include "Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Boolean operation types for BoolOpNode
 */
enum class BoolOp : uint8_t {
    AND = 0,  // A && B && C && ... (all inputs must be true)
    OR = 1,   // A || B || C || ... (at least one input must be true)
    XOR = 2,  // Exactly one input must be true (exclusive or across all inputs)
    NOT = 3,  // !A (single input only, ignores others)
    NAND = 4, // !(A && B && C && ...) (not all inputs true)
    NOR = 5   // !(A || B || C || ...) (no inputs true)
};

// Compile-time slot counts
namespace BoolOpNodeCounts {
    static constexpr size_t INPUTS = 1;   // INPUTS (single slot, multiple connections)
    static constexpr size_t OUTPUTS = 1;  // OUTPUT
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for BoolOpNode
 *
 * Phase 0.4: Boolean logic composition for multi-loop conditions
 * Enables graph-side composition of loop execution logic with N inputs
 *
 * Example: Node executes when ALL loops active (physics AND network AND AI)
 *   physicsLoop.SHOULD_EXECUTE → INPUTS[0]
 *   networkLoop.SHOULD_EXECUTE → INPUTS[1]
 *   aiLoop.SHOULD_EXECUTE → INPUTS[2]
 *   OPERATION = BoolOp::AND
 *   OUTPUT → customNode.SHOULD_EXECUTE
 *
 * Inputs: 1 array slot (INPUTS: bool[], supports N connections)
 * Outputs: 1 (OUTPUT: bool)
 * Parameters: OPERATION (BoolOp)
 */
CONSTEXPR_NODE_CONFIG(BoolOpNodeConfig,
                      BoolOpNodeCounts::INPUTS,
                      BoolOpNodeCounts::OUTPUTS,
                      BoolOpNodeCounts::ARRAY_MODE) {
    // Compile-time input slot definition (array slot)
    CONSTEXPR_INPUT(INPUTS, bool, 0, false);

    // Compile-time output slot definition
    CONSTEXPR_OUTPUT(OUTPUT, bool, 0, false);

    // Parameter definition
    PARAM_DEFINITION(OPERATION, BoolOp);

    // Constructor for runtime descriptor initialization
    BoolOpNodeConfig() {
        // Initialize input descriptor (array slot)
        HandleDescriptor boolDesc{"bool"};
        INIT_INPUT_DESC(INPUTS, "inputs", ResourceLifetime::Transient, boolDesc);

        // Initialize output descriptor
        INIT_OUTPUT_DESC(OUTPUT, "output", ResourceLifetime::Transient, boolDesc);
    }

    // Compile-time validation
    static_assert(INPUT_COUNT == BoolOpNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == BoolOpNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == BoolOpNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(INPUTS_Slot::index == 0, "INPUTS must be at index 0");
    static_assert(OUTPUT_Slot::index == 0, "OUTPUT must be at index 0");
    static_assert(!INPUTS_Slot::nullable, "INPUTS is required");
    static_assert(!OUTPUT_Slot::nullable, "OUTPUT is not nullable");

    // Type validations
    static_assert(std::is_same_v<INPUTS_Slot::Type, bool>);
    static_assert(std::is_same_v<OUTPUT_Slot::Type, bool>);
};

// Global compile-time validations
static_assert(BoolOpNodeConfig::INPUT_COUNT == BoolOpNodeCounts::INPUTS);
static_assert(BoolOpNodeConfig::OUTPUT_COUNT == BoolOpNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph
