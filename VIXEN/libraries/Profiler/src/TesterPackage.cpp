#include "Profiler/TesterPackage.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

// Use miniz for ZIP creation - included via CMake or as header
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"

namespace Vixen::Profiler {

std::string TesterPackage::GetHostname() {
#ifdef _WIN32
    char hostname[256];
    DWORD size = sizeof(hostname);
    if (GetComputerNameA(hostname, &size)) {
        return std::string(hostname);
    }
    return "unknown-host";
#else
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "unknown-host";
#endif
}

std::string TesterPackage::GetShortGPUName(const std::string& fullName) {
    // Extract short GPU identifier from full name
    // "NVIDIA GeForce RTX 3060 Laptop GPU" -> "RTX3060"
    // "AMD Radeon RX 6800 XT" -> "RX6800XT"
    // "Intel(R) UHD Graphics 630" -> "UHD630"

    std::string result;

    // Look for common patterns
    size_t rtxPos = fullName.find("RTX");
    size_t gtxPos = fullName.find("GTX");
    size_t rxPos = fullName.find("RX ");
    size_t uhdPos = fullName.find("UHD");
    size_t irisPos = fullName.find("Iris");

    if (rtxPos != std::string::npos) {
        // Extract "RTX XXXX"
        size_t numStart = fullName.find_first_of("0123456789", rtxPos);
        if (numStart != std::string::npos) {
            size_t numEnd = fullName.find_first_not_of("0123456789", numStart);
            result = "RTX" + fullName.substr(numStart, numEnd - numStart);
        }
    } else if (gtxPos != std::string::npos) {
        size_t numStart = fullName.find_first_of("0123456789", gtxPos);
        if (numStart != std::string::npos) {
            size_t numEnd = fullName.find_first_not_of("0123456789", numStart);
            result = "GTX" + fullName.substr(numStart, numEnd - numStart);
        }
    } else if (rxPos != std::string::npos) {
        size_t numStart = fullName.find_first_of("0123456789", rxPos);
        if (numStart != std::string::npos) {
            size_t numEnd = fullName.find_first_not_of("0123456789 ", numStart);
            std::string num = fullName.substr(numStart, numEnd - numStart);
            // Remove spaces
            num.erase(std::remove(num.begin(), num.end(), ' '), num.end());
            result = "RX" + num;
            // Check for XT suffix
            if (fullName.find("XT", numEnd) != std::string::npos) {
                result += "XT";
            }
        }
    } else if (uhdPos != std::string::npos) {
        size_t numStart = fullName.find_first_of("0123456789", uhdPos);
        if (numStart != std::string::npos) {
            size_t numEnd = fullName.find_first_not_of("0123456789", numStart);
            result = "UHD" + fullName.substr(numStart, numEnd - numStart);
        }
    } else if (irisPos != std::string::npos) {
        result = "Iris";
        size_t numStart = fullName.find_first_of("0123456789", irisPos);
        if (numStart != std::string::npos) {
            size_t numEnd = fullName.find_first_not_of("0123456789", numStart);
            result += fullName.substr(numStart, numEnd - numStart);
        }
    }

    // Fallback: use first alphanumeric chars
    if (result.empty()) {
        for (char c : fullName) {
            if (std::isalnum(c) && result.length() < 16) {
                result += c;
            }
        }
    }

    return result.empty() ? "GPU" : result;
}

std::string TesterPackage::GeneratePackageName(const DeviceCapabilities& deviceCaps) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream oss;
    oss << "VIXEN_benchmark_"
        << std::put_time(&tm, "%Y%m%d_%H%M%S")
        << "_" << GetShortGPUName(deviceCaps.deviceName)
        << ".zip";

    return oss.str();
}

std::string TesterPackage::GenerateSystemInfo(
    const DeviceCapabilities& deviceCaps,
    const std::string& machineName,
    size_t fileCount) const {

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream json;
    json << "{\n";
    json << "  \"package_version\": \"1.0\",\n";
    json << "  \"created\": \"" << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << "\",\n";
    json << "  \"machine_name\": \"" << machineName << "\",\n";

    if (!testerName_.empty()) {
        json << "  \"tester_name\": \"" << testerName_ << "\",\n";
    }

    json << "  \"device\": {\n";
    json << "    \"gpu\": \"" << deviceCaps.deviceName << "\",\n";
    json << "    \"driver\": \"" << deviceCaps.driverVersion << "\",\n";
    json << "    \"vulkan_version\": \"" << deviceCaps.vulkanVersion << "\",\n";
    json << "    \"vram_mb\": " << deviceCaps.totalVRAM_MB << ",\n";
    json << "    \"vendor_id\": " << deviceCaps.vendorID << ",\n";
    json << "    \"device_id\": " << deviceCaps.deviceID << ",\n";
    json << "    \"device_type\": " << static_cast<int>(deviceCaps.deviceType) << ",\n";
    json << "    \"timestamp_period_ns\": " << std::fixed << std::setprecision(2)
         << deviceCaps.timestampPeriod << ",\n";
    json << "    \"timestamp_supported\": " << (deviceCaps.timestampSupported ? "true" : "false") << ",\n";
    json << "    \"performance_query_supported\": " << (deviceCaps.performanceQuerySupported ? "true" : "false") << "\n";
    json << "  },\n";

    json << "  \"test_count\": " << fileCount << ",\n";

    if (!notes_.empty()) {
        json << "  \"notes\": \"" << notes_ << "\",\n";
    }

    json << "  \"format_notes\": \"JSON files contain per-test results. debug_images/ contains screenshots.\"\n";
    json << "}\n";

    return json.str();
}

