#pragma once

#include "ShaderProgram.h"
#include "SpirvReflectionData.h"
#include "DescriptorLayoutSpec.h"
#include "ShaderDirtyFlags.h"
#include <filesystem>
#include <string>
#include <memory>

namespace ShaderManagement {

/**
 * @brief Complete shader data bundle
 *
 * A unified package containing everything needed to work with a shader:
 * - Compiled SPIRV bytecode
 * - Reflected metadata and type information
 * - Descriptor layout specifications
 * - Generated SDI (SPIRV Descriptor Interface) header reference
 * - Unique identifier for tracking
 *
 * **IMPORTANT**: This struct is MOVE-ONLY to prevent accidental copies of
 * large SPIRV data (potentially megabytes). Use std::move() when transferring.
 *
 * This is the primary output from ShaderBundleBuilder and provides
 * a single, cohesive interface for accessing all shader-related data.
 *
 * Usage:
 * @code
 * auto result = ShaderBundleBuilder()
 *     .SetSource(source)
 *     .SetStage(ShaderStage::Fragment)
 *     .Build();
 *
 * if (result) {
 *     // Take ownership via unique_ptr (already move-only)
 *     auto bundle = std::move(result.bundle);
 *
 *     // Or move into container
 *     myShaders.push_back(std::move(*result.bundle));
 *
 *     // Access SPIRV bytecode for Vulkan
 *     const auto& spirv = bundle->GetSpirv(ShaderStage::Fragment);
 * }
 * @endcode
 */
struct ShaderDataBundle {
    // ===== Move-Only Semantics =====

    // Allow default construction
    ShaderDataBundle() = default;

    // Delete copy operations (prevents accidental copies of megabytes of SPIRV data)
    ShaderDataBundle(const ShaderDataBundle&) = delete;
    ShaderDataBundle& operator=(const ShaderDataBundle&) = delete;

    // Allow move operations (efficient transfer of ownership)
    ShaderDataBundle(ShaderDataBundle&&) noexcept = default;
    ShaderDataBundle& operator=(ShaderDataBundle&&) noexcept = default;

    // Default destructor
    ~ShaderDataBundle() = default;

    // ===== Data Members =====

    /**
     * @brief Compiled shader program with SPIRV bytecode
     *
     * Contains all shader stages with compiled SPIRV code.
     * Device-agnostic - no VkShaderModule.
     * WARNING: May contain megabytes of data - use move semantics!
     */
    CompiledProgram program;

    /**
     * @brief Complete SPIRV reflection data
     *
     * Full metadata extracted from SPIRV including:
     * - Descriptor bindings with type information
     * - Push constants with struct layouts
     * - Vertex inputs/outputs
     * - Struct definitions
     * - Specialization constants
     */
    std::shared_ptr<SpirvReflectionData> reflectionData;

    /**
     * @brief Descriptor layout specification
     *
     * Vulkan-compatible descriptor set layout specification.
     * Can be used to create VkDescriptorSetLayout.
     */
    std::shared_ptr<DescriptorLayoutSpec> descriptorLayout;

    /**
     * @brief Unique identifier for this shader bundle
     *
     * Used as the filename prefix for SDI generation: {uuid}-SDI.h
     * Typically a content-based hash or GUID.
     */
    std::string uuid;

    /**
     * @brief Path to generated SDI header file
     *
     * Absolute path to the {uuid}-SDI.h file.
     * Include this in C++ code for type-safe shader access.
     */
    std::filesystem::path sdiHeaderPath;

    /**
     * @brief SDI namespace for this shader
     *
     * Fully qualified namespace containing type-safe constants.
     * Format: "{namespacePrefix}::{sanitized_uuid}"
     * Example: "ShaderInterface::my_shader_abc123"
     */
    std::string sdiNamespace;

    /**
     * @brief Generation timestamp
     *
     * When this bundle was created.
     * Useful for tracking and debugging.
     */
    std::chrono::system_clock::time_point createdAt;

    /**
     * @brief Descriptor-only interface hash
     *
     * Hash based ONLY on descriptor layout, NOT on:
     * - Program name, UUID, or unique identifiers
     * - Timestamp or file paths
     *
     * Includes:
     * - Descriptor sets, bindings, types
     * - Push constant layouts
     * - Vertex input formats
     * - Struct member layouts (types, offsets, names)
     * - Variable names
     *
     * Purpose: Two shaders with identical descriptor layout get the same hash,
     * enabling descriptor set sharing and smart hot-reload detection.
     */
    std::string descriptorInterfaceHash;

    /**
     * @brief Dirty flags for hot-reload tracking
     *
     * Indicates what changed compared to a previous version.
     * Used to determine safe hot-reload operations:
     * - SPIRV only → Safe hot-swap
     * - Descriptors changed → May need pipeline rebuild
     * - Vertex inputs changed → Must rebuild pipeline
     *
     * Set by CompareBundles() function.
     */
    ShaderDirtyFlags dirtyFlags = ShaderDirtyFlags::None;

