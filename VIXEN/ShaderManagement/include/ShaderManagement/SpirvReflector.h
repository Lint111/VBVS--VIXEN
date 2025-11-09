#pragma once

#include "SpirvReflectionData.h"
#include "ShaderProgram.h"
#include "../../logger/ILoggable.h"
#include <memory>

// Forward declare SPIRV-Reflect types from global namespace
struct SpvReflectShaderModule;
struct SpvReflectTypeDescription;

namespace ShaderManagement {

/**
 * @brief SPIRV reflection utility
 *
 * Extracts comprehensive metadata from SPIRV bytecode including:
 * - Descriptor bindings with full type information
 * - Push constants with struct layouts
 * - Vertex inputs/outputs
 * - Specialization constants
 * - Struct definitions
 *
 * Uses SPIRV-Reflect library for analysis.
 * Pure function - no state, no Vulkan objects.
 */
class SpirvReflector : public ILoggable {
public:
    /**
     * @brief Reflect complete shader program interface
     *
     * Analyzes all stages in the program and extracts metadata.
     * Merges information across stages (e.g., descriptor bindings).
     *
     * @param program Compiled shader program with SPIRV bytecode
     * @return Complete reflection data, or nullptr on error
     */
    static std::unique_ptr<SpirvReflectionData> Reflect(const CompiledProgram& program);

    /**
     * @brief Reflect single shader stage
     *
     * Extracts metadata from a single SPIRV module.
     * Useful for incremental analysis.
     *
     * @param spirvCode SPIRV bytecode
     * @param stage Shader stage
     * @return Stage reflection data, or nullptr on error
     */
    static std::unique_ptr<SpirvReflectionData> ReflectStage(
        const std::vector<uint32_t>& spirvCode,
        ShaderStage stage
    );

    /**
     * @brief Compute interface hash from SPIRV bytecode
     *
     * Generates SHA-256 hash of all SPIRV code in the program.
     * Used for validation and cache invalidation.
     *
     * @param program Compiled shader program
     * @return Hex-encoded SHA-256 hash
     */
    static std::string ComputeInterfaceHash(const CompiledProgram& program);

    /**
     * @brief Validate that two reflection data structures are compatible
     *
     * Checks if descriptor layouts, push constants, etc. match.
     * Useful for hot-reload validation.
     *
     * @param a First reflection data
     * @param b Second reflection data
     * @return True if compatible, false otherwise
     */
    static bool AreInterfacesCompatible(
        const SpirvReflectionData& a,
        const SpirvReflectionData& b
    );

private:
    // Internal helpers
    static void ReflectDescriptors(
        struct ::SpvReflectShaderModule& module,
        ShaderStage stage,
        SpirvReflectionData& data
    );

    static void ReflectPushConstants(
        struct ::SpvReflectShaderModule& module,
        ShaderStage stage,
        SpirvReflectionData& data
    );

    static void ReflectVertexInputs(
        struct ::SpvReflectShaderModule& module,
        SpirvReflectionData& data
    );

    static void ReflectStageInputsOutputs(
        struct ::SpvReflectShaderModule& module,
        ShaderStage stage,
        SpirvReflectionData& data
    );

    static void ReflectSpecializationConstants(
        struct ::SpvReflectShaderModule& module,
        SpirvReflectionData& data
    );

    static SpirvTypeInfo ConvertTypeInfo(const struct ::SpvReflectTypeDescription* typeDesc);
    static SpirvStructDefinition ConvertStructDefinition(const struct ::SpvReflectTypeDescription* typeDesc);
    static void MergeReflectionData(SpirvReflectionData& dest, const SpirvReflectionData& src);
};

} // namespace ShaderManagement
