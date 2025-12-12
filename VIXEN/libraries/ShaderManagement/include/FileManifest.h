#pragma once

#include <filesystem>
#include <unordered_set>
#include <string>
#include <cstdint>

namespace ShaderManagement {

/**
 * @brief Manifest tracking generated files for cleanup
 *
 * Prevents orphaned files by recording all generated outputs.
 * Automatically saves/loads from .shader_tool_manifest.json in output directory.
 *
 * Usage:
 * @code
 * FileManifest manifest(outputDir);
 * manifest.TrackFile(spirvPath);
 * manifest.TrackFile(bundlePath);
 *
 * // Later, remove orphaned files
 * uint32_t removed = manifest.CleanupOrphaned();
 * @endcode
 *
 * Manifest format:
 * @code
 * {
 *   "version": 1,
 *   "files": ["shader1.spv", "shader1.json", ...]
 * }
 * @endcode
 */
class FileManifest {
public:
    /**
     * @brief Construct manifest for an output directory
     *
     * Automatically loads existing manifest if present.
     *
     * @param outputDir Directory containing generated files
     */
    explicit FileManifest(const std::filesystem::path& outputDir);

    /**
     * @brief Destructor - automatically saves manifest
     */
    ~FileManifest();

    // Non-copyable (owns file state)
    FileManifest(const FileManifest&) = delete;
    FileManifest& operator=(const FileManifest&) = delete;

    // Movable
    FileManifest(FileManifest&&) noexcept = default;
    FileManifest& operator=(FileManifest&&) noexcept = default;

    /**
     * @brief Track a generated file
     *
     * Adds file to manifest. Path is stored relative to output directory.
     *
     * @param file Absolute or relative path to file
     */
    void TrackFile(const std::filesystem::path& file);

    /**
     * @brief Stop tracking a file
     *
     * Removes file from manifest (does not delete the file).
     *
     * @param file Path to file
     */
    void UntrackFile(const std::filesystem::path& file);

    /**
     * @brief Remove orphaned files from output directory
     *
     * An orphaned file is one that:
     * - Exists on disk with .spv or .json extension
     * - Is NOT in the manifest
     *
     * Also removes dead entries from manifest (tracked but don't exist).
     *
     * @return Number of files removed
     */
    uint32_t CleanupOrphaned();

    /**
     * @brief Get number of tracked files
     *
     * @return Count of files in manifest
     */
    size_t GetTrackedCount() const { return trackedFiles_.size(); }

    /**
     * @brief Check if a file is tracked
     *
     * @param file Path to check
     * @return True if file is in manifest
     */
    bool IsTracked(const std::filesystem::path& file) const;

    /**
     * @brief Save manifest to disk
     *
     * Called automatically by destructor, but can be called manually.
     */
    void Save();

    /**
     * @brief Get manifest file path
     *
     * @return Path to .shader_tool_manifest.json
     */
    const std::filesystem::path& GetManifestPath() const { return manifestPath_; }

private:
    /**
     * @brief Load manifest from disk
     *
     * Called by constructor. Silently handles missing or corrupted files.
     */
    void Load();

    std::filesystem::path manifestPath_;
    std::filesystem::path outputDir_;
    std::unordered_set<std::string> trackedFiles_;
};

} // namespace ShaderManagement