    // ===== Convenience Accessors =====

    /**
     * @brief Get SPIRV bytecode for a specific stage
     *
     * @param stage Shader stage to retrieve
     * @return SPIRV bytecode, or empty vector if stage not found
     */
    const std::vector<uint32_t>& GetSpirv(ShaderStage stage) const {
        static const std::vector<uint32_t> empty;
        const auto* stageData = program.GetStage(stage);
        return stageData ? stageData->spirvCode : empty;
    }

    /**
     * @brief Get entry point name for a specific stage
     *
     * @param stage Shader stage
     * @return Entry point name (typically "main")
     */
    std::string GetEntryPoint(ShaderStage stage) const {
        const auto* stageData = program.GetStage(stage);
        return stageData ? stageData->entryPoint : "main";
    }

    /**
     * @brief Get SDI include path for C++ code
     *
     * Returns a string suitable for #include directive.
     * Example: "generated/sdi/abc123-SDI.h"
     *
     * @return Relative or absolute path to SDI header
     */
    std::string GetSdiIncludePath() const {
        return sdiHeaderPath.string();
    }

    /**
     * @brief Get SDI namespace for using directive
     *
     * Returns the namespace string for easy access.
     * Example: "ShaderInterface::my_shader_abc123"
     *
     * Usage:
     * @code
     * using namespace bundle.GetSdiNamespace();
     * // Now can access: Set0::MaterialBuffer::BINDING
     * @endcode
     */
    const std::string& GetSdiNamespace() const {
        return sdiNamespace;
    }

    /**
     * @brief Get descriptor bindings for a specific set
     *
     * @param setIndex Descriptor set index (0, 1, 2, ...)
     * @return Vector of bindings in that set, or empty if not found
     */
    std::vector<SpirvDescriptorBinding> GetDescriptorSet(uint32_t setIndex) const {
        if (!reflectionData) return {};

        auto it = reflectionData->descriptorSets.find(setIndex);
        if (it != reflectionData->descriptorSets.end()) {
            return it->second;
        }
        return {};
    }

    /**
     * @brief Get all push constant ranges
     *
     * @return Vector of push constant ranges
     */
    const std::vector<SpirvPushConstantRange>& GetPushConstants() const {
        static const std::vector<SpirvPushConstantRange> empty;
        return reflectionData ? reflectionData->pushConstants : empty;
    }

    /**
     * @brief Get vertex input attributes
     *
     * Only populated for vertex shaders.
     *
     * @return Vector of vertex input attributes
     */
    const std::vector<SpirvVertexInput>& GetVertexInputs() const {
        static const std::vector<SpirvVertexInput> empty;
        return reflectionData ? reflectionData->vertexInputs : empty;
    }

    /**
     * @brief Get interface hash for validation
     *
     * SHA-256 hash of all SPIRV bytecode.
     * Use to detect interface changes during hot-reload.
     *
     * @return Hex-encoded hash string
     */
    const std::string& GetInterfaceHash() const {
        static const std::string empty;
        return reflectionData ? reflectionData->interfaceHash : empty;
    }

    /**
     * @brief Validate that runtime SPIRV matches this bundle's interface
     *
     * Compares interface hashes to ensure compatibility.
     * Useful for hot-reload validation.
     *
     * @param runtimeHash Hash computed from runtime SPIRV
     * @return True if compatible, false if interface has changed
     */
    bool ValidateInterface(const std::string& runtimeHash) const {
        return reflectionData && reflectionData->interfaceHash == runtimeHash;
    }

    /**
     * @brief Check if SDI header exists on disk
     *
     * @return True if SDI file is present
     */
    bool HasValidSdi() const {
        return !sdiHeaderPath.empty() && std::filesystem::exists(sdiHeaderPath);
    }

    /**
     * @brief Get program name for debugging/logging
     *
     * @return Human-readable shader program name
     */
    const std::string& GetProgramName() const {
        return program.name;
    }

    /**
     * @brief Get pipeline type constraint
     *
     * @return Pipeline type (Graphics, Compute, RayTracing, etc.)
     */
    PipelineTypeConstraint GetPipelineType() const {
        return program.pipelineType;
    }

    /**
     * @brief Get number of shader stages
     *
     * @return Count of stages in this program
     */
    size_t GetStageCount() const {
        return program.stages.size();
    }

    /**
     * @brief Check if bundle has a specific shader stage
     *
     * @param stage Stage to check
     * @return True if stage is present
     */
    bool HasStage(ShaderStage stage) const {
        return program.HasStage(stage);
    }

    /**
     * @brief Get all shader stages in this bundle
     *
     * @return Vector of shader stages
     */
    std::vector<ShaderStage> GetStages() const {
        std::vector<ShaderStage> stages;
        stages.reserve(program.stages.size());
        for (const auto& stage : program.stages) {
            stages.push_back(stage.stage);
        }
        return stages;
    }

