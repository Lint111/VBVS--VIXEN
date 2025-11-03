#pragma once

#include "Core/ResourceConfig.h"
// TEMPORARILY REMOVED - using simple descriptor layouts for MVP
// #include "ShaderManagement/DescriptorLayoutSpec.h"
#include "VulkanResources/VulkanDevice.h"

// Forward declaration for shader program type
namespace ShaderManagement {
    struct CompiledProgram;
    struct DescriptorLayoutSpec; // Forward declare
    struct ShaderDataBundle;
}

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;
using ShaderDataBundlePtr = std::shared_ptr<ShaderManagement::ShaderDataBundle>;

/**
 * @brief Pure constexpr resource configuration for DescriptorSetNode
 *
 * This node creates descriptor sets based on a user-provided layout specification.
 * NO hardcoded assumptions about uniform buffers, textures, or bindings!
 *
 * Inputs:
 * - SHADER_PROGRAM (CompiledProgram*, nullable) - Optional shader program for automatic reflection
 * - VULKAN_DEVICE (VulkanDevicePtr) - VulkanDevice pointer for resource creation
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
// Compile-time slot counts (declared early for reuse)
namespace DescriptorSetNodeCounts {
    static constexpr size_t INPUTS = 9; // Added DESCRIPTOR_RESOURCES for Phase H
    static constexpr size_t OUTPUTS = 4;  // Added VULKAN_DEVICE_OUT for pass-through
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(DescriptorSetNodeConfig,
                      DescriptorSetNodeCounts::INPUTS,
                      DescriptorSetNodeCounts::OUTPUTS,
                      DescriptorSetNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (8) =====
    INPUT_SLOT(SHADER_PROGRAM, const ShaderManagement::CompiledProgram*, 0,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevicePtr, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(TEXTURE_IMAGE, VkImage, 2,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(TEXTURE_VIEW, VkImageView, 3,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(TEXTURE_SAMPLER, VkSampler, 4,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(SHADER_DATA_BUNDLE, ShaderDataBundlePtr, 5,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(SWAPCHAIN_PUBLIC, SwapChainPublicVariablesPtr, 6,
        SlotNullability::Optional,  // Optional: for compute shaders with storage images
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_INDEX, uint32_t, 7,
        SlotNullability::Required,
        SlotRole::ExecuteOnly,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(DESCRIPTOR_RESOURCES, ResourceHandleVariantVector, 8,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (4) =====
    OUTPUT_SLOT(DESCRIPTOR_SET_LAYOUT, VkDescriptorSetLayout, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(DESCRIPTOR_POOL, VkDescriptorPool, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(DESCRIPTOR_SETS, DescriptorSetVector, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevicePtr, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    DescriptorSetNodeConfig() {
        // Initialize input descriptors
        INIT_INPUT_DESC(SHADER_PROGRAM, "shader_program",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque pointer (future use)
        );

        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        // Texture inputs (MVP for descriptor set binding 1)
        INIT_INPUT_DESC(TEXTURE_IMAGE, "texture_image",
            ResourceLifetime::Persistent,
            ImageDescription{}
        );

        INIT_INPUT_DESC(TEXTURE_VIEW, "texture_view",
            ResourceLifetime::Persistent,
            ImageDescription{}
        );

        INIT_INPUT_DESC(TEXTURE_SAMPLER, "texture_sampler",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // ShaderDataBundle input (Phase 2)
        HandleDescriptor shaderDataBundleDesc{"ShaderDataBundle*"};
        INIT_INPUT_DESC(SHADER_DATA_BUNDLE, "shader_data_bundle", ResourceLifetime::Persistent, shaderDataBundleDesc);

        // Per-frame resource inputs (Phase 0.1)
        HandleDescriptor swapchainPublicDesc{"SwapChainPublicVariables*"};
        INIT_INPUT_DESC(SWAPCHAIN_PUBLIC, "swapchain_public", ResourceLifetime::Persistent, swapchainPublicDesc);

        INIT_INPUT_DESC(IMAGE_INDEX, "image_index", ResourceLifetime::Transient, BufferDescription{});

        HandleDescriptor descriptorResourcesDesc{"ResourceHandleVariantVector"};
        INIT_INPUT_DESC(DESCRIPTOR_RESOURCES, "descriptor_resources", ResourceLifetime::Transient, descriptorResourcesDesc);

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

    // Compile-time validations
    static_assert(INPUT_COUNT == DescriptorSetNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == DescriptorSetNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == DescriptorSetNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(SHADER_PROGRAM_Slot::index == 0, "SHADER_PROGRAM must be at index 0");
    static_assert(SHADER_PROGRAM_Slot::nullable, "SHADER_PROGRAM is optional (future use)");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 1, "VULKAN_DEVICE input must be at index 1");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(SHADER_DATA_BUNDLE_Slot::index == 5, "SHADER_DATA_BUNDLE must be at index 5");
    static_assert(!SHADER_DATA_BUNDLE_Slot::nullable, "SHADER_DATA_BUNDLE is required");

    static_assert(SWAPCHAIN_PUBLIC_Slot::index == 6, "SWAPCHAIN_PUBLIC must be at index 6");
    static_assert(SWAPCHAIN_PUBLIC_Slot::nullable, "SWAPCHAIN_PUBLIC is optional");

    static_assert(IMAGE_INDEX_Slot::index == 7, "IMAGE_INDEX must be at index 7");
    static_assert(!IMAGE_INDEX_Slot::nullable, "IMAGE_INDEX is required");

    static_assert(DESCRIPTOR_RESOURCES_Slot::index == 8, "DESCRIPTOR_RESOURCES must be at index 8");
    static_assert(DESCRIPTOR_RESOURCES_Slot::nullable, "DESCRIPTOR_RESOURCES is optional");

    static_assert(DESCRIPTOR_SET_LAYOUT_Slot::index == 0, "DESCRIPTOR_SET_LAYOUT must be at index 0");
    static_assert(!DESCRIPTOR_SET_LAYOUT_Slot::nullable, "DESCRIPTOR_SET_LAYOUT is required");

    static_assert(DESCRIPTOR_POOL_Slot::index == 1, "DESCRIPTOR_POOL must be at index 1");
    static_assert(!DESCRIPTOR_POOL_Slot::nullable, "DESCRIPTOR_POOL is required");

    static_assert(DESCRIPTOR_SETS_Slot::index == 2, "DESCRIPTOR_SETS must be at index 2");
    static_assert(!DESCRIPTOR_SETS_Slot::nullable, "DESCRIPTOR_SETS is required");

    // Type validations
    static_assert(std::is_same_v<SHADER_PROGRAM_Slot::Type, const ShaderManagement::CompiledProgram*>);
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<SHADER_DATA_BUNDLE_Slot::Type, ShaderDataBundlePtr>);

    static_assert(std::is_same_v<DESCRIPTOR_SET_LAYOUT_Slot::Type, VkDescriptorSetLayout>);
    static_assert(std::is_same_v<DESCRIPTOR_POOL_Slot::Type, VkDescriptorPool>);
    static_assert(std::is_same_v<DESCRIPTOR_SETS_Slot::Type, DescriptorSetVector>);

    //-------------------------------------------------------------------------
    // Parameters
    //-------------------------------------------------------------------------

    /**
     * @brief Descriptor layout specification parameter
     */
    static constexpr const char* PARAM_LAYOUT_SPEC = "layoutSpec";
};

// Global compile-time validations
static_assert(DescriptorSetNodeConfig::INPUT_COUNT == DescriptorSetNodeCounts::INPUTS);
static_assert(DescriptorSetNodeConfig::OUTPUT_COUNT == DescriptorSetNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph
