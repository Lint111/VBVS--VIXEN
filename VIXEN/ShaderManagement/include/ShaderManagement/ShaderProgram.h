#pragma once

#include <system_error>
#include "ShaderStage.h"
#include "DescriptorLayoutSpec.h"
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <chrono>

namespace ShaderManagement {

/**
 * @brief Shader stage definition (input to library)
 *
 * Describes a single shader stage to be loaded and compiled.
 * No Vulkan objects - just paths and metadata.
 */
struct ShaderStageDefinition {
    ShaderStage stage;
    std::filesystem::path spirvPath;
    std::string entryPoint = "main";

    // Optional specialization constants (constantId -> value)
    std::unordered_map<uint32_t, uint32_t> specializationConstants;

    // File watching metadata (managed by library)
    std::filesystem::file_time_type lastModified;
    bool needsRecompile = false;
};

/**
 * @brief Shader program definition (input to library)
 *
 * Collection of shader stages forming a complete program.
 * Validated against pipeline type constraints.
 */
struct ShaderProgramDefinition {
    uint32_t programId = 0;  // Set by library on registration
    std::string name;        // For debugging/logging
    PipelineTypeConstraint pipelineType;
    std::vector<ShaderStageDefinition> stages;

    /**
     * @brief Check if program has a specific stage
     */
    bool HasStage(ShaderStage stage) const {
        return std::any_of(stages.begin(), stages.end(),
            [stage](const auto& s) { return s.stage == stage; });
    }

    /**
     * @brief Get specific stage definition
     */
    const ShaderStageDefinition* GetStage(ShaderStage stage) const {
        auto it = std::find_if(stages.begin(), stages.end(),
            [stage](const auto& s) { return s.stage == stage; });
        return (it != stages.end()) ? &(*it) : nullptr;
    }

    /**
     * @brief Validate stage requirements for pipeline type
     */
    bool IsValid() const;
};

/**
 * @brief Compiled shader stage (output from library)
 *
 * Contains compiled SPIRV bytecode, NO VkShaderModule.
 * Graph side creates Vulkan objects from this.
 */
struct CompiledShaderStage {
    ShaderStage stage;
    std::vector<uint32_t> spirvCode;  // Raw SPIRV bytecode
    std::string entryPoint;

    // Specialization constants (for VkSpecializationInfo creation)
    std::vector<uint32_t> specializationConstantIds;
    std::vector<uint32_t> specializationConstantValues;

    // Generation tracking (increments on recompilation)
    uint64_t generation = 0;
};

/**
 * @brief Compiled shader program (output from library)
 *
 * Result of successful compilation. Contains SPIRV bytecode for all stages.
 * No Vulkan objects - device-agnostic.
 */
struct CompiledProgram {
    uint32_t programId;
    std::string name;
    PipelineTypeConstraint pipelineType;
    std::vector<CompiledShaderStage> stages;

    // Generation tracking (increments when any stage recompiles)
    uint64_t generation = 0;

    // Compilation timestamp
    std::chrono::steady_clock::time_point compiledAt;

    // Reflected descriptor layout (extracted from SPIRV via SPIRV-Reflect)
    // Populated automatically during compilation - merges all shader stages
    DescriptorLayoutSpec* descriptorLayout = nullptr;

    /**
     * @brief Get compiled stage by type
     */
    const CompiledShaderStage* GetStage(ShaderStage stage) const {
        auto it = std::find_if(stages.begin(), stages.end(),
            [stage](const auto& s) { return s.stage == stage; });
        return (it != stages.end()) ? &(*it) : nullptr;
    }

    /**
     * @brief Check if program has a specific stage
     */
    bool HasStage(ShaderStage stage) const {
        return GetStage(stage) != nullptr;
    }
};

/**
 * @brief Compilation status
 */
enum class CompilationStatus {
    NotCompiled,    // Program registered but not yet compiled
    Pending,        // Queued for compilation
    Compiling,      // Currently compiling (background thread)
    Completed,      // Compilation successful
    Failed,         // Compilation failed (check error message)
};

/**
 * @brief Compilation result
 *
 * Returned from background compilation jobs.
 */
struct CompilationResult {
    uint32_t programId;
    CompilationStatus status;
    CompiledProgram program;  // Valid if status == Completed
    std::string errorMessage; // Valid if status == Failed
    std::chrono::milliseconds compilationTime;
};

// ===== Validation Implementation =====

inline bool ShaderProgramDefinition::IsValid() const {
    switch (pipelineType) {
        case PipelineTypeConstraint::Graphics:
            // Vertex + Fragment required
            return HasStage(ShaderStage::Vertex) &&
                   HasStage(ShaderStage::Fragment);

        case PipelineTypeConstraint::Mesh:
            // Mesh + Fragment required
            return HasStage(ShaderStage::Mesh) &&
                   HasStage(ShaderStage::Fragment);

        case PipelineTypeConstraint::Compute:
            // Compute only
            return stages.size() == 1 &&
                   HasStage(ShaderStage::Compute);

        case PipelineTypeConstraint::RayTracing:
            // RayGen + Miss + ClosestHit required
            return HasStage(ShaderStage::RayGen) &&
                   HasStage(ShaderStage::Miss) &&
                   HasStage(ShaderStage::ClosestHit);

        default:
            return false;
    }
}

} // namespace ShaderManagement