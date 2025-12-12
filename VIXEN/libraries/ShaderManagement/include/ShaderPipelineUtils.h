#pragma once

#include "ShaderStage.h"
#include "DescriptorLayoutSpec.h"  // For PipelineTypeConstraint
#include <filesystem>
#include <vector>
#include <string>
#include <optional>
#include <unordered_set>

namespace ShaderManagement {

/**
 * @brief Result of pipeline type detection from shader files
 */
struct PipelineDetectionResult {
    PipelineTypeConstraint type = PipelineTypeConstraint::Graphics;
    std::string reason;
    bool confident = false;
};

/**
 * @brief Expected/optional shader extensions for a pipeline type
 */
struct PipelineExtensions {
    std::vector<std::string> required;   ///< At least one of these must exist
    std::vector<std::string> optional;   ///< Will be included if found
};

/**
 * @brief Utility class for shader pipeline detection and validation
 *
 * Provides common functionality for:
 * - Detecting shader stage from file extensions
 * - Detecting pipeline type from shader files
 * - Discovering sibling shader files
 * - Validating pipeline stage requirements
 *
 * This is the single source of truth for pipeline-related utilities,
 * used by both the sdi_tool and runtime shader management.
 */
class ShaderPipelineUtils {
public:
    /**
     * @brief Detect shader stage from file extension
     *
     * Maps file extensions to shader stages:
     * - .vert -> Vertex
     * - .frag -> Fragment
     * - .comp -> Compute
     * - .rgen -> RayGen
     * - etc.
     *
     * @param path File path (only extension is used)
     * @return Shader stage, or nullopt if extension is unknown
     */
    static std::optional<ShaderStage> DetectStageFromPath(const std::filesystem::path& path);

    /**
     * @brief Detect pipeline type from a single file extension
     *
     * Maps extensions to pipeline types:
     * - .comp -> Compute
     * - .rgen/.rmiss/.rchit/etc -> RayTracing
     * - .mesh/.task -> Mesh
     * - .vert/.frag/etc -> Graphics
     *
     * @param extension File extension (with or without leading dot)
     * @return Pipeline type, or nullopt if unknown
     */
    static std::optional<PipelineTypeConstraint> DetectPipelineFromExtension(const std::string& extension);

    /**
     * @brief Detect pipeline type from multiple shader files
     *
     * Analyzes all input files and determines the pipeline type with priority:
     * 1. Ray tracing stages -> RayTracing (highest priority)
     * 2. Mesh/Task stages -> Mesh
     * 3. Compute stage alone -> Compute
     * 4. Traditional stages -> Graphics (default)
     *
     * @param files List of shader file paths
     * @return Detection result with type, reason, and confidence
     */
    static PipelineDetectionResult DetectPipelineFromFiles(const std::vector<std::string>& files);

    /**
     * @brief Get expected extensions for a pipeline type
     *
     * Returns required and optional shader extensions for each pipeline type:
     * - Graphics: required={.vert,.frag}, optional={.geom,.tesc,.tese}
     * - Compute: required={.comp}
     * - RayTracing: required={.rgen}, optional={.rmiss,.rchit,.rahit,.rint,.rcall}
     * - Mesh: required={.mesh}, optional={.task,.frag}
     *
     * @param pipelineType Pipeline type
     * @return Extensions struct with required and optional lists
     */
    static PipelineExtensions GetPipelineExtensions(PipelineTypeConstraint pipelineType);

    /**
     * @brief Discover sibling shader files based on naming convention
     *
     * Given a shader file like "VoxelRT.rgen", looks for sibling files:
     * - VoxelRT.rmiss, VoxelRT.rchit, VoxelRT.rint, etc.
     *
     * @param inputFiles Current list of input files (will be expanded in-place)
     * @param pipelineType Detected pipeline type (determines which extensions to search)
     * @return Number of files discovered and added
     */
    static uint32_t DiscoverSiblingShaders(
        std::vector<std::string>& inputFiles,
        PipelineTypeConstraint pipelineType
    );

    /**
     * @brief Validate that required shader stages are present
     *
     * Checks if at least one required stage for the pipeline type is present.
     *
     * @param inputFiles List of shader file paths
     * @param pipelineType Pipeline type to validate against
     * @return Error message if validation fails, empty string if OK
     */
    static std::string ValidatePipelineStages(
        const std::vector<std::string>& inputFiles,
        PipelineTypeConstraint pipelineType
    );

    /**
     * @brief Get human-readable name for pipeline type
     */
    static const char* GetPipelineTypeName(PipelineTypeConstraint type);
};

} // namespace ShaderManagement
