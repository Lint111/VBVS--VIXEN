#include "FileManifest.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace ShaderManagement {

FileManifest::FileManifest(const std::filesystem::path& outputDir)
    : manifestPath_(outputDir / ".shader_tool_manifest.json")
    , outputDir_(outputDir) {
    Load();
}

FileManifest::~FileManifest() {
    Save();
}

void FileManifest::TrackFile(const std::filesystem::path& file) {
    std::filesystem::path relPath = std::filesystem::relative(file, outputDir_);
    trackedFiles_.insert(relPath.string());
}

void FileManifest::UntrackFile(const std::filesystem::path& file) {
    std::filesystem::path relPath = std::filesystem::relative(file, outputDir_);
    trackedFiles_.erase(relPath.string());
}

bool FileManifest::IsTracked(const std::filesystem::path& file) const {
    std::filesystem::path relPath = std::filesystem::relative(file, outputDir_);
    return trackedFiles_.contains(relPath.string());
}

uint32_t FileManifest::CleanupOrphaned() {
    namespace fs = std::filesystem;
    std::unordered_set<std::string> existingFiles;

    // Find all .spv and .json files in output directory
    if (fs::exists(outputDir_)) {
        for (const auto& entry : fs::recursive_directory_iterator(outputDir_)) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension();
                if (ext == ".spv" || ext == ".json") {
                    fs::path relPath = fs::relative(entry.path(), outputDir_);
                    existingFiles.insert(relPath.string());
                }
            }
        }
    }

    // Find orphaned files (exist on disk but not in manifest)
    uint32_t removed = 0;
    for (const auto& file : existingFiles) {
        if (file == ".shader_tool_manifest.json") continue;  // Skip manifest itself

        if (!trackedFiles_.contains(file)) {
            // Orphaned file - remove it
            fs::path fullPath = outputDir_ / file;
            std::error_code ec;
            if (fs::remove(fullPath, ec)) {
                ++removed;
            }
        }
    }

    // Remove dead entries from manifest (tracked but don't exist)
    auto it = trackedFiles_.begin();
    while (it != trackedFiles_.end()) {
        if (!existingFiles.contains(*it)) {
            it = trackedFiles_.erase(it);
        } else {
            ++it;
        }
    }

    return removed;
}

void FileManifest::Save() {
    nlohmann::json j;
    j["version"] = 1;
    j["files"] = nlohmann::json::array();
    for (const auto& file : trackedFiles_) {
        j["files"].push_back(file);
    }

    std::ofstream file(manifestPath_);
    if (file.is_open()) {
        file << j.dump(2);
    }
}

void FileManifest::Load() {
    if (!std::filesystem::exists(manifestPath_)) return;

    std::ifstream file(manifestPath_);
    if (!file.is_open()) return;

    try {
        nlohmann::json j;
        file >> j;
        if (j.contains("files") && j["files"].is_array()) {
            for (const auto& item : j["files"]) {
                trackedFiles_.insert(item.get<std::string>());
            }
        }
    } catch (...) {
        // Corrupted manifest - start fresh
        trackedFiles_.clear();
    }
}

} // namespace ShaderManagement
