#pragma once

#include "Core/ResourceConfig.h"
#include "Core/ResourceVariant.h"

namespace ShaderManagement {
    struct ShaderDataBundle;
}

namespace Vixen::RenderGraph {

using ShaderDataBundlePtr = std::shared_ptr<ShaderManagement::ShaderDataBundle>;

/**
 * @brief Configuration for DescriptorResourceGathererNode
 *
 * This node reads shader SDI files to discover descriptor requirements and
 * accepts variadic inputs (arbitrary number of connections) which are validated
 * against the shader's descriptor layout during compile. Outputs a
 * std::vector<ResourceHandleVariant> containing all descriptor resources.
 *
 * Inputs:
 * - SHADER_DATA_BUNDLE (ShaderDataBundlePtr) - Contains descriptor metadata from shader reflection
 * - VARIADIC_RESOURCES (variadic) - Any number of ResourceHandleVariant connections (validated at compile)
 *
 * Outputs:
 * - DESCRIPTOR_RESOURCES (std::vector<ResourceHandleVariant>) - Resource array in binding order
 * - SHADER_DATA_BUNDLE_OUT (ShaderDataBundlePtr) - Pass-through for downstream nodes
 *
 * Workflow:
 * 1. Setup: Read shader bundle to discover required descriptors
 * 2. Compile: Validate connected resources against shader requirements
 * 3. Execute: Gather validated resources into output array
 *
 * This enables fully data-driven descriptor management - users connect resources,
 * system validates against shader metadata automatically.
 */

// Compile-time slot counts
namespace DescriptorResourceGathererNodeCounts {
    static constexpr size_t INPUTS = 1;   // SHADER_DATA_BUNDLE (+ dynamic variadic resources)
    static constexpr size_t OUTPUTS = 2;  // DESCRIPTOR_RESOURCES array + SHADER_DATA_BUNDLE_OUT pass-through
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(DescriptorResourceGathererNodeConfig,
                      DescriptorResourceGathererNodeCounts::INPUTS,
                      DescriptorResourceGathererNodeCounts::OUTPUTS,
                      DescriptorResourceGathererNodeCounts::ARRAY_MODE) {

    // ===== INPUTS (1 + dynamic) =====
    INPUT_SLOT(SHADER_DATA_BUNDLE, ShaderDataBundlePtr, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====
    OUTPUT_SLOT(DESCRIPTOR_RESOURCES, std::vector<ResourceVariant>, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(SHADER_DATA_BUNDLE_OUT, ShaderDataBundlePtr, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    DescriptorResourceGathererNodeConfig() {
        // Initialize input descriptor
        HandleDescriptor shaderDataBundleDesc{"ShaderDataBundle*"};
        INIT_INPUT_DESC(SHADER_DATA_BUNDLE, "shader_data_bundle",
            ResourceLifetime::Persistent, shaderDataBundleDesc);

        // Initialize output descriptors
        HandleDescriptor descriptorResourcesDesc{"std::vector<ResourceHandleVariant>"};
        INIT_OUTPUT_DESC(DESCRIPTOR_RESOURCES, "descriptor_resources",
            ResourceLifetime::Transient, descriptorResourcesDesc);

        INIT_OUTPUT_DESC(SHADER_DATA_BUNDLE_OUT, "shader_data_bundle_out",
            ResourceLifetime::Persistent, shaderDataBundleDesc);
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == DescriptorResourceGathererNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == DescriptorResourceGathererNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == DescriptorResourceGathererNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(SHADER_DATA_BUNDLE_Slot::index == 0, "SHADER_DATA_BUNDLE must be at index 0");
    static_assert(!SHADER_DATA_BUNDLE_Slot::nullable, "SHADER_DATA_BUNDLE is required");

    static_assert(DESCRIPTOR_RESOURCES_Slot::index == 0, "DESCRIPTOR_RESOURCES must be at index 0");
    static_assert(!DESCRIPTOR_RESOURCES_Slot::nullable, "DESCRIPTOR_RESOURCES is required");

    static_assert(SHADER_DATA_BUNDLE_OUT_Slot::index == 1, "SHADER_DATA_BUNDLE_OUT must be at index 1");
    static_assert(!SHADER_DATA_BUNDLE_OUT_Slot::nullable, "SHADER_DATA_BUNDLE_OUT is required");

    // Type validations
    static_assert(std::is_same_v<SHADER_DATA_BUNDLE_Slot::Type, ShaderDataBundlePtr>);
    static_assert(std::is_same_v<DESCRIPTOR_RESOURCES_Slot::Type, std::vector<ResourceVariant>>);
    static_assert(std::is_same_v<SHADER_DATA_BUNDLE_OUT_Slot::Type, ShaderDataBundlePtr>);
};

// Global compile-time validations
static_assert(DescriptorResourceGathererNodeConfig::INPUT_COUNT == DescriptorResourceGathererNodeCounts::INPUTS);
static_assert(DescriptorResourceGathererNodeConfig::OUTPUT_COUNT == DescriptorResourceGathererNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph
