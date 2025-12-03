#include "SPIRVReflection.h"
#include "spirv_reflect.h"
#include <unordered_map>
#include <algorithm>
#include <stdexcept>

namespace ShaderManagement {

namespace {
    /**
     * @brief Convert SpvReflectDescriptorType to VkDescriptorType
     */
    VkDescriptorType ConvertDescriptorType(SpvReflectDescriptorType spvType) {
        switch (spvType) {
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                return VK_DESCRIPTOR_TYPE_SAMPLER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
            case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
                return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            default:
                throw std::runtime_error("Unknown SPIRV descriptor type: " + std::to_string(static_cast<int>(spvType)));
        }
    }

    /**
     * @brief Convert ShaderStage enum to VkShaderStageFlagBits
     */
    VkShaderStageFlagBits ConvertShaderStage(ShaderStage stage) {
        switch (stage) {
            case ShaderStage::Vertex:   return VK_SHADER_STAGE_VERTEX_BIT;
            case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
            case ShaderStage::Compute:  return VK_SHADER_STAGE_COMPUTE_BIT;
            case ShaderStage::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
            case ShaderStage::TessControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            case ShaderStage::TessEval: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            case ShaderStage::Mesh:     return VK_SHADER_STAGE_MESH_BIT_EXT;
            case ShaderStage::Task:     return VK_SHADER_STAGE_TASK_BIT_EXT;
            case ShaderStage::RayGen:   return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            case ShaderStage::AnyHit:   return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            case ShaderStage::ClosestHit: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            case ShaderStage::Miss:     return VK_SHADER_STAGE_MISS_BIT_KHR;
            case ShaderStage::Intersection: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            case ShaderStage::Callable: return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            default:
                throw std::runtime_error("Unknown shader stage");
        }
    }

    /**
     * @brief Reflect bindings from a single shader stage
     */
    void ReflectStageBindings(
        const std::vector<uint32_t>& spirvCode,
        ShaderStage stage,
        std::unordered_map<uint32_t, DescriptorBindingSpec>& mergedBindings
    ) {
        // Create SPIRV-Reflect module
        SpvReflectShaderModule module;
        SpvReflectResult result = spvReflectCreateShaderModule(
            spirvCode.size() * sizeof(uint32_t),
            spirvCode.data(),
            &module
        );

        if (result != SPV_REFLECT_RESULT_SUCCESS) {
            throw std::runtime_error("Failed to create SPIRV-Reflect module: " + std::to_string(result));
        }

        // Enumerate descriptor bindings
        uint32_t bindingCount = 0;
        result = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, nullptr);
        if (result != SPV_REFLECT_RESULT_SUCCESS) {
            spvReflectDestroyShaderModule(&module);
            throw std::runtime_error("Failed to enumerate descriptor bindings: " + std::to_string(result));
        }

        if (bindingCount == 0) {
            spvReflectDestroyShaderModule(&module);
            return; // No bindings in this stage
        }

        std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
        result = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, bindings.data());
        if (result != SPV_REFLECT_RESULT_SUCCESS) {
            spvReflectDestroyShaderModule(&module);
            throw std::runtime_error("Failed to get descriptor bindings: " + std::to_string(result));
        }

        // Convert stage to Vulkan flag
        VkShaderStageFlagBits stageFlag = ConvertShaderStage(stage);

        // Process each binding
        for (const auto* binding : bindings) {
            uint32_t bindingIndex = binding->binding;

            // Check if binding already exists (from another stage)
            auto it = mergedBindings.find(bindingIndex);
            if (it != mergedBindings.end()) {
                // Binding exists - merge stage flags and validate compatibility
                DescriptorBindingSpec& existing = it->second;

                // Validate type compatibility
                VkDescriptorType type = ConvertDescriptorType(binding->descriptor_type);
                if (existing.descriptorType != type) {
                    spvReflectDestroyShaderModule(&module);
                    throw std::runtime_error(
                        "Binding " + std::to_string(bindingIndex) + 
                        " has incompatible types across shader stages"
                    );
                }

                // Validate count compatibility
                if (existing.descriptorCount != binding->count) {
                    spvReflectDestroyShaderModule(&module);
                    throw std::runtime_error(
                        "Binding " + std::to_string(bindingIndex) + 
                        " has incompatible counts across shader stages"
                    );
                }

                // Merge stage flags
                existing.stageFlags |= stageFlag;
            } else {
                // New binding - add it
                DescriptorBindingSpec spec;
                spec.binding = bindingIndex;
                spec.descriptorType = ConvertDescriptorType(binding->descriptor_type);
                spec.descriptorCount = binding->count;
                spec.stageFlags = stageFlag;
                spec.name = binding->name ? binding->name : "";

                mergedBindings[bindingIndex] = spec;
            }
        }

        spvReflectDestroyShaderModule(&module);
    }

} // anonymous namespace

std::unique_ptr<DescriptorLayoutSpec> ReflectDescriptorLayout(const CompiledProgram& program) {
    try {
        // Map: binding index -> merged binding spec
        std::unordered_map<uint32_t, DescriptorBindingSpec> mergedBindings;

        // Reflect each shader stage
        for (const auto& stage : program.stages) {
            if (!stage.spirvCode.empty()) {
                ReflectStageBindings(stage.spirvCode, stage.stage, mergedBindings);
            }
        }

        // Convert to DescriptorLayoutSpec
        auto layoutSpec = std::make_unique<DescriptorLayoutSpec>();

        // Sort bindings by index for consistency
        std::vector<uint32_t> sortedIndices;
        sortedIndices.reserve(mergedBindings.size());
        for (const auto& [index, _] : mergedBindings) {
            sortedIndices.push_back(index);
        }
        std::sort(sortedIndices.begin(), sortedIndices.end());

        // Add bindings in sorted order
        for (uint32_t index : sortedIndices) {
            layoutSpec->AddBinding(mergedBindings[index]);
        }

        // Default to 1 set (can be overridden by user)
        layoutSpec->maxSets = 1;

        return layoutSpec;

    } catch (const std::exception& e) {
        // Return nullptr on error - caller can log and handle
        return nullptr;
    }
}

} // namespace ShaderManagement
