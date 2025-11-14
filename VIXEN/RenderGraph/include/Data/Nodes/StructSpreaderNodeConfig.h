#pragma once

#include "Data/Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Generic variadic struct spreader configuration
 *
 * Takes a struct resource (via ResourceHandleVariant) and exposes its members
 * as variadic output slots. The node validates it's a struct with multiple members
 * and creates typed outputs for each member.
 *
 * Input:
 * - STRUCT_RESOURCE (ResourceHandleVariant) - Struct resource (e.g., SwapChainPublicVariables*)
 *
 * Outputs:
 * - Variadic outputs created based on struct member metadata
 *
 * Type ID: 121
 */
namespace StructSpreaderNodeCounts {
    static constexpr size_t INPUTS = 1;
    static constexpr size_t OUTPUTS = 0;  // Variadic outputs
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(StructSpreaderNodeConfig,
                      StructSpreaderNodeCounts::INPUTS,
                      StructSpreaderNodeCounts::OUTPUTS,
                      StructSpreaderNodeCounts::ARRAY_MODE) {

    // ===== INPUTS (1) =====
    INPUT_SLOT(STRUCT_RESOURCE, SwapChainPublicVariables*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    StructSpreaderNodeConfig() {
        HandleDescriptor structDesc{"SwapChainPublicVariables*"};
        INIT_INPUT_DESC(STRUCT_RESOURCE, "struct_resource",
            ResourceLifetime::Persistent,
            structDesc);
    }

    static_assert(INPUT_COUNT == StructSpreaderNodeCounts::INPUTS);
    static_assert(OUTPUT_COUNT == StructSpreaderNodeCounts::OUTPUTS);
    static_assert(ARRAY_MODE == StructSpreaderNodeCounts::ARRAY_MODE);
};

} // namespace Vixen::RenderGraph

