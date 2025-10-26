#include "ShaderManagement/SpirvReflector.h"
#include "spirv_reflect.h"
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#ifdef HAS_OPENSSL
#include <openssl/sha.h>
#endif

namespace ShaderManagement {

namespace {

// ===== Type Conversion Helpers =====

/**
 * @brief Convert SPIRV-Reflect type to our SpirvTypeInfo
 */
SpirvTypeInfo ConvertType(const SpvReflectTypeDescription* typeDesc) {
    SpirvTypeInfo info;

    if (!typeDesc) {
        return info;
    }

    // Determine base type
    switch (typeDesc->type_flags) {
        case SPV_REFLECT_TYPE_FLAG_VOID:
            info.baseType = SpirvTypeInfo::BaseType::Void;
            break;
        case SPV_REFLECT_TYPE_FLAG_BOOL:
            info.baseType = SpirvTypeInfo::BaseType::Boolean;
            info.width = 32;  // Booleans are typically 32-bit
            break;
        case SPV_REFLECT_TYPE_FLAG_INT:
            info.baseType = SpirvTypeInfo::BaseType::Int;
            info.width = typeDesc->traits.numeric.scalar.width;
            break;
        case SPV_REFLECT_TYPE_FLAG_FLOAT:
            info.baseType = SpirvTypeInfo::BaseType::Float;
            info.width = typeDesc->traits.numeric.scalar.width;
            break;
        case SPV_REFLECT_TYPE_FLAG_STRUCT:
            info.baseType = SpirvTypeInfo::BaseType::Struct;
            info.structName = typeDesc->type_name ? typeDesc->type_name : "AnonymousStruct";
            break;
        case SPV_REFLECT_TYPE_FLAG_ARRAY:
            info.baseType = SpirvTypeInfo::BaseType::Array;
            info.arraySize = typeDesc->traits.array.dims_count > 0 ?
                typeDesc->traits.array.dims[0] : 0;
            break;
        case SPV_REFLECT_TYPE_FLAG_VECTOR:
            info.baseType = SpirvTypeInfo::BaseType::Vector;
            info.vecSize = typeDesc->traits.numeric.vector.component_count;
            info.width = typeDesc->traits.numeric.scalar.width;
            break;
        case SPV_REFLECT_TYPE_FLAG_MATRIX:
            info.baseType = SpirvTypeInfo::BaseType::Matrix;
            info.columns = typeDesc->traits.numeric.matrix.column_count;
            info.rows = typeDesc->traits.numeric.matrix.row_count;
            info.width = typeDesc->traits.numeric.scalar.width;
            break;
        case SPV_REFLECT_TYPE_FLAG_EXTERNAL_IMAGE:
            info.baseType = SpirvTypeInfo::BaseType::Image;
            break;
        case SPV_REFLECT_TYPE_FLAG_EXTERNAL_SAMPLER:
            info.baseType = SpirvTypeInfo::BaseType::Sampler;
            break;
        case SPV_REFLECT_TYPE_FLAG_EXTERNAL_SAMPLED_IMAGE:
            info.baseType = SpirvTypeInfo::BaseType::SampledImage;
            break;
        case SPV_REFLECT_TYPE_FLAG_EXTERNAL_ACCELERATION_STRUCTURE:
            info.baseType = SpirvTypeInfo::BaseType::AccelerationStructure;
            break;
        default:
            // Handle combinations (e.g., ARRAY | VECTOR)
            if (typeDesc->type_flags & SPV_REFLECT_TYPE_FLAG_ARRAY) {
                info.baseType = SpirvTypeInfo::BaseType::Array;
                info.arraySize = typeDesc->traits.array.dims_count > 0 ?
                    typeDesc->traits.array.dims[0] : 0;
            } else if (typeDesc->type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR) {
                info.baseType = SpirvTypeInfo::BaseType::Vector;
                info.vecSize = typeDesc->traits.numeric.vector.component_count;
            }
            break;
    }

    // Size information
    info.sizeInBytes = typeDesc->traits.numeric.scalar.width / 8;
    if (info.baseType == SpirvTypeInfo::BaseType::Vector) {
        info.sizeInBytes *= info.vecSize;
    } else if (info.baseType == SpirvTypeInfo::BaseType::Matrix) {
        info.sizeInBytes = (info.width / 8) * info.rows * info.columns;
    }

    // Alignment (typically same as size for basic types)
    info.alignment = info.sizeInBytes;

    return info;
}

/**
 * @brief Convert VkDescriptorType to string for documentation
 */
const char* DescriptorTypeToString(VkDescriptorType type) {
    switch (type) {
        case VK_DESCRIPTOR_TYPE_SAMPLER: return "Sampler";
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return "CombinedImageSampler";
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return "SampledImage";
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return "StorageImage";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return "UniformBuffer";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return "StorageBuffer";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: return "UniformBufferDynamic";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: return "StorageBufferDynamic";
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return "InputAttachment";
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: return "AccelerationStructure";
        default: return "Unknown";
    }
}

/**
 * @brief Convert SPIRV-Reflect descriptor type to Vulkan descriptor type
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
            return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

/**
 * @brief Convert shader stage to Vulkan flag bits
 */
VkShaderStageFlagBits ShaderStageToVulkan(ShaderStage stage) {
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
        case ShaderStage::Miss:     return VK_SHADER_STAGE_MISS_BIT_KHR;
        case ShaderStage::ClosestHit: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        case ShaderStage::AnyHit:   return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        case ShaderStage::Intersection: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        case ShaderStage::Callable: return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
        default: return VK_SHADER_STAGE_ALL;
    }
}

} // anonymous namespace

