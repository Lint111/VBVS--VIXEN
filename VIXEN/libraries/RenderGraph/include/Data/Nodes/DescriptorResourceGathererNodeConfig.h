#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"

namespace ShaderManagement {
    struct ShaderDataBundle;
}

// Forward declare IDebugCapture for output slot
namespace Vixen::RenderGraph::Debug {
    class IDebugCapture;
}

namespace Vixen::RenderGraph {

using IDebugCapture = Debug::IDebugCapture;

/**
 * @brief Configuration for DescriptorResourceGathererNode
 *
 * This node reads shader SDI files to discover descriptor requirements and
 * accepts variadic inputs (arbitrary number of connections) which are validated
 * against the shader's descriptor layout during compile. Outputs a
 * std::vector<ResourceHandleVariant> containing all descriptor resources.
 *
 * Inputs:
 * - SHADER_DATA_BUNDLE (ShaderManagement::ShaderDataBundle*) - Contains descriptor metadata from shader reflection
 * - VARIADIC_RESOURCES (variadic) - Any number of ResourceHandleVariant connections (validated at compile)
 *
 * Outputs:
 * - DESCRIPTOR_RESOURCES (std::vector<ResourceHandleVariant>) - Resource array in binding order
 * - SHADER_DATA_BUNDLE_OUT (ShaderManagement::ShaderDataBundle*) - Pass-through for downstream nodes
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
    static constexpr size_t OUTPUTS = 3;  // DESCRIPTOR_RESOURCES, SHADER_DATA_BUNDLE_OUT, DEBUG_CAPTURE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(DescriptorResourceGathererNodeConfig,
                      DescriptorResourceGathererNodeCounts::INPUTS,
                      DescriptorResourceGathererNodeCounts::OUTPUTS,
                      DescriptorResourceGathererNodeCounts::ARRAY_MODE) {

    // ===== INPUTS (1 + dynamic) =====
    INPUT_SLOT(SHADER_DATA_BUNDLE, const std::shared_ptr<ShaderManagement::ShaderDataBundle>&, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (3) =====
    // Resource entries include handle + slotRole + optional debug capture metadata
    OUTPUT_SLOT(DESCRIPTOR_RESOURCES, std::vector<DescriptorResourceEntry>, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(SHADER_DATA_BUNDLE_OUT, const std::shared_ptr<ShaderManagement::ShaderDataBundle>&, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // First debug capture found in resources (for downstream debug reader nodes)
    OUTPUT_SLOT(DEBUG_CAPTURE, IDebugCapture*, 2,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    DescriptorResourceGathererNodeConfig() {
        // Initialize input descriptor
        HandleDescriptor shaderDataBundleDesc{"ShaderDataBundle*"};
        INIT_INPUT_DESC(SHADER_DATA_BUNDLE, "shader_data_bundle",
            ResourceLifetime::Persistent, shaderDataBundleDesc);

        // Initialize output descriptors
        // DescriptorResourceEntry includes handle + slotRole + optional debug capture pointer
        HandleDescriptor descriptorResourcesDesc{"std::vector<DescriptorResourceEntry>"};
        INIT_OUTPUT_DESC(DESCRIPTOR_RESOURCES, "descriptor_resources",
            ResourceLifetime::Transient, descriptorResourcesDesc);

        INIT_OUTPUT_DESC(SHADER_DATA_BUNDLE_OUT, "shader_data_bundle_out",
            ResourceLifetime::Persistent, shaderDataBundleDesc);

        HandleDescriptor debugCaptureDesc{"IDebugCapture*"};
        INIT_OUTPUT_DESC(DEBUG_CAPTURE, "debug_capture",
            ResourceLifetime::Transient, debugCaptureDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(DescriptorResourceGathererNodeConfig, DescriptorResourceGathererNodeCounts);

    static_assert(SHADER_DATA_BUNDLE_Slot::index == 0, "SHADER_DATA_BUNDLE must be at index 0");
    static_assert(!SHADER_DATA_BUNDLE_Slot::nullable, "SHADER_DATA_BUNDLE is required");

    static_assert(DESCRIPTOR_RESOURCES_Slot::index == 0, "DESCRIPTOR_RESOURCES must be at index 0");
    static_assert(!DESCRIPTOR_RESOURCES_Slot::nullable, "DESCRIPTOR_RESOURCES is required");

    static_assert(SHADER_DATA_BUNDLE_OUT_Slot::index == 1, "SHADER_DATA_BUNDLE_OUT must be at index 1");
    static_assert(!SHADER_DATA_BUNDLE_OUT_Slot::nullable, "SHADER_DATA_BUNDLE_OUT is required");

    static_assert(DEBUG_CAPTURE_Slot::index == 2, "DEBUG_CAPTURE must be at index 2");
    static_assert(DEBUG_CAPTURE_Slot::nullable, "DEBUG_CAPTURE is optional");

    // Type validations
    static_assert(std::is_same_v<SHADER_DATA_BUNDLE_Slot::Type, const std::shared_ptr<ShaderManagement::ShaderDataBundle>&>);
    static_assert(std::is_same_v<DESCRIPTOR_RESOURCES_Slot::Type, std::vector<DescriptorResourceEntry>>);
    static_assert(std::is_same_v<SHADER_DATA_BUNDLE_OUT_Slot::Type, const std::shared_ptr<ShaderManagement::ShaderDataBundle>&>);
    static_assert(std::is_same_v<DEBUG_CAPTURE_Slot::Type, IDebugCapture*>);
};
} // namespace Vixen::RenderGraph