    /**
     * @brief Check if bundle is valid and complete
     *
     * Verifies that all required data is present.
     *
     * @return True if bundle is valid
     */
    bool IsValid() const {
        return !program.stages.empty() &&
               reflectionData != nullptr &&
               !uuid.empty() &&
               HasValidSdi();
    }

    /**
     * @brief Get bundle age (time since creation)
     *
     * @return Duration since bundle was created
     */
    std::chrono::milliseconds GetAge() const {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - createdAt);
    }

    /**
     * @brief Generate debug information string
     *
     * Returns a multi-line string with bundle details for logging.
     *
     * @return Debug information
     */
    std::string GetDebugInfo() const {
        std::ostringstream oss;
        oss << "ShaderDataBundle '" << program.name << "'\n";
        oss << "  UUID: " << uuid << "\n";
        oss << "  Pipeline Type: " << PipelineTypeName(program.pipelineType) << "\n";
        oss << "  Stages: " << program.stages.size() << "\n";
        for (const auto& stage : program.stages) {
            oss << "    - " << ShaderStageName(stage.stage)
                << " (" << stage.spirvCode.size() << " words)\n";
        }
        oss << "  Descriptor Sets: ";
        if (reflectionData) {
            oss << reflectionData->descriptorSets.size() << "\n";
            for (const auto& [setIdx, bindings] : reflectionData->descriptorSets) {
                oss << "    Set " << setIdx << ": " << bindings.size() << " bindings\n";
            }
        } else {
            oss << "none\n";
        }
        oss << "  SDI: " << (HasValidSdi() ? "Generated" : "Missing") << "\n";
        oss << "  SDI Path: " << sdiHeaderPath << "\n";
        oss << "  Interface Hash: " << GetInterfaceHash().substr(0, 16) << "...\n";
        oss << "  Descriptor Hash: " << descriptorInterfaceHash.substr(0, 16) << "...\n";
        oss << "  Age: " << GetAge().count() << "ms\n";
        return oss.str();
    }

    /**
     * @brief Get hot-reload compatibility with another bundle
     *
     * @param other Bundle to compare against
     * @return Compatibility level
     */
    HotReloadCompatibility GetHotReloadCompatibility(const ShaderDataBundle& other) const {
        return ShaderManagement::GetHotReloadCompatibility(dirtyFlags);
    }

    /**
     * @brief Check if interfaces are identical (descriptor-only hash match)
     *
     * Two shaders with identical descriptor layouts will match.
     * Enables descriptor set sharing across different shader programs.
     *
     * @param other Bundle to compare
     * @return True if descriptor interfaces match
     */
    bool HasIdenticalInterface(const ShaderDataBundle& other) const {
        return descriptorInterfaceHash == other.descriptorInterfaceHash;
    }

    /**
     * @brief Validate descriptor bindings for dangling samplers/textures
     *
     * Checks each descriptor set for:
     * - Samplers without corresponding sampled images (dangling samplers)
     * - Sampled images without corresponding samplers (dangling textures)
     * - Ambiguous naming patterns that prevent automatic pairing
     *
     * Throws std::runtime_error with detailed message if validation fails.
     * Convention: sampler should be named "<textureName>Sampler" (e.g., "colorTexture" + "colorTextureSampler")
     *
     * @throws std::runtime_error If dangling descriptors found
     */
    void ValidateDescriptorPairing() const;

    /**
     * @brief Find sampler binding that pairs with a given texture
     *
     * Searches the same descriptor set for a sampler binding that corresponds
     * to the given texture binding. Uses naming convention:
     * - Texture: "colorTexture" → Sampler: "colorTextureSampler"
     * - Fallback: finds any sampler in the same set
     *
     * @param setIndex Descriptor set index
     * @param textureBinding Texture binding to find sampler for
     * @return Pointer to sampler binding, or nullptr if not found
     */
    const SpirvDescriptorBinding* FindPairedSampler(uint32_t setIndex, const SpirvDescriptorBinding& textureBinding) const;
};

/**
 * @brief Compare two shader bundles and compute dirty flags
 *
 * Determines what changed between old and new bundles.
 * Sets newBundle.dirtyFlags based on differences.
 *
 * @param oldBundle Previous version
 * @param newBundle New version (dirtyFlags will be set)
 * @return Computed dirty flags
 */
ShaderDirtyFlags CompareBundles(
    const ShaderDataBundle& oldBundle,
    ShaderDataBundle& newBundle
);

/**
 * @brief Compute descriptor-only interface hash
 *
 * Hash based ONLY on descriptor layout (generalized, reusable).
 * Two shaders with identical descriptors will have the same hash.
 *
 * Includes:
 * - Descriptor sets/bindings/types
 * - Push constants
 * - Vertex inputs
 * - Struct layouts
 * - Variable names
 *
 * Excludes:
 * - Program name
 * - UUID
 * - Timestamps
 * - SPIRV bytecode
 *
 * @param reflectionData Reflection data to hash
 * @return SHA-256 hash (hex-encoded)
 */
std::string ComputeDescriptorInterfaceHash(const SpirvReflectionData& reflectionData);

} // namespace ShaderManagement
