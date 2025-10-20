#pragma once

#include "RenderGraph/ResourceConfig.h"
#include "ShaderManagement/DescriptorLayoutSpec.h"

// Forward declaration for shader program type
namespace ShaderManagement {
    struct CompiledProgram;
}

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for DescriptorSetNode
 *
 * This node creates descriptor sets based on a user-provided layout specification.
 * NO hardcoded assumptions about uniform buffers, textures, or bindings!
 *
 * Inputs:
 * - SHADER_PROGRAM (CompiledProgram*, nullable) - Optional shader program for automatic reflection
 *
 * Outputs:
 * - DESCRIPTOR_SET_LAYOUT (VkDescriptorSetLayout) - Layout defining descriptor bindings
 * - DESCRIPTOR_POOL (VkDescriptorPool) - Pool for allocating descriptor sets
 * - DESCRIPTOR_SETS (VkDescriptorSet[]) - Allocated descriptor sets (array, updated on demand)
 *
 * Parameters:
 * - NONE (layout spec is set via SetLayoutSpec() method, not parameters)
 *
 * IMPORTANT: 
 * - The DescriptorLayoutSpec must remain valid for the node's lifetime
 * - Descriptor set updates are done via UpdateDescriptorSet() method with actual resources
 * - No automatic resource creation (uniform buffers, etc.) - user provides resources
 *
 * Example usage:
 * @code
 * // Create layout specification
 * DescriptorLayoutSpec layout;
 * layout.AddBinding(DescriptorBindingSpec(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT));
 * layout.AddBinding(DescriptorBindingSpec(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT));
 * layout.maxSets = 2;
 *
 * // Set on node before compile
 * node->SetLayoutSpec(&layout);
 * node->Compile();
 * @endcode
 */
CONSTEXPR_NODE_CONFIG(DescriptorSetNodeConfig, 1, 3, false) {
    // ===== INPUTS (1) =====
    // Shader program for automatic descriptor reflection (future feature)
    CONSTEXPR_INPUT(SHADER_PROGRAM, const ShaderManagement::CompiledProgram*, 0, true);

    // ===== OUTPUTS (3) =====
    // Descriptor set layout
    CONSTEXPR_OUTPUT(DESCRIPTOR_SET_LAYOUT, VkDescriptorSetLayout, 0, false);

    // Descriptor pool
    CONSTEXPR_OUTPUT(DESCRIPTOR_POOL, VkDescriptorPool, 1, false);

    // Descriptor sets (array output - allocated based on layoutSpec.maxSets)
    CONSTEXPR_OUTPUT(DESCRIPTOR_SETS, VkDescriptorSet, 2, false);

    DescriptorSetNodeConfig() {
        // Initialize input descriptors
        INIT_INPUT_DESC(SHADER_PROGRAM, "shader_program",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque pointer (future use)
        );

        // Initialize output descriptors
        INIT_OUTPUT_DESC(DESCRIPTOR_SET_LAYOUT, "descriptor_set_layout",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_OUTPUT_DESC(DESCRIPTOR_POOL, "descriptor_pool",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_OUTPUT_DESC(DESCRIPTOR_SETS, "descriptor_sets",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle (array)
        );
    }

    // Compile-time validations
    static_assert(SHADER_PROGRAM_Slot::index == 0, "SHADER_PROGRAM must be at index 0");
    static_assert(SHADER_PROGRAM_Slot::nullable, "SHADER_PROGRAM is optional (future use)");

    static_assert(DESCRIPTOR_SET_LAYOUT_Slot::index == 0, "DESCRIPTOR_SET_LAYOUT must be at index 0");
    static_assert(!DESCRIPTOR_SET_LAYOUT_Slot::nullable, "DESCRIPTOR_SET_LAYOUT is required");

    static_assert(DESCRIPTOR_POOL_Slot::index == 1, "DESCRIPTOR_POOL must be at index 1");
    static_assert(!DESCRIPTOR_POOL_Slot::nullable, "DESCRIPTOR_POOL is required");

    static_assert(DESCRIPTOR_SETS_Slot::index == 2, "DESCRIPTOR_SETS must be at index 2");
    static_assert(!DESCRIPTOR_SETS_Slot::nullable, "DESCRIPTOR_SETS is required");

    // Type validations
    static_assert(std::is_same_v<SHADER_PROGRAM_Slot::Type, const ShaderManagement::CompiledProgram*>);
    static_assert(std::is_same_v<DESCRIPTOR_SET_LAYOUT_Slot::Type, VkDescriptorSetLayout>);
    static_assert(std::is_same_v<DESCRIPTOR_POOL_Slot::Type, VkDescriptorPool>);
    static_assert(std::is_same_v<DESCRIPTOR_SETS_Slot::Type, VkDescriptorSet>);
    
    //-------------------------------------------------------------------------
    // Parameters
    //-------------------------------------------------------------------------
    
    /**
     * @brief Descriptor layout specification parameter
     */
    static constexpr const char* PARAM_LAYOUT_SPEC = "layoutSpec";
};

// Global compile-time validations
static_assert(DescriptorSetNodeConfig::INPUT_COUNT == 1);
static_assert(DescriptorSetNodeConfig::OUTPUT_COUNT == 3);

} // namespace Vixen::RenderGraph
