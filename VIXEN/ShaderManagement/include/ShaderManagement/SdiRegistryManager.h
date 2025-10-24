#pragma once

#include "SpirvInterfaceGenerator.h"
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <vector>
#include <mutex>

namespace ShaderManagement {

/**
 * @brief Shader registration entry for SDI registry
 */
struct SdiRegistryEntry {
    std::string uuid;                          // Shader UUID
    std::string programName;                   // Human-readable name
    std::filesystem::path sdiHeaderPath;       // Path to {uuid}-SDI.h
    std::string sdiNamespace;                  // Full namespace (e.g., "ShaderInterface::uuid")
    std::string aliasName;                     // Convenient alias (e.g., "PBRShader")
    bool isActive = true;                      // Is this shader currently registered?

    std::chrono::system_clock::time_point registeredAt;
    std::chrono::system_clock::time_point lastAccessedAt;
};

/**
 * @brief Central SDI registry manager
 *
 * Manages a central SDI_Registry.h header file that includes only
 * currently registered/active shader SDI headers.
 *
 * **Key Feature**: Dynamic registry that only includes active shaders,
 * reducing compilation time by excluding unused shader interfaces.
 *
 * Generated registry format:
 * @code
 * // SDI_Registry.h (auto-generated)
 * #pragma once
 *
 * // Include only active/registered shader SDI headers
 * #include "abc123-SDI.h"
 * #include "def456-SDI.h"
 *
 * // Convenient namespace aliases
 * namespace Shaders {
 *     namespace PBRShader = ShaderInterface::abc123;
 *     namespace TerrainShader = ShaderInterface::def456;
 * }
 * @endcode
 *
 * Usage:
 * @code
 * // In your C++ code - single include for all shaders
 * #include "generated/sdi/SDI_Registry.h"
 *
 * // Use convenient aliases
 * using namespace Shaders;
 * binding.binding = PBRShader::Set0::MaterialBuffer::BINDING;
 * binding.binding = TerrainShader::Set0::HeightMap::BINDING;
 * @endcode
 */
class SdiRegistryManager {
public:
    /**
     * @brief Registry configuration
     */
    struct Config {
        std::filesystem::path sdiDirectory = "./generated/sdi";
        std::filesystem::path registryHeaderPath = "./generated/sdi/SDI_Registry.h";
        std::string registryNamespace = "Shaders";  // Namespace for aliases
        bool generateAliases = true;                // Create friendly namespace aliases
        bool generateComments = true;               // Include documentation
        bool autoRegenerate = true;                 // Auto-regenerate on changes

        // Optimization: Only regenerate if X shaders added/removed since last gen
        uint32_t regenerationThreshold = 1;
    };

    explicit SdiRegistryManager(const Config& config = {});
    ~SdiRegistryManager();

    // ===== Registration Methods =====

    /**
     * @brief Register a shader SDI in the central registry
     *
     * Adds the shader to the registry and regenerates SDI_Registry.h
     * to include this shader's header.
     *
     * @param entry Registry entry with shader information
     * @return True if registered successfully
     */
    bool RegisterShader(const SdiRegistryEntry& entry);

    /**
     * @brief Unregister a shader SDI from the central registry
     *
     * Marks shader as inactive and regenerates SDI_Registry.h
     * to exclude this shader's header (reduces compilation time).
     *
     * @param uuid Shader UUID to unregister
     * @param deleteFromDisk If true, also delete the individual SDI header
     * @return True if unregistered successfully
     */
    bool UnregisterShader(const std::string& uuid, bool deleteFromDisk = false);

    /**
     * @brief Check if a shader is registered
     *
     * @param uuid Shader UUID
     * @return True if currently registered and active
     */
    bool IsRegistered(const std::string& uuid) const;

    /**
     * @brief Get registration entry for a shader
     *
     * @param uuid Shader UUID
     * @return Entry if found, nullptr otherwise
     */
    const SdiRegistryEntry* GetEntry(const std::string& uuid) const;