// ===== SpirvTypeInfo Methods =====

std::string SpirvTypeInfo::ToCppType() const {
    std::ostringstream oss;

    switch (baseType) {
        case BaseType::Void:
            oss << "void";
            break;
        case BaseType::Boolean:
            oss << "bool";
            break;
        case BaseType::Int:
            oss << (width == 64 ? "int64_t" : "int32_t");
            break;
        case BaseType::UInt:
            oss << (width == 64 ? "uint64_t" : "uint32_t");
            break;
        case BaseType::Float:
            oss << (width == 64 ? "double" : "float");
            break;
        case BaseType::Vector:
            // Use GLSL-style vector types (vec2, vec3, vec4)
            if (width == 32) {
                oss << "vec" << vecSize;
            } else {
                oss << "dvec" << vecSize;
            }
            break;
        case BaseType::Matrix:
            // Use GLSL-style matrix types (mat4, mat3x4, etc.)
            oss << "mat";
            if (columns != rows) {
                oss << columns << "x" << rows;
            } else {
                oss << columns;
            }
            break;
        case BaseType::Struct:
            oss << structName;
            break;
        case BaseType::Array:
            oss << "Array[" << arraySize << "]";
            break;
        case BaseType::Sampler:
            oss << "VkSampler";
            break;
        case BaseType::Image:
            oss << "VkImage";
            break;
        case BaseType::SampledImage:
            oss << "VkImageView";
            break;
        case BaseType::AccelerationStructure:
            oss << "VkAccelerationStructureKHR";
            break;
        default:
            oss << "unknown";
            break;
    }

    return oss.str();
}

std::string SpirvTypeInfo::ToGlslType() const {
    std::ostringstream oss;

    switch (baseType) {
        case BaseType::Void:
            oss << "void";
            break;
        case BaseType::Boolean:
            oss << "bool";
            break;
        case BaseType::Int:
            oss << "int";
            break;
        case BaseType::UInt:
            oss << "uint";
            break;
        case BaseType::Float:
            oss << (width == 64 ? "double" : "float");
            break;
        case BaseType::Vector:
            if (width == 32) {
                oss << "vec" << vecSize;
            } else if (width == 64) {
                oss << "dvec" << vecSize;
            } else {
                oss << "ivec" << vecSize;  // Integer vectors
            }
            break;
        case BaseType::Matrix:
            oss << "mat" << columns;
            if (columns != rows) {
                oss << "x" << rows;
            }
            break;
        case BaseType::Struct:
            oss << structName;
            break;
        case BaseType::Array:
            oss << "[" << arraySize << "]";
            break;
        case BaseType::Sampler:
            oss << "sampler";
            break;
        case BaseType::SampledImage:
            oss << "sampler2D";  // Default to 2D
            break;
        case BaseType::Image:
            oss << "image2D";
            break;
        default:
            oss << "unknown";
            break;
    }

    return oss.str();
}

// ===== SpirvReflector Implementation =====

std::unique_ptr<SpirvReflectionData> SpirvReflector::Reflect(const CompiledProgram& program) {
    try {
        auto data = std::make_unique<SpirvReflectionData>();

        data->programName = program.name;
        data->pipelineType = program.pipelineType;

        // Reflect each stage and merge results
        for (const auto& stage : program.stages) {
            if (!stage.spirvCode.empty()) {
                data->stages.push_back(stage.stage);
                data->entryPoints[stage.stage] = stage.entryPoint;

                auto stageData = ReflectStage(stage.spirvCode, stage.stage);
                if (stageData) {
                    MergeReflectionData(*data, *stageData);
                }
            }
        }

        // Compute interface hash
        data->interfaceHash = ComputeInterfaceHash(program);

        return data;

    } catch (const std::exception& e) {
        // Return nullptr on error
        return nullptr;
    }
}

