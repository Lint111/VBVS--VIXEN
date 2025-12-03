#include "SdiDiscoveryScanner.h"
#include <fstream>
#include <sstream>
#include <regex>

namespace ShaderManagement {

SdiDiscoveryScanner::SdiDiscoveryScanner(const std::filesystem::path& sdiDirectory)
    : sdiDirectory_(sdiDirectory)
{
}

std::vector<DiscoveredStructLayout> SdiDiscoveryScanner::ScanAll() {
    std::vector<DiscoveredStructLayout> allLayouts;

    if (!std::filesystem::exists(sdiDirectory_)) {
        // SDI directory not found - silently return empty results
        return allLayouts;
    }

    // Scan all *-SDI.h files
    for (const auto& entry : std::filesystem::directory_iterator(sdiDirectory_)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string filename = entry.path().filename().string();
        if (filename.find("-SDI.h") == std::string::npos) {
            continue;  // Not an SDI file
        }

        auto layouts = ScanFile(entry.path());
        allLayouts.insert(allLayouts.end(), layouts.begin(), layouts.end());
    }

    // Discovery complete - allLayouts contains all discovered struct layouts
    return allLayouts;
}

std::vector<DiscoveredStructLayout> SdiDiscoveryScanner::ScanFile(
    const std::filesystem::path& sdiFilePath
) {
    std::vector<DiscoveredStructLayout> layouts;

    std::ifstream file(sdiFilePath);
    if (!file.is_open()) {
        // Failed to open SDI file - return empty layouts
        return layouts;
    }

    std::string uuid = ExtractUuidFromFilename(sdiFilePath.filename().string());
    std::string line;
    std::string currentStruct;  // Name of struct being parsed

    while (std::getline(file, line)) {
        // Check for struct definition
        std::string structName = ExtractStructName(line);
        if (!structName.empty()) {
            currentStruct = structName;
            continue;
        }

        // Check for LAYOUT_HASH within current struct
        if (!currentStruct.empty()) {
            auto hashOpt = ExtractLayoutHash(line);
            if (hashOpt.has_value()) {
                DiscoveredStructLayout layout;
                layout.structName = currentStruct;
                layout.layoutHash = hashOpt.value();
                layout.sdiFilePath = sdiFilePath;
                layout.shaderUuid = uuid;

                layouts.push_back(layout);

                // Reset current struct (assume we're done with this struct)
                currentStruct.clear();
            }
        }

        // Detect end of struct
        if (line.find("};") != std::string::npos) {
            currentStruct.clear();
        }
    }

    return layouts;
}

std::optional<uint64_t> SdiDiscoveryScanner::ExtractLayoutHash(const std::string& line) {
    // Match: "static constexpr uint64_t LAYOUT_HASH = 0x123456789abcdefULL;"
    std::regex hashRegex(R"(static\s+constexpr\s+uint64_t\s+LAYOUT_HASH\s*=\s*0x([0-9a-fA-F]+)ULL)");
    std::smatch match;

    if (std::regex_search(line, match, hashRegex)) {
        if (match.size() >= 2) {
            std::string hashHexStr = match[1].str();
            try {
                uint64_t hash = std::stoull(hashHexStr, nullptr, 16);
                return hash;
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }
    }

    return std::nullopt;
}

std::string SdiDiscoveryScanner::ExtractStructName(const std::string& line) {
    // Match: "struct StructName {" or "struct StructName{"
    std::regex structRegex(R"(^\s*struct\s+(\w+)\s*\{)");
    std::smatch match;

    if (std::regex_search(line, match, structRegex)) {
        if (match.size() >= 2) {
            return match[1].str();
        }
    }

    return "";
}

std::string SdiDiscoveryScanner::ExtractUuidFromFilename(const std::string& filename) {
    // Extract UUID from "7a57264d155fdf74-SDI.h"
    size_t dashPos = filename.find("-SDI.h");
    if (dashPos != std::string::npos) {
        return filename.substr(0, dashPos);
    }

    return "";
}

} // namespace ShaderManagement