    /**
     * @brief Update shader alias name
     *
     * Changes the convenient namespace alias for a shader.
     * Regenerates registry if auto-regenerate is enabled.
     *
     * @param uuid Shader UUID
     * @param aliasName New alias (e.g., "MyShader")
     * @return True if updated successfully
     */
    bool UpdateAlias(const std::string& uuid, const std::string& aliasName);

    // ===== Query Methods =====

    /**
     * @brief Get all registered shader UUIDs
     *
     * @param activeOnly If true, only return active shaders
     * @return Vector of UUIDs
     */
    std::vector<std::string> GetRegisteredUuids(bool activeOnly = true) const;

    /**
     * @brief Get all registry entries
     *
     * @param activeOnly If true, only return active shaders
     * @return Vector of entries
     */
    std::vector<SdiRegistryEntry> GetAllEntries(bool activeOnly = true) const;

    /**
     * @brief Get number of registered shaders
     *
     * @param activeOnly If true, only count active shaders
     * @return Count of registered shaders
     */
    size_t GetRegisteredCount(bool activeOnly = true) const;

    /**
     * @brief Find shader by alias name
     *
     * @param aliasName Alias to search for
     * @return UUID if found, empty string otherwise
     */
    std::string FindByAlias(const std::string& aliasName) const;

    // ===== Registry Generation =====

    /**
     * @brief Regenerate SDI_Registry.h header file
     *
     * Creates a new registry header including only active shaders.
     * Called automatically when shaders are registered/unregistered
     * if autoRegenerate is enabled.
     *
     * @return True if generation succeeded
     */
    bool RegenerateRegistry();

    /**
     * @brief Generate registry to string (without writing to disk)
     *
     * Useful for testing or preview.
     *
     * @return Generated C++ code
     */
    std::string GenerateRegistryToString() const;

    /**
     * @brief Check if registry needs regeneration
     *
     * Based on number of changes since last generation.
     *
     * @return True if should regenerate
     */
    bool NeedsRegeneration() const;

    /**
     * @brief Force regeneration on next change
     */
    void MarkDirty();

    // ===== Maintenance =====

    /**
     * @brief Cleanup inactive shaders from registry
     *
     * Removes entries marked as inactive for longer than specified duration.
     *
     * @param olderThan Duration since last access
     * @return Number of entries removed
     */
    uint32_t CleanupInactive(std::chrono::hours olderThan = std::chrono::hours(24));

    /**
     * @brief Validate registry integrity
     *
     * Checks that all registered SDI files exist on disk.
     * Marks missing entries as inactive.
     *
     * @return Number of invalid entries found
     */
    uint32_t ValidateRegistry();

    /**
     * @brief Clear all registrations
     *
     * Removes all entries and regenerates empty registry.
     *
     * @param deleteFromDisk If true, also delete individual SDI files
     * @return Number of entries cleared
     */
    uint32_t ClearAll(bool deleteFromDisk = false);

    /**
     * @brief Get registry file path
     *
     * @return Path to SDI_Registry.h
     */
    std::filesystem::path GetRegistryPath() const { return config_.registryHeaderPath; }

    /**
     * @brief Get registry statistics
     */
    struct Stats {
        size_t totalRegistered = 0;
        size_t activeShaders = 0;
        size_t inactiveShaders = 0;
        size_t orphanedFiles = 0;
        std::chrono::system_clock::time_point lastRegeneration;
        uint32_t changesSinceRegeneration = 0;
    };

    Stats GetStats() const;

private:
    Config config_;
    mutable std::mutex mutex_;

    // Registry data
    std::unordered_map<std::string, SdiRegistryEntry> entries_;  // uuid -> entry
    std::unordered_map<std::string, std::string> aliasToUuid_;   // alias -> uuid

    // Change tracking
    uint32_t changesSinceRegeneration_ = 0;
    std::chrono::system_clock::time_point lastRegeneration_;

    // Helper methods
    void SaveRegistry();
    void LoadRegistry();
    std::string SanitizeAlias(const std::string& name) const;
    bool ValidateAliasUnique(const std::string& alias, const std::string& excludeUuid = "") const;
};

} // namespace ShaderManagement
