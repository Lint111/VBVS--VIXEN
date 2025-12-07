#pragma once

#include "SpirvReflectionData.h"
#include "ILoggable.h"
#include <filesystem>
#include <string>
#include <unordered_set>
#include <fstream>

namespace ShaderManagement {

/**
 * @brief Configuration for SDI code generation
 */
struct SdiGeneratorConfig {
    std::filesystem::path outputDirectory = "./generated/sdi";
    std::string namespacePrefix = "ShaderInterface";
    bool generateComments = true;
    bool generateLayoutInfo = true;      // Include memory layout comments
    bool generateAccessorHelpers = false; // Generate helper functions for descriptor access
    bool prettyPrint = true;             // Format with indentation
};

/**
 * @brief SPIRV Descriptor Interface (SDI) code generator
 *
 * Generates C++ header files containing strongly-typed shader interfaces.
 * Format: {UUID}-SDI.h
 *
 * Generated headers include:
 * - Struct definitions matching UBO/SSBO layouts
 * - Push constant structs
 * - Vertex input/output structs
 * - Descriptor binding constants
 * - Type-safe accessor classes
 * - Interface hash for validation
 *
 * Purpose:
 * - Provide IDE-time autocompletion for shader resources
 * - Compile-time type checking when accessing shader data
 * - Prevent shader/C++ interface mismatches
 *
 * Example usage:
 * @code
 * SpirvInterfaceGenerator generator(config);
 * SpirvReflector reflector;
 * auto reflectionData = reflector.Reflect(program);
 * std::string uuid = "abc123";  // From shader program
 * std::string filePath = generator.Generate(uuid, *reflectionData);
 * // Include generated file: #include "abc123-SDI.h"
 * @endcode
 */
class SpirvInterfaceGenerator : public ILoggable {
public:
    explicit SpirvInterfaceGenerator(const SdiGeneratorConfig& config = {});

    /**
     * @brief Generate SDI header file from reflection data
     *
     * Creates a C++ header file with strongly-typed shader interface.
     * File naming: {uuid}-SDI.h
     *
     * @param uuid Unique identifier for the shader program
     * @param reflectionData Reflected SPIRV metadata
     * @return Absolute path to generated file, or empty string on error
     */
    std::string Generate(
        const std::string& uuid,
        const SpirvReflectionData& reflectionData
    );

    /**
     * @brief Generate SDI header to string (without writing to disk)
     *
     * Useful for testing or in-memory code generation.
     *
     * @param uuid Unique identifier for the shader program
     * @param reflectionData Reflected SPIRV metadata
     * @return Generated C++ code as string
     */
    std::string GenerateToString(
        const std::string& uuid,
        const SpirvReflectionData& reflectionData
    );

    /**
     * @brief Delete SDI file for a given UUID
     *
     * Removes generated header when shader is removed.
     *
     * @param uuid Shader program UUID
     * @return True if file was deleted, false if it didn't exist
     */
    bool DeleteSdi(const std::string& uuid);

    /**
     * @brief Check if SDI file exists for UUID
     *
     * @param uuid Shader program UUID
     * @return True if SDI file exists
     */
    bool SdiExists(const std::string& uuid) const;

    /**
     * @brief Get path to SDI file for UUID
     *
     * @param uuid Shader program UUID
     * @return Absolute path to SDI file (may not exist)
     */
    std::filesystem::path GetSdiPath(const std::string& uuid) const;

    /**
     * @brief Generate shader-specific Names.h file
     *
     * Creates {programName}Names.h with shader-specific constexpr constants
     * and type aliases that map to the generic .si.h interface.
     *
     * @param programName Name of the shader program (e.g., "Draw_Shader")
     * @param uuid UUID of the shader (for namespace lookup)
     * @param reflectionData Reflected SPIRV metadata
     * @return Path to generated Names.h file, or empty string on error
     */
    std::string GenerateNamesHeader(
        const std::string& programName,
        const std::string& uuid,
        const SpirvReflectionData& reflectionData
    );

private:
    SdiGeneratorConfig config_;

