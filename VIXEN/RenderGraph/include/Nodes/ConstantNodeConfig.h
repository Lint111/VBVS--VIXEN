#pragma once

#include "Core/ResourceConfig.h"

// Forward declare VulkanShader for the shader constant type
class VulkanShader;
using VulkanShaderPtr = VulkanShader*;

namespace Vixen::RenderGraph {

/**
 * @brief Configuration for ConstantNode
 *
 * Provides a single output slot that can hold any registered resource type.
 * The actual type is determined when SetValue<T>() is called.
 *
 * Outputs:
 * - OUTPUT: Generic resource output (type set at runtime)
 */
namespace ConstantNodeCounts {
    static constexpr size_t INPUTS = 0;
    static constexpr size_t OUTPUTS = 1;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(ConstantNodeConfig,
                      ConstantNodeCounts::INPUTS,
                      ConstantNodeCounts::OUTPUTS,
                      ConstantNodeCounts::ARRAY_MODE) {

    // ===== OUTPUTS (1) =====
    // Generic output - actual type determined by SetValue<T>() call
    // Using VulkanShaderPtr as the concrete type for now (can be cast to other types)
    CONSTEXPR_OUTPUT(OUTPUT, VulkanShaderPtr, 0, false);

    ConstantNodeConfig() {
        // Initialize output descriptor as generic handle
        HandleDescriptor genericDesc{"Constant"};
        INIT_OUTPUT_DESC(OUTPUT, "output",
            ResourceLifetime::Persistent,
            genericDesc
        );
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == ConstantNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == ConstantNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == ConstantNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(OUTPUT_Slot::index == 0, "OUTPUT must be at index 0");
    static_assert(!OUTPUT_Slot::nullable, "OUTPUT is required");

    static_assert(std::is_same_v<OUTPUT_Slot::Type, VulkanShaderPtr>);
};

// Global compile-time validations
static_assert(ConstantNodeConfig::INPUT_COUNT == ConstantNodeCounts::INPUTS);
static_assert(ConstantNodeConfig::OUTPUT_COUNT == ConstantNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph
