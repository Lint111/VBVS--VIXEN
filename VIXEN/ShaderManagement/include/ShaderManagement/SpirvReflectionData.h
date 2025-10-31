#pragma once

#include "ShaderStage.h"
#include "DescriptorLayoutSpec.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace ShaderManagement {

/**
 * @brief SPIRV type information
 *
 * Represents the type of a shader variable (scalar, vector, matrix, struct, array).
 */
struct SpirvTypeInfo {
    enum class BaseType {
        Void,
        Boolean,
        Int,
        UInt,
        Float,
        Double,
        Struct,
        Array,
        Matrix,
        Vector,
        Sampler,
        Image,
        SampledImage,
        AccelerationStructure
    };

    BaseType baseType = BaseType::Void;
    uint32_t width = 0;            // Bit width for scalars (32, 64, etc.)
    uint32_t vecSize = 1;          // Vector component count (1-4)
    uint32_t columns = 1;          // Matrix column count
    uint32_t rows = 1;             // Matrix row count
    uint32_t arraySize = 0;        // Array size (0 = not an array)
    std::string structName;        // Struct type name (if baseType == Struct)

    // Size in bytes (for buffer layout)
    uint32_t sizeInBytes = 0;
    uint32_t alignment = 0;

    /**
     * @brief Get C++ type string for code generation
     */
    std::string ToCppType() const;

    /**
     * @brief Get GLSL type string for documentation
     */
    std::string ToGlslType() const;
};

/**
 * @brief Struct member information
 *
 * Describes a member within a shader struct (UBO/SSBO).
 */
struct SpirvStructMember {
    std::string name;
    SpirvTypeInfo type;
    uint32_t offset = 0;           // Byte offset within struct
    uint32_t arrayStride = 0;      // For arrays: stride between elements
    uint32_t matrixStride = 0;     // For matrices: stride between columns
    bool isRowMajor = false;       // Matrix layout
};

/**
 * @brief Complete struct definition
 *
 * Represents a shader struct type (typically UBO/SSBO).
 */
struct SpirvStructDefinition {
    std::string name;
    std::vector<SpirvStructMember> members;
    uint32_t sizeInBytes = 0;
    uint32_t alignment = 0;
};

/**
 * @brief Descriptor binding with full type information
 *
 * Extends DescriptorBindingSpec with detailed type data for code generation.
 */
struct SpirvDescriptorBinding {
    uint32_t set = 0;
    uint32_t binding = 0;
    std::string name;
    VkDescriptorType descriptorType;
    uint32_t descriptorCount = 1;
    VkShaderStageFlags stageFlags = 0;

    // Type information
    SpirvTypeInfo typeInfo;

    // For UBO/SSBO: index into SpirvReflectionData::structDefinitions (-1 = none)
    int structDefIndex = -1;

    // For images/samplers: format and dimension info
    VkFormat imageFormat = VK_FORMAT_UNDEFINED;
    uint32_t imageDimension = 0;  // 1D, 2D, 3D, Cube
};

/**
 * @brief Push constant range with type information
 */
struct SpirvPushConstantRange {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
    VkShaderStageFlags stageFlags = 0;

    // Struct definition for the push constant block
    SpirvStructDefinition structDef;
};

/**
 * @brief Vertex input attribute
 */
struct SpirvVertexInput {
    uint32_t location = 0;
    std::string name;
    VkFormat format = VK_FORMAT_UNDEFINED;
    SpirvTypeInfo type;
};

/**
 * @brief Vertex output / Fragment input attribute
 */
struct SpirvStageIO {
    uint32_t location = 0;
    std::string name;
    SpirvTypeInfo type;
};

/**
 * @brief Specialization constant
 */
struct SpirvSpecializationConstant {
    uint32_t constantId = 0;
    std::string name;
    SpirvTypeInfo type;
    uint32_t defaultValue = 0;  // For integers/floats stored as uint32_t
};

/**
 * @brief Complete SPIRV reflection data for a shader program
 *
 * Contains all metadata extracted from SPIRV reflection.
 * Used to generate strongly-typed SDI header files.
 */
struct SpirvReflectionData {
    // Program metadata
    std::string programName;
    PipelineTypeConstraint pipelineType;
    std::vector<ShaderStage> stages;

    // Descriptor bindings (organized by set)
    std::unordered_map<uint32_t, std::vector<SpirvDescriptorBinding>> descriptorSets;

    // Push constants
    std::vector<SpirvPushConstantRange> pushConstants;

    // Vertex inputs (for vertex shaders)
    std::vector<SpirvVertexInput> vertexInputs;

    // Stage inputs/outputs (for inter-stage communication)
    std::unordered_map<ShaderStage, std::vector<SpirvStageIO>> stageInputs;
    std::unordered_map<ShaderStage, std::vector<SpirvStageIO>> stageOutputs;

    // Specialization constants
    std::vector<SpirvSpecializationConstant> specializationConstants;

    // Struct definitions (referenced by descriptors/push constants)
    std::vector<SpirvStructDefinition> structDefinitions;

    // Computed hash for validation (SHA-256 of SPIRV bytecode)
    std::string interfaceHash;

    // Entry points per stage
    std::unordered_map<ShaderStage, std::string> entryPoints;
};

} // namespace ShaderManagement
