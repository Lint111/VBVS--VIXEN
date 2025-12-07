#pragma once

#include "ShaderDataBundle.h"
#include <filesystem>
#include <optional>
#include <string>
#include <functional>

namespace ShaderManagement {

/**
 * @brief Configuration for bundle serialization
 */
struct BundleSerializerConfig {
    /**
     * @brief Embed SPIRV directly in JSON instead of separate .spv files
     *
     * Advantages:
     * - No orphaned .spv files to track
     * - Single file per shader bundle
     *
     * Disadvantages:
     * - Larger JSON files (base64 encoding)
     * - Slower to load individual stages
     */
    bool embedSpirv = false;

    /**
     * @brief Callback invoked when a SPIRV file is written
     *
     * Used by FileManifest to track generated files for cleanup.
     * Called with the absolute path to the written file.
     */
    std::function<void(const std::filesystem::path&)> onFileWritten;
};

/**
 * @brief Serializes ShaderDataBundle to/from JSON format
 *
 * Handles:
 * - Saving bundles with either embedded or external SPIRV
 * - Loading bundles and reconstructing SPIRV data
 * - Quick UUID extraction for cleanup operations
 *
 * JSON Format:
 * @code
 * {
 *   "uuid": "abc123...",
 *   "programName": "MyShader",
 *   "pipelineType": 0,
 *   "descriptorInterfaceHash": "def456...",
 *   "sdiHeaderPath": "generated/sdi/abc123-SDI.h",
 *   "sdiNamespace": "SDI::MyShader",
 *   "stages": [
 *     {
 *       "stage": 0,
 *       "entryPoint": "main",
 *       "spirvSize": 1234,
 *       "spirvFile": "abc123_stage0.spv"  // or "spirvData": [...]
 *     }
 *   ]
 * }
 * @endcode
 */
class ShaderBundleSerializer {
public:
    /**
     * @brief Save bundle to JSON file
     *
     * @param bundle Bundle to serialize
     * @param outputPath Path for JSON file
     * @param config Serialization options
     * @return True on success, false on error (message to stderr)
     */
    static bool SaveToJson(
        const ShaderDataBundle& bundle,
        const std::filesystem::path& outputPath,
        const BundleSerializerConfig& config = {}
    );

    /**
     * @brief Load bundle from JSON file
     *
     * @param jsonPath Path to JSON file
     * @param outBundle Bundle to populate
     * @return True on success, false on error (message to stderr)
     */
    static bool LoadFromJson(
        const std::filesystem::path& jsonPath,
        ShaderDataBundle& outBundle
    );

    /**
     * @brief Load only the UUID from a bundle JSON file
     *
     * Fast extraction without loading full bundle data.
     * Useful for cleanup operations that need to detect UUID changes.
     *
     * @param jsonPath Path to JSON file
     * @return UUID if file exists and is valid, empty string otherwise
     */
    static std::string LoadUuid(const std::filesystem::path& jsonPath);

    /**
     * @brief Check if a JSON file contains a valid bundle
     *
     * Quick validation without full deserialization.
     *
     * @param jsonPath Path to JSON file
     * @return True if file exists and appears to be a valid bundle
     */
    static bool IsValidBundle(const std::filesystem::path& jsonPath);
};

} // namespace ShaderManagement
