#pragma once

#include "Data/Core/ResourceConfig.h"
// TEMPORARILY REMOVED - using simple descriptor layouts for MVP
// #include "ShaderManagement/DescriptorLayoutSpec.h"
#include "VulkanDevice.h"

using SlotRoleEnum = Vixen::RenderGraph::SlotRole;  // For descriptor filtering


// Forward declaration for shader program type
namespace ShaderManagement {
    struct CompiledProgram;
    struct DescriptorLayoutSpec; // Forward declare
    struct ShaderDataBundle;
}

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;
using ShaderDataBundle = ShaderManagement::ShaderDataBundle;

/**
 * @brief Pure constexpr resource configuration for DescriptorSetNode
 *
 * This node creates descriptor sets based on a user-provided layout specification.
 * NO hardcoded assumptions about uniform buffers, textures, or bindings!
 *
 * Inputs:
 * - SHADER_PROGRAM (CompiledProgram*, nullable) - Optional shader program for automatic reflection
 * - VULKAN_DEVICE (VulkanDevice*) - VulkanDevice pointer for resource creation
 *
 * Outputs:
 * - DESCRIPTOR_SET_LAYOUT (VkDescriptorSetLayout) - Layout defining descriptor bindings
 * - DESCRIPTOR_POOL (VkDescriptorPool) - Pool for allocating descriptor sets
 * - DESCRIPTOR_SETS (VkDescriptorSet[]) - Allocated descriptor sets (array, updated on demand)
 *
 * Parameters:
 * - NONE (layout spec is set via SetLayoutSpec() method, not parameters)
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
// Compile-time slot counts (declared early for reuse)
namespace DescriptorSetNodeCounts {
    static constexpr size_t INPUTS = 6;  // DEVICE, SHADER_BUNDLE, SWAPCHAIN_COUNT, DESCRIPTOR_RESOURCES, SLOT_ROLES, IMAGE_INDEX
    static constexpr size_t OUTPUTS = 4;  // LAYOUT, POOL, SETS, DEVICE_OUT
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(DescriptorSetNodeConfig,
                      DescriptorSetNodeCounts::INPUTS,
                      DescriptorSetNodeCounts::OUTPUTS,
                      DescriptorSetNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (6) - Data-Driven with Metadata =====
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(SHADER_DATA_BUNDLE, const std::shared_ptr<ShaderManagement::ShaderDataBundle>&, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Swapchain image count metadata (extracted from SwapChainPublicVariables::imageCount)
    // Execute-only: image count rarely changes, no need to trigger recompilation
    INPUT_SLOT(SWAPCHAIN_IMAGE_COUNT, uint32_t, 2,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Resource array from DescriptorResourceGathererNode (data-driven binding)
    INPUT_SLOT(DESCRIPTOR_RESOURCES, std::vector<DescriptorHandleVariant>, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Slot roles array (parallel to DESCRIPTOR_RESOURCES) for filtering
    INPUT_SLOT(DESCRIPTOR_SLOT_ROLES, std::vector<SlotRoleEnum>, 4,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_INDEX, uint32_t, 5,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (4) =====
    OUTPUT_SLOT(DESCRIPTOR_SET_LAYOUT, VkDescriptorSetLayout, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(DESCRIPTOR_POOL, VkDescriptorPool, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(DESCRIPTOR_SETS, std::vector<VkDescriptorSet>, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    DescriptorSetNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor shaderDataBundleDesc{"ShaderDataBundle*"};
        INIT_INPUT_DESC(SHADER_DATA_BUNDLE, "shader_data_bundle", ResourceLifetime::Persistent, shaderDataBundleDesc);

        INIT_INPUT_DESC(SWAPCHAIN_IMAGE_COUNT, "swapchain_image_count", ResourceLifetime::Transient, BufferDescription{});

        HandleDescriptor descriptorResourcesDesc{"std::vector<ResourceHandleVariant>"};
        INIT_INPUT_DESC(DESCRIPTOR_RESOURCES, "descriptor_resources", ResourceLifetime::Transient, descriptorResourcesDesc);

        HandleDescriptor slotRolesDesc{"std::vector<SlotRoleEnum>"};
        INIT_INPUT_DESC(DESCRIPTOR_SLOT_ROLES, "descriptor_slot_roles", ResourceLifetime::Transient, slotRolesDesc);

        INPUT_SLOT(IMAGE_INDEX, uint32_t, 5,
            SlotNullability::Required,
            SlotRole::Execute,
            SlotMutability::ReadOnly,
            SlotScope::NodeLevel);

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

        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out",
            ResourceLifetime::Persistent,
            vulkanDeviceDesc  // Pass-through device pointer
        );
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(DescriptorSetNodeConfig, DescriptorSetNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE input must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(SHADER_DATA_BUNDLE_Slot::index == 1, "SHADER_DATA_BUNDLE must be at index 1");
    static_assert(!SHADER_DATA_BUNDLE_Slot::nullable, "SHADER_DATA_BUNDLE is required");

    static_assert(SWAPCHAIN_IMAGE_COUNT_Slot::index == 2, "SWAPCHAIN_IMAGE_COUNT must be at index 2");
    static_assert(!SWAPCHAIN_IMAGE_COUNT_Slot::nullable, "SWAPCHAIN_IMAGE_COUNT is required");

    static_assert(DESCRIPTOR_RESOURCES_Slot::index == 3, "DESCRIPTOR_RESOURCES must be at index 3");
    static_assert(!DESCRIPTOR_RESOURCES_Slot::nullable, "DESCRIPTOR_RESOURCES is required");

    static_assert(DESCRIPTOR_SLOT_ROLES_Slot::index == 4, "DESCRIPTOR_SLOT_ROLES must be at index 4");
    static_assert(!DESCRIPTOR_SLOT_ROLES_Slot::nullable, "DESCRIPTOR_SLOT_ROLES is required");

    static_assert(IMAGE_INDEX_Slot::index == 5, "IMAGE_INDEX must be at index 5");
    static_assert(!IMAGE_INDEX_Slot::nullable, "IMAGE_INDEX is required");

    static_assert(DESCRIPTOR_SET_LAYOUT_Slot::index == 0, "DESCRIPTOR_SET_LAYOUT must be at index 0");
    static_assert(!DESCRIPTOR_SET_LAYOUT_Slot::nullable, "DESCRIPTOR_SET_LAYOUT is required");

    static_assert(DESCRIPTOR_POOL_Slot::index == 1, "DESCRIPTOR_POOL must be at index 1");
    static_assert(!DESCRIPTOR_POOL_Slot::nullable, "DESCRIPTOR_POOL is required");

    static_assert(DESCRIPTOR_SETS_Slot::index == 2, "DESCRIPTOR_SETS must be at index 2");
    static_assert(!DESCRIPTOR_SETS_Slot::nullable, "DESCRIPTOR_SETS is required");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<SHADER_DATA_BUNDLE_Slot::Type, const std::shared_ptr<ShaderManagement::ShaderDataBundle>&>);
    static_assert(std::is_same_v<SWAPCHAIN_IMAGE_COUNT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<DESCRIPTOR_RESOURCES_Slot::Type, std::vector<DescriptorHandleVariant>>);

    static_assert(std::is_same_v<DESCRIPTOR_SET_LAYOUT_Slot::Type, VkDescriptorSetLayout>);
    static_assert(std::is_same_v<DESCRIPTOR_POOL_Slot::Type, VkDescriptorPool>);
    static_assert(std::is_same_v<DESCRIPTOR_SETS_Slot::Type, std::vector<VkDescriptorSet>>);

    //-------------------------------------------------------------------------
    // Parameters
    //-------------------------------------------------------------------------

    /**
     * @brief Descriptor layout specification parameter
     */
    static constexpr const char* PARAM_LAYOUT_SPEC = "layoutSpec";
};

} // namespace Vixen::RenderGraph
