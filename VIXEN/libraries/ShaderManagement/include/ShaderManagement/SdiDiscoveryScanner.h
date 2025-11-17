#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <optional>

namespace ShaderManagement {

/**
 * @brief Discovered struct layout from SDI file
 *
 * Phase H: Discovery System
 * Extracted from generated SDI headers during startup scan.
 */
struct DiscoveredStructLayout {
    std::string structName;
    uint64_t layoutHash = 0;
    std::filesystem::path sdiFilePath;  // Source SDI file
    std::string shaderUuid;  // UUID from filename
};

/**
 * @brief SDI file discovery scanner
 *
 * Phase H: Hybrid Discovery System
 *
 * Scans generated SDI files at startup to discover shader struct layouts.
 * Extracts LAYOUT_HASH constants from struct definitions.
 *
 * Flow:
 * 1. Scan all *-SDI.h files in generated/sdi directory
 * 2. Parse struct definitions to extract LAYOUT_HASH constants
 * 3. Compare hashes against known compile-time types
 * 4. Register unknown types in UnknownTypeRegistry
 *
 * This enables:
 * - Automatic discovery of new shader struct types
 * - Runtime binding for unknown types
 * - User notification for promotion to compile-time
 */
class SdiDiscoveryScanner {
public:
    /**
     * @brief Construct scanner for SDI directory
     *
     * @param sdiDirectory Path to generated SDI files (e.g., "generated/sdi")
     */
    explicit SdiDiscoveryScanner(const std::filesystem::path& sdiDirectory);

    /**
     * @brief Scan all SDI files and discover struct layouts
     *
     * Parses each SDI file to extract LAYOUT_HASH constants from structs.
     *
     * @return Vector of discovered struct layouts with hashes
     */
    std::vector<DiscoveredStructLayout> ScanAll();

    /**
     * @brief Scan single SDI file
     *
     * @param sdiFilePath Path to SDI file
     * @return Vector of discovered struct layouts in this file
     */
    std::vector<DiscoveredStructLayout> ScanFile(const std::filesystem::path& sdiFilePath);

    /**
     * @brief Extract struct name and hash from LAYOUT_HASH line
     *
     * Example input:
     * "    static constexpr uint64_t LAYOUT_HASH = 0x123456789abcdefULL;"
     *
     * @param line Line from SDI file
     * @param currentStruct Name of struct being parsed (from previous "struct X {" line)
     * @return Layout hash if found, nullopt otherwise
     */
    static std::optional<uint64_t> ExtractLayoutHash(const std::string& line);

    /**
     * @brief Extract struct name from struct definition line
     *
     * Example input:
     * "struct CameraData {"
     *
     * @param line Line from SDI file
     * @return Struct name if found, empty string otherwise
     */
    static std::string ExtractStructName(const std::string& line);

    /**
     * @brief Extract UUID from SDI filename
     *
     * Example: "7a57264d155fdf74-SDI.h" -> "7a57264d155fdf74"
     *
     * @param filename SDI filename
     * @return UUID string, or empty if invalid format
     */
    static std::string ExtractUuidFromFilename(const std::string& filename);

private:
    std::filesystem::path sdiDirectory_;
};

} // namespace ShaderManagement