TesterPackage::PackageResult TesterPackage::CreatePackage(
    const std::filesystem::path& sourceDir,
    const std::filesystem::path& outputDir,
    const DeviceCapabilities& deviceCaps,
    const std::string& machineName) {

    PackageResult result;

    // Validate source directory
    if (!std::filesystem::exists(sourceDir)) {
        result.errorMessage = "Source directory does not exist: " + sourceDir.string();
        return result;
    }

    // Create output directory if needed
    if (!std::filesystem::exists(outputDir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(outputDir, ec)) {
            result.errorMessage = "Failed to create output directory: " + outputDir.string();
            return result;
        }
    }

    // Generate package path
    std::string packageName = GeneratePackageName(deviceCaps);
    result.packagePath = outputDir / packageName;

    // Determine machine name
    std::string machine = machineName.empty() ? GetHostname() : machineName;

    // Collect files to package
    std::vector<std::filesystem::path> filesToPackage;

    // Add all JSON files
    for (const auto& entry : std::filesystem::directory_iterator(sourceDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            filesToPackage.push_back(entry.path());
        }
    }

    // Add debug_images directory contents
    auto debugImagesDir = sourceDir / "debug_images";
    if (std::filesystem::exists(debugImagesDir)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(debugImagesDir)) {
            if (entry.is_regular_file()) {
                filesToPackage.push_back(entry.path());
            }
        }
    }

    // Create ZIP archive
    mz_zip_archive zip{};
    mz_bool status = mz_zip_writer_init_file(&zip, result.packagePath.string().c_str(), 0);
    if (!status) {
        result.errorMessage = "Failed to create ZIP file: " + result.packagePath.string();
        return result;
    }

    // Add files to ZIP
    size_t totalOriginalSize = 0;
    for (const auto& filePath : filesToPackage) {
        // Calculate archive path (relative to source directory)
        auto relativePath = std::filesystem::relative(filePath, sourceDir);
        std::string archivePath = relativePath.string();
        // Normalize path separators for ZIP
        std::replace(archivePath.begin(), archivePath.end(), '\\', '/');

        // Read file content
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            continue;  // Skip unreadable files
        }

        std::ostringstream content;
        content << file.rdbuf();
        std::string data = content.str();

        totalOriginalSize += data.size();

        // Add to ZIP with compression
        status = mz_zip_writer_add_mem(&zip, archivePath.c_str(),
                                        data.data(), data.size(),
                                        MZ_BEST_COMPRESSION);
        if (!status) {
            mz_zip_writer_end(&zip);
            result.errorMessage = "Failed to add file to ZIP: " + filePath.string();
            return result;
        }

        result.filesIncluded++;
    }

    // Generate and add system_info.json
    std::string systemInfo = GenerateSystemInfo(deviceCaps, machine, result.filesIncluded);
    status = mz_zip_writer_add_mem(&zip, "system_info.json",
                                    systemInfo.data(), systemInfo.size(),
                                    MZ_BEST_COMPRESSION);
    if (!status) {
        mz_zip_writer_end(&zip);
        result.errorMessage = "Failed to add system_info.json to ZIP";
        return result;
    }
    totalOriginalSize += systemInfo.size();
    result.filesIncluded++;

    // Add benchmark schema for data validation (reference for consumers)
    std::string schemaReference = R"({
  "$comment": "VIXEN Benchmark Schema Reference",
  "schema_url": "https://github.com/VIXEN-Engine/VIXEN/blob/main/application/benchmark/benchmark_schema.json",
  "description": "Full JSON Schema for benchmark result validation available at schema_url"
})";
    status = mz_zip_writer_add_mem(&zip, "schema_reference.json",
                                    schemaReference.data(), schemaReference.size(),
                                    MZ_BEST_COMPRESSION);
    if (status) {
        totalOriginalSize += schemaReference.size();
        result.filesIncluded++;
    }
    // Non-critical if this fails, continue

    // Finalize ZIP
    status = mz_zip_writer_finalize_archive(&zip);
    if (!status) {
        mz_zip_writer_end(&zip);
        result.errorMessage = "Failed to finalize ZIP archive";
        return result;
    }

    mz_zip_writer_end(&zip);

    // Get compressed size
    result.originalSizeBytes = totalOriginalSize;
    std::error_code ec;
    result.compressedSizeBytes = std::filesystem::file_size(result.packagePath, ec);

    result.success = true;
    return result;
}

} // namespace Vixen::Profiler
