#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Data/Core/ResourceV3.h"

namespace ShaderManagement {
    struct ShaderDataBundle;
}

namespace Vixen::RenderGraph {

/**
 * @brief Configuration for PushConstantGathererNode
 *
 * This node reads shader reflection to discover push constant fields and
 * accepts variadic inputs (one per push constant field) which are validated
 * against the shader's push constant layout during compile. Outputs packed
 * push constant data ready for vkCmdPushConstants.
 *
 * Inputs:
 * - SHADER_DATA_BUNDLE (ShaderManagement::ShaderDataBundle*) - Contains push constant metadata from shader reflection
 * - VARIADIC_FIELDS (variadic) - Field values (vec3, float, etc.) validated at compile
 *
 * Outputs:
 * - PUSH_CONSTANT_DATA (std::vector<uint8_t>) - Packed push constant bytes
 * - PUSH_CONSTANT_RANGES (std::vector<VkPushConstantRange>) - Stage flags, offset, size
 * - SHADER_DATA_BUNDLE_OUT (ShaderManagement::ShaderDataBundle*) - Pass-through for downstream nodes
 *
 * Workflow:
 * 1. Setup: Read shader bundle to discover push constant fields
 * 2. Compile: Validate connected field values against shader requirements
 * 3. Execute: Pack field values into contiguous buffer with proper alignment
 *
 * Example for camera push constants:
 *   CameraNode -> [cameraPos] \
 *   TimeNode -> [time]         |-> PushConstantGatherer -> ComputeDispatch
 *   CameraNode -> [cameraDir]  /
 *
 * Enables data-driven push constant management - users connect field values,
 * system validates against shader metadata and handles packing automatically.
 */

// Compile-time slot counts
namespace PushConstantGathererNodeCounts {
    static constexpr size_t INPUTS = 1;   // SHADER_DATA_BUNDLE (+ dynamic variadic fields)
    static constexpr size_t OUTPUTS = 3;  // PUSH_CONSTANT_DATA + RANGES + SHADER_DATA_BUNDLE_OUT
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(PushConstantGathererNodeConfig,
                      PushConstantGathererNodeCounts::INPUTS,
                      PushConstantGathererNodeCounts::OUTPUTS,
                      PushConstantGathererNodeCounts::ARRAY_MODE) {

    // ===== INPUTS (1 + dynamic) =====
    INPUT_SLOT(SHADER_DATA_BUNDLE, ShaderManagement::ShaderDataBundle*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Variadic inputs for push constant fields are added dynamically
    // based on shader reflection. Each field becomes a variadic slot:
    // - Slot 0: vec3 cameraPos
    // - Slot 1: float time
    // - Slot 2: vec3 cameraDir
    // - etc.

    // ===== OUTPUTS (3) =====
    OUTPUT_SLOT(PUSH_CONSTANT_DATA, std::vector<uint8_t>, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(PUSH_CONSTANT_RANGES, std::vector<VkPushConstantRange>, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(SHADER_DATA_BUNDLE_OUT, ShaderManagement::ShaderDataBundle*, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    PushConstantGathererNodeConfig() {
        // Initialize input descriptor
        HandleDescriptor shaderDataBundleDesc{"ShaderDataBundle*"};
        INIT_INPUT_DESC(SHADER_DATA_BUNDLE, "shader_data_bundle",
            ResourceLifetime::Persistent, shaderDataBundleDesc);

        // Initialize output descriptors
        HandleDescriptor pushConstantDataDesc{"std::vector<uint8_t>"};
        INIT_OUTPUT_DESC(PUSH_CONSTANT_DATA, "push_constant_data",
            ResourceLifetime::Transient, pushConstantDataDesc);

        HandleDescriptor pushConstantRangesDesc{"std::vector<VkPushConstantRange>"};
        INIT_OUTPUT_DESC(PUSH_CONSTANT_RANGES, "push_constant_ranges",
            ResourceLifetime::Transient, pushConstantRangesDesc);

        INIT_OUTPUT_DESC(SHADER_DATA_BUNDLE_OUT, "shader_data_bundle_out",
            ResourceLifetime::Persistent, shaderDataBundleDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(PushConstantGathererNodeConfig, PushConstantGathererNodeCounts);

    static_assert(SHADER_DATA_BUNDLE_Slot::index == 0, "SHADER_DATA_BUNDLE must be at index 0");
    static_assert(!SHADER_DATA_BUNDLE_Slot::nullable, "SHADER_DATA_BUNDLE is required");

    static_assert(PUSH_CONSTANT_DATA_Slot::index == 0, "PUSH_CONSTANT_DATA must be at index 0");
    static_assert(!PUSH_CONSTANT_DATA_Slot::nullable, "PUSH_CONSTANT_DATA is required");

    static_assert(PUSH_CONSTANT_RANGES_Slot::index == 1, "PUSH_CONSTANT_RANGES must be at index 1");
    static_assert(!PUSH_CONSTANT_RANGES_Slot::nullable, "PUSH_CONSTANT_RANGES is required");

    static_assert(SHADER_DATA_BUNDLE_OUT_Slot::index == 2, "SHADER_DATA_BUNDLE_OUT must be at index 2");
    static_assert(!SHADER_DATA_BUNDLE_OUT_Slot::nullable, "SHADER_DATA_BUNDLE_OUT is required");

    // Type validations
    static_assert(std::is_same_v<SHADER_DATA_BUNDLE_Slot::Type, ShaderManagement::ShaderDataBundle*>);
    static_assert(std::is_same_v<PUSH_CONSTANT_DATA_Slot::Type, std::vector<uint8_t>>);
    static_assert(std::is_same_v<PUSH_CONSTANT_RANGES_Slot::Type, std::vector<VkPushConstantRange>>);
    static_assert(std::is_same_v<SHADER_DATA_BUNDLE_OUT_Slot::Type, ShaderManagement::ShaderDataBundle*>);
};

} // namespace Vixen::RenderGraph