#pragma once

#include "DeviceCapabilities.h"
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>

namespace Vixen::Profiler {

/**
 * @brief Packages benchmark results into a ZIP archive for sharing
 *
 * Creates a self-contained ZIP file that testers can send back for aggregation.
 * The package includes:
 * - All JSON benchmark results
 * - Debug images (screenshots)
 * - system_info.json with machine/GPU metadata
 *
 * Usage:
 * @code
 * TesterPackage packager;
 * auto result = packager.CreatePackage(
 *     "./benchmark_results",  // Source directory with JSON/images
 *     "./packages",           // Output directory for ZIP
 *     deviceCapabilities      // GPU/machine info
 * );
 * if (result.success) {
 *     std::cout << "Package: " << result.packagePath << "\n";
 * }
 * @endcode
 */
class TesterPackage {
public:
    /// Result of package creation
    struct PackageResult {
        bool success = false;
        std::filesystem::path packagePath;
        std::string errorMessage;
        size_t filesIncluded = 0;
        size_t originalSizeBytes = 0;
        size_t compressedSizeBytes = 0;
    };

    TesterPackage() = default;

    /**
     * @brief Create a ZIP package from benchmark results directory
     *
     * @param sourceDir Directory containing JSON results and debug_images/
     * @param outputDir Directory to write the ZIP file
     * @param deviceCaps Device capabilities for system_info.json
     * @param machineName Optional machine name (defaults to hostname)
     * @return PackageResult with success status and package path
     */
    PackageResult CreatePackage(
        const std::filesystem::path& sourceDir,
        const std::filesystem::path& outputDir,
        const DeviceCapabilities& deviceCaps,
        const std::string& machineName = "");

    /**
     * @brief Generate a unique package name based on timestamp and GPU
     *
     * Format: VIXEN_benchmark_YYYYMMDD_HHMMSS_<gpu-short>.zip
     * Example: VIXEN_benchmark_20251210_143052_RTX3060.zip
     */
    static std::string GeneratePackageName(const DeviceCapabilities& deviceCaps);

    /**
     * @brief Set tester name for inclusion in system_info
     */
    void SetTesterName(const std::string& name) { testerName_ = name; }

    /**
     * @brief Set optional notes for inclusion in system_info
     */
    void SetNotes(const std::string& notes) { notes_ = notes; }

private:
    std::string testerName_;
    std::string notes_;

    /// Create system_info.json content
    std::string GenerateSystemInfo(
        const DeviceCapabilities& deviceCaps,
        const std::string& machineName,
        size_t fileCount) const;

    /// Get short GPU name for filename (e.g., "RTX3060" from "NVIDIA GeForce RTX 3060")
    static std::string GetShortGPUName(const std::string& fullName);

    /// Get machine hostname
    static std::string GetHostname();
};

} // namespace Vixen::Profiler
