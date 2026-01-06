#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"  // For BoolVector typedef
#include "Data/Core/BoolVector.h"
#include "Data/Core/ConnectionConcepts.h"  // For Iterable concept and iterable_value_t

namespace Vixen::RenderGraph::Data {
    struct BoolVector;
}

using BoolVector = Vixen::RenderGraph::Data::BoolVector;

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
    static constexpr size_t INPUTS = 2;   // OPERATION, INPUTS (vector of bools)
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
 *   ConstantNode(BoolOp::AND) → OPERATION
 *   physicsLoop.SHOULD_EXECUTE → INPUTS (connection 0)
 *   networkLoop.SHOULD_EXECUTE → INPUTS (connection 1)
 *   aiLoop.SHOULD_EXECUTE → INPUTS (connection 2)
 *   OUTPUT → customNode.SHOULD_EXECUTE
 *
 * Inputs: 2 (OPERATION: BoolOp from ConstantNode, INPUTS: bool[] array slot)
 * Outputs: 1 (OUTPUT: bool)
 */
CONSTEXPR_NODE_CONFIG(BoolOpNodeConfig,
                      BoolOpNodeCounts::INPUTS,
                      BoolOpNodeCounts::OUTPUTS,
                      BoolOpNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (2) =====
    INPUT_SLOT(OPERATION, BoolOp, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Sprint 6.0.2: Proper accumulation slot using container type
    // Collects bool elements into std::vector<bool> using Value strategy (copies)
    ACCUMULATION_INPUT_SLOT_V2(INPUTS, std::vector<bool>, bool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotStorageStrategy::Value);

    // ===== OUTPUTS (1) =====
    OUTPUT_SLOT(OUTPUT, bool, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Constructor for runtime descriptor initialization
    BoolOpNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor boolOpDesc{"BoolOp"};
        INIT_INPUT_DESC(OPERATION, "operation", ResourceLifetime::Transient, boolOpDesc);

        HandleDescriptor boolVecDesc{"std::vector<bool>"};
        INIT_INPUT_DESC(INPUTS, "inputs", ResourceLifetime::Transient, boolVecDesc);

        HandleDescriptor boolDesc{"bool"};
        INIT_OUTPUT_DESC(OUTPUT, "output", ResourceLifetime::Transient, boolDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(BoolOpNodeConfig, BoolOpNodeCounts);

    static_assert(OPERATION_Slot::index == 0, "OPERATION must be at index 0");
    static_assert(INPUTS_Slot::index == 1, "INPUTS must be at index 1");
    static_assert(OUTPUT_Slot::index == 0, "OUTPUT must be at index 0");
    static_assert(!OPERATION_Slot::nullable, "OPERATION is required");
    static_assert(!INPUTS_Slot::nullable, "INPUTS is required");
    static_assert(!OUTPUT_Slot::nullable, "OUTPUT is not nullable");

    // Type validations
    static_assert(std::is_same_v<OPERATION_Slot::Type, BoolOp>);
    static_assert(std::is_same_v<INPUTS_Slot::Type, std::vector<bool>>);  // Sprint 6.0.2: Container type (not element type)
    static_assert(std::is_same_v<OUTPUT_Slot::Type, bool>);

    
};

} // namespace Vixen::RenderGraph