std::unique_ptr<SpirvReflectionData> SpirvReflector::ReflectStage(
    const std::vector<uint32_t>& spirvCode,
    ShaderStage stage
) {
    try {
        auto data = std::make_unique<SpirvReflectionData>();
        data->stages.push_back(stage);

        // Create SPIRV-Reflect module
        SpvReflectShaderModule module;
        SpvReflectResult result = spvReflectCreateShaderModule(
            spirvCode.size() * sizeof(uint32_t),
            spirvCode.data(),
            &module
        );

        if (result != SPV_REFLECT_RESULT_SUCCESS) {
            return nullptr;
        }

        // Extract entry point
        data->entryPoints[stage] = module.entry_point_name ? module.entry_point_name : "main";

        // Reflect different components
        ReflectDescriptors(module, stage, *data);
        ReflectPushConstants(module, stage, *data);

        if (stage == ShaderStage::Vertex) {
            ReflectVertexInputs(module, *data);
        }

        ReflectStageInputsOutputs(module, stage, *data);
        ReflectSpecializationConstants(module, *data);

        spvReflectDestroyShaderModule(&module);

        return data;

    } catch (const std::exception& e) {
        return nullptr;
    }
}

std::string SpirvReflector::ComputeInterfaceHash(const CompiledProgram& program) {
#ifdef HAS_OPENSSL
    // Compute SHA-256 hash of all SPIRV code
    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    for (const auto& stage : program.stages) {
        if (!stage.spirvCode.empty()) {
            SHA256_Update(&sha256, stage.spirvCode.data(),
                stage.spirvCode.size() * sizeof(uint32_t));
        }
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);

    // Convert to hex string
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(hash[i]);
    }

    return oss.str();
#else
    // Fallback: simple hash based on code size and stage count
    std::ostringstream oss;
    oss << std::hex << program.stages.size() << "_";
    for (const auto& stage : program.stages) {
        oss << stage.spirvCode.size() << "_";
    }
    return oss.str();
#endif
}

bool SpirvReflector::AreInterfacesCompatible(
    const SpirvReflectionData& a,
    const SpirvReflectionData& b
) {
    // Simple hash-based comparison
    return a.interfaceHash == b.interfaceHash;
}

// ===== Private Implementation Helpers =====

void SpirvReflector::ReflectDescriptors(
    SpvReflectShaderModule& module,
    ShaderStage stage,
    SpirvReflectionData& data
) {
    uint32_t bindingCount = 0;
    SpvReflectResult result = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, nullptr);

    if (result != SPV_REFLECT_RESULT_SUCCESS || bindingCount == 0) {
        return;
    }

    std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
    result = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, bindings.data());

    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        return;
    }

    VkShaderStageFlagBits stageFlag = ShaderStageToVulkan(stage);

    for (const auto* binding : bindings) {
        SpirvDescriptorBinding desc;
        desc.set = binding->set;
        desc.binding = binding->binding;
        desc.name = binding->name ? binding->name : "";
        desc.descriptorType = ConvertDescriptorType(binding->descriptor_type);
        desc.descriptorCount = binding->count;
        desc.stageFlags = stageFlag;
        desc.typeInfo = ConvertType(binding->type_description);

        // TODO: Extract struct definition for UBO/SSBO
        // This would require walking the type_description tree

        data.descriptorSets[desc.set].push_back(desc);
    }
}

void SpirvReflector::ReflectPushConstants(
    SpvReflectShaderModule& module,
    ShaderStage stage,
    SpirvReflectionData& data
) {
    uint32_t pushConstantCount = 0;
    SpvReflectResult result = spvReflectEnumeratePushConstantBlocks(&module, &pushConstantCount, nullptr);

    if (result != SPV_REFLECT_RESULT_SUCCESS || pushConstantCount == 0) {
        return;
    }

    std::vector<SpvReflectBlockVariable*> pushConstants(pushConstantCount);
    result = spvReflectEnumeratePushConstantBlocks(&module, &pushConstantCount, pushConstants.data());

    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        return;
    }

    VkShaderStageFlagBits stageFlag = ShaderStageToVulkan(stage);

    for (const auto* pushConstant : pushConstants) {
        SpirvPushConstantRange range;
        range.name = pushConstant->name ? pushConstant->name : "PushConstants";
        range.offset = pushConstant->offset;
        range.size = pushConstant->size;
        range.stageFlags = stageFlag;

        // TODO: Extract struct definition from type_description

        data.pushConstants.push_back(range);
    }
}

void SpirvReflector::ReflectVertexInputs(
    SpvReflectShaderModule& module,
    SpirvReflectionData& data
) {
    uint32_t inputCount = 0;
    SpvReflectResult result = spvReflectEnumerateInputVariables(&module, &inputCount, nullptr);

    if (result != SPV_REFLECT_RESULT_SUCCESS || inputCount == 0) {
        return;
    }

    std::vector<SpvReflectInterfaceVariable*> inputs(inputCount);
    result = spvReflectEnumerateInputVariables(&module, &inputCount, inputs.data());

    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        return;
    }

    for (const auto* input : inputs) {
        // Skip built-ins
        if (input->built_in != -1) {
            continue;
        }

        SpirvVertexInput vertexInput;
        vertexInput.location = input->location;
        vertexInput.name = input->name ? input->name : "";
        vertexInput.type = ConvertType(input->type_description);

        // Convert to VkFormat (simplified)
        // TODO: More comprehensive format mapping
        vertexInput.format = VK_FORMAT_R32G32B32A32_SFLOAT;

        data.vertexInputs.push_back(vertexInput);
    }
}

void SpirvReflector::ReflectStageInputsOutputs(
    SpvReflectShaderModule& module,
    ShaderStage stage,
    SpirvReflectionData& data
) {
    // Reflect outputs
    uint32_t outputCount = 0;
    SpvReflectResult result = spvReflectEnumerateOutputVariables(&module, &outputCount, nullptr);

    if (result == SPV_REFLECT_RESULT_SUCCESS && outputCount > 0) {
        std::vector<SpvReflectInterfaceVariable*> outputs(outputCount);
        result = spvReflectEnumerateOutputVariables(&module, &outputCount, outputs.data());

        if (result == SPV_REFLECT_RESULT_SUCCESS) {
            for (const auto* output : outputs) {
                if (output->built_in != -1) {
                    continue;  // Skip built-ins
                }

                SpirvStageIO stageIO;
                stageIO.location = output->location;
                stageIO.name = output->name ? output->name : "";
                stageIO.type = ConvertType(output->type_description);

                data.stageOutputs[stage].push_back(stageIO);
            }
        }
    }
}

void SpirvReflector::ReflectSpecializationConstants(
    SpvReflectShaderModule& module,
    SpirvReflectionData& data
) {
    // SPIRV-Reflect doesn't have direct specialization constant enumeration
    // This would require custom SPIRV parsing or extension
    // Leave as future enhancement
}

SpirvTypeInfo SpirvReflector::ConvertTypeInfo(const SpvReflectTypeDescription* typeDesc) {
    return ConvertType(typeDesc);
}

SpirvStructDefinition SpirvReflector::ConvertStructDefinition(const SpvReflectTypeDescription* typeDesc) {
    SpirvStructDefinition structDef;

    if (!typeDesc || !(typeDesc->type_flags & SPV_REFLECT_TYPE_FLAG_STRUCT)) {
        return structDef;
    }

    structDef.name = typeDesc->type_name ? typeDesc->type_name : "AnonymousStruct";
    structDef.sizeInBytes = typeDesc->traits.numeric.scalar.width / 8;

    // TODO: Walk struct members and populate
    // This requires recursive type_description traversal

    return structDef;
}

void SpirvReflector::MergeReflectionData(SpirvReflectionData& dest, const SpirvReflectionData& src) {
    // Merge descriptor sets
    for (const auto& [setIndex, bindings] : src.descriptorSets) {
        for (const auto& binding : bindings) {
            // Check if binding already exists
            auto& destBindings = dest.descriptorSets[setIndex];
            auto it = std::find_if(destBindings.begin(), destBindings.end(),
                [&binding](const SpirvDescriptorBinding& b) {
                    return b.binding == binding.binding;
                });

            if (it != destBindings.end()) {
                // Merge stage flags
                it->stageFlags |= binding.stageFlags;
            } else {
                // Add new binding
                destBindings.push_back(binding);
            }
        }
    }

    // Merge push constants
    for (const auto& pushConst : src.pushConstants) {
        auto it = std::find_if(dest.pushConstants.begin(), dest.pushConstants.end(),
            [&pushConst](const SpirvPushConstantRange& pc) {
                return pc.offset == pushConst.offset && pc.size == pushConst.size;
            });

        if (it != dest.pushConstants.end()) {
            it->stageFlags |= pushConst.stageFlags;
        } else {
            dest.pushConstants.push_back(pushConst);
        }
    }

    // Merge vertex inputs (usually only in vertex shader)
    dest.vertexInputs.insert(dest.vertexInputs.end(),
        src.vertexInputs.begin(), src.vertexInputs.end());

    // Merge stage I/O
    for (const auto& [stage, ios] : src.stageInputs) {
        dest.stageInputs[stage].insert(dest.stageInputs[stage].end(), ios.begin(), ios.end());
    }
    for (const auto& [stage, ios] : src.stageOutputs) {
        dest.stageOutputs[stage].insert(dest.stageOutputs[stage].end(), ios.begin(), ios.end());
    }

    // Merge struct definitions
    dest.structDefinitions.insert(dest.structDefinitions.end(),
        src.structDefinitions.begin(), src.structDefinitions.end());
}

} // namespace ShaderManagement