    // Code generation helpers
    std::string GenerateHeader(const std::string& uuid, const SpirvReflectionData& data);
    std::string GenerateNamespaceBegin(const std::string& uuid);
    std::string GenerateNamespaceEnd();
    std::string GenerateStructDefinitions(const SpirvReflectionData& data);
    std::string GenerateStructDefinition(const SpirvStructDefinition& structDef);
    std::string GenerateDescriptorInfo(const SpirvReflectionData& data);
    std::string GeneratePushConstantInfo(const SpirvReflectionData& data);
    std::string GenerateVertexInputInfo(const SpirvReflectionData& data);
    std::string GenerateMetadata(const SpirvReflectionData& data);
    std::string GenerateInterfaceHashValidator(const SpirvReflectionData& data);
    std::string GenerateAccessorClass(const SpirvReflectionData& data);
    std::string Indent(uint32_t level) const;
};

/**
 * @brief SDI file manager
 *
 * Manages the lifecycle of generated SDI files.
 * Tracks which UUIDs have generated files and provides cleanup utilities.
 */
class SdiFileManager {
public:
    explicit SdiFileManager(const std::filesystem::path& sdiDirectory);

    /**
     * @brief Register a generated SDI file
     *
     * Adds UUID to the tracking database.
     *
     * @param uuid Shader program UUID
     * @param filePath Path to generated SDI file
     */
    void RegisterSdi(const std::string& uuid, const std::filesystem::path& filePath);

    /**
     * @brief Unregister and optionally delete SDI file
     *
     * @param uuid Shader program UUID
     * @param deleteFile If true, delete the file from disk
     * @return True if unregistered, false if UUID wasn't found
     */
    bool UnregisterSdi(const std::string& uuid, bool deleteFile = true);

    /**
     * @brief Get all registered SDI UUIDs
     *
     * @return Vector of UUIDs with generated SDI files
     */
    std::vector<std::string> GetRegisteredUuids() const;

    /**
     * @brief Clean up orphaned SDI files (registry-based)
     *
     * Removes SDI files that aren't in the registered set.
     * Useful after shader program deletions.
     *
     * @return Number of orphaned files deleted
     */
    uint32_t CleanupOrphans();

    /**
     * @brief Clean up orphaned SDI files (naming-file-based)
     *
     * Scans all *Names.h files in the directory, extracts the UUIDs they reference,
     * and deletes any *-SDI.h files not referenced by any naming file.
     * This is more robust than registry-based cleanup as it uses actual file references.
     *
     * @param verbose If true, populate referencedUuids and orphanedFiles output params
     * @param referencedUuids Optional output: UUIDs found in naming files
     * @param orphanedFiles Optional output: Paths of deleted orphaned files
     * @return Number of orphaned files deleted
     */
    uint32_t CleanupOrphanedSdis(
        bool verbose = false,
        std::vector<std::string>* referencedUuids = nullptr,
        std::vector<std::filesystem::path>* orphanedFiles = nullptr
    );

    /**
     * @brief Extract UUID from an SDI include directive
     *
     * Parses lines like: #include "2744040dfb644549-SDI.h"
     *
     * @param includeLine The line containing the include directive
     * @return UUID string if found, empty string otherwise
     */
    static std::string ExtractSdiUuidFromInclude(const std::string& includeLine);

    /**
     * @brief Scan naming files and return referenced SDI UUIDs
     *
     * @return Set of UUIDs referenced by *Names.h files
     */
    std::unordered_set<std::string> GetReferencedUuids() const;

    /**
     * @brief Delete all SDI files
     *
     * Clears the entire SDI directory.
     *
     * @return Number of files deleted
     */
    uint32_t DeleteAll();

    /**
     * @brief Get path to SDI file for UUID
     *
     * @param uuid Shader program UUID
     * @return Path to SDI file, or empty if not registered
     */
    std::filesystem::path GetSdiPath(const std::string& uuid) const;

    /**
     * @brief Check if UUID is registered
     *
     * @param uuid Shader program UUID
     * @return True if registered
     */
    bool IsRegistered(const std::string& uuid) const;

private:
    std::filesystem::path sdiDirectory_;
    std::unordered_map<std::string, std::filesystem::path> registeredSdis_;

    void LoadRegistry();
    void SaveRegistry();
};

} // namespace ShaderManagement
