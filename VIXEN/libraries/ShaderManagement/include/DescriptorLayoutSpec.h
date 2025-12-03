#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <unordered_map>

namespace ShaderManagement {

/**
 * @brief Descriptor binding specification (device-agnostic)
 * 
 * Describes a single binding in a descriptor set layout.
 * Can be populated from SPIRV reflection or manually specified.
 */
struct DescriptorBindingSpec {
    uint32_t binding;                    // Binding index (e.g., layout(binding=0))
    VkDescriptorType descriptorType;     // Type (uniform, sampler, storage, etc.)
    uint32_t descriptorCount;            // Number of descriptors (for arrays)
    VkShaderStageFlags stageFlags;       // Which shader stages access this
    std::string name;                    // Debug name (optional)

    DescriptorBindingSpec(
        uint32_t binding = 0,
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        uint32_t count = 1,
        VkShaderStageFlags stages = VK_SHADER_STAGE_ALL
    ) : binding(binding),
        descriptorType(type),
        descriptorCount(count),
        stageFlags(stages)
    {}
};

/**
 * @brief Complete descriptor set layout specification
 * 
 * Describes all bindings in a descriptor set.
 * Can be:
 * 1. Extracted from SPIRV reflection (future)
 * 2. Manually specified by user
 * 3. Built from shader program metadata
 */
struct DescriptorLayoutSpec {
    std::vector<DescriptorBindingSpec> bindings;
    uint32_t maxSets = 1;  // How many descriptor sets to allocate

    /**
     * @brief Add a binding to the layout
     */
    void AddBinding(const DescriptorBindingSpec& binding) {
        bindings.push_back(binding);
    }

    /**
     * @brief Count descriptors of a specific type (for pool sizing)
     */
    uint32_t CountDescriptorType(VkDescriptorType type) const {
        uint32_t count = 0;
        for (const auto& binding : bindings) {
            if (binding.descriptorType == type) {
                count += binding.descriptorCount;
            }
        }
        return count;
    }

    /**
     * @brief Convert to VkDescriptorSetLayoutBinding array
     */
    std::vector<VkDescriptorSetLayoutBinding> ToVulkanBindings() const {
        std::vector<VkDescriptorSetLayoutBinding> vkBindings;
        vkBindings.reserve(bindings.size());

        for (const auto& spec : bindings) {
            VkDescriptorSetLayoutBinding vkBinding{};
            vkBinding.binding = spec.binding;
            vkBinding.descriptorType = spec.descriptorType;
            vkBinding.descriptorCount = spec.descriptorCount;
            vkBinding.stageFlags = spec.stageFlags;
            vkBinding.pImmutableSamplers = nullptr;
            vkBindings.push_back(vkBinding);
        }

        return vkBindings;
    }

    /**
     * @brief Create pool sizes from bindings
     */
    std::vector<VkDescriptorPoolSize> ToPoolSizes() const {
        // Group by descriptor type
        std::unordered_map<VkDescriptorType, uint32_t> typeCounts;

        for (const auto& binding : bindings) {
            typeCounts[binding.descriptorType] += binding.descriptorCount * maxSets;
        }

        std::vector<VkDescriptorPoolSize> poolSizes;
        poolSizes.reserve(typeCounts.size());

        for (const auto& [type, count] : typeCounts) {
            VkDescriptorPoolSize poolSize{};
            poolSize.type = type;
            poolSize.descriptorCount = count;
            poolSizes.push_back(poolSize);
        }

        return poolSizes;
    }

    /**
     * @brief Check if layout is valid
     */
    bool IsValid() const {
        return !bindings.empty();
    }
};

/**
 * @brief Helper factory for common descriptor layouts
 */
struct DescriptorLayoutPresets {
    /**
     * @brief MVP + optional texture (legacy VulkanDrawable style)
     */
    static DescriptorLayoutSpec MVP_Texture(bool includeTexture = false) {
        DescriptorLayoutSpec spec;

        // Uniform buffer at binding 0 (MVP matrix)
        spec.AddBinding(DescriptorBindingSpec(
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            1,
            VK_SHADER_STAGE_VERTEX_BIT
        ));

        // Optional combined image sampler at binding 1
        if (includeTexture) {
            spec.AddBinding(DescriptorBindingSpec(
                1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                1,
                VK_SHADER_STAGE_FRAGMENT_BIT
            ));
        }

        return spec;
    }

    /**
     * @brief PBR material layout (multiple textures + uniform)
     */
    static DescriptorLayoutSpec PBR_Material() {
        DescriptorLayoutSpec spec;

        // UBO at binding 0 (material properties)
        spec.AddBinding(DescriptorBindingSpec(
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT
        ));

        // Albedo texture
        spec.AddBinding(DescriptorBindingSpec(
            1,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT
        ));

        // Normal map
        spec.AddBinding(DescriptorBindingSpec(
            2,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT
        ));

        // Metallic/Roughness
        spec.AddBinding(DescriptorBindingSpec(
            3,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT
        ));

        return spec;
    }

    /**
     * @brief Compute shader with storage buffers
     */
    static DescriptorLayoutSpec Compute_Storage(uint32_t bufferCount = 2) {
        DescriptorLayoutSpec spec;

        for (uint32_t i = 0; i < bufferCount; ++i) {
            spec.AddBinding(DescriptorBindingSpec(
                i,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                1,
                VK_SHADER_STAGE_COMPUTE_BIT
            ));
        }

        return spec;
    }
};

} // namespace ShaderManagement
