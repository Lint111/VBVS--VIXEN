// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file CalibrationStore.h
 * @brief File persistence for TaskProfile calibration data
 *
 * Sprint 6.3: Phase 3.2c - Persistence Layer
 * Design Element: #38 Timeline Capacity Tracker
 *
 * Provides JSON file I/O for TaskProfileRegistry:
 * - Save calibration data after sessions
 * - Load previous calibration on startup
 * - Per-application/per-GPU calibration files
 *
 * File format:
 * {
 *   "version": 1,
 *   "gpuName": "NVIDIA GeForce RTX 3080",
 *   "gpuVendorId": 4318,
 *   "timestamp": "2025-01-08T12:00:00Z",
 *   "profiles": [ ... ]
 * }
 *
 * @see TaskProfileRegistry for runtime management
 */

#include "TaskProfileRegistry.h"
#include <nlohmann/json.hpp>
#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace Vixen::RenderGraph {

/**
 * @brief GPU identification for calibration file selection
 */
struct GPUIdentifier {
    std::string name;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;

    /**
     * @brief Generate filename-safe identifier
     */
    [[nodiscard]] std::string ToFilename() const {
        std::string safe = name;
        // Replace spaces and special chars with underscores
        for (char& c : safe) {
            if (!std::isalnum(c) && c != '-' && c != '_') {
                c = '_';
            }
        }
        // Add vendor/device IDs for uniqueness
        return safe + "_" + std::to_string(vendorId) + "_" + std::to_string(deviceId);
    }
};

/**
 * @brief Result of a save/load operation
 */
struct CalibrationStoreResult {
    bool success = false;
    std::string message;
    size_t profileCount = 0;  // Number of profiles saved/loaded
};

/**
 * @brief File persistence for calibration data
 *
 * Usage:
 * @code
 * CalibrationStore store("calibration");
 * store.SetGPU({"RTX 3080", 4318, 8710});
 *
 * // On startup
 * store.Load(registry);
 *
 * // After session (or periodically)
 * store.Save(registry);
 * @endcode
 */
class CalibrationStore {
public:
    static constexpr uint32_t CURRENT_VERSION = 1;

    /**
     * @brief Construct with base directory
     *
     * @param baseDir Directory for calibration files (created if needed)
     */
    explicit CalibrationStore(const std::filesystem::path& baseDir)
        : baseDir_(baseDir) {}

    /**
     * @brief Set GPU identifier for file selection
     *
     * Calibration files are per-GPU since timing characteristics vary.
     * Call this before Save/Load operations.
     *
     * @param gpu GPU identification
     */
    void SetGPU(const GPUIdentifier& gpu) {
        gpu_ = gpu;
    }

    /**
     * @brief Get current GPU identifier
     */
    [[nodiscard]] const GPUIdentifier& GetGPU() const {
        return gpu_;
    }

    /**
     * @brief Save registry state to JSON file
     *
     * Saves to: {baseDir}/{gpuFilename}.json
     *
     * @param registry Registry to save
     * @return Operation result
     */
    CalibrationStoreResult Save(const TaskProfileRegistry& registry) {
        CalibrationStoreResult result;

        try {
            // Ensure directory exists
            if (!std::filesystem::exists(baseDir_)) {
                std::filesystem::create_directories(baseDir_);
            }

            // Build file path
            auto filePath = GetFilePath();

            // Build JSON
            nlohmann::json j;
            j["version"] = CURRENT_VERSION;
            j["gpuName"] = gpu_.name;
            j["gpuVendorId"] = gpu_.vendorId;
            j["gpuDeviceId"] = gpu_.deviceId;
            j["timestamp"] = GetISOTimestamp();

            // Save profiles via registry
            registry.SaveState(j);

            // Write to file
            std::ofstream file(filePath);
            if (!file.is_open()) {
                result.message = "Failed to open file for writing: " + filePath.string();
                return result;
            }

            file << j.dump(2);  // Pretty print with 2-space indent
            file.close();

            result.success = true;
            result.profileCount = registry.GetTaskCount();
            result.message = "Saved " + std::to_string(result.profileCount) +
                           " profiles to " + filePath.string();
        }
        catch (const std::exception& e) {
            result.message = std::string("Save failed: ") + e.what();
        }

        return result;
    }

    /**
     * @brief Load calibration data from JSON file
     *
     * Loads from: {baseDir}/{gpuFilename}.json
     * If file doesn't exist, returns success with 0 profiles.
     *
     * @param registry Registry to load into
     * @return Operation result
     */
    CalibrationStoreResult Load(TaskProfileRegistry& registry) {
        CalibrationStoreResult result;

        try {
            auto filePath = GetFilePath();

            // Check if file exists
            if (!std::filesystem::exists(filePath)) {
                result.success = true;
                result.message = "No calibration file found (first run): " + filePath.string();
                return result;
            }

            // Read file
            std::ifstream file(filePath);
            if (!file.is_open()) {
                result.message = "Failed to open file for reading: " + filePath.string();
                return result;
            }

            nlohmann::json j;
            file >> j;
            file.close();

            // Version check
            uint32_t version = j.value("version", 0u);
            if (version != CURRENT_VERSION) {
                result.message = "Version mismatch: file v" + std::to_string(version) +
                               ", expected v" + std::to_string(CURRENT_VERSION);
                // Could add migration logic here in future
                return result;
            }

            // Load profiles
            result.profileCount = registry.LoadState(j);

            result.success = true;
            result.message = "Loaded " + std::to_string(result.profileCount) +
                           " profiles from " + filePath.string();
        }
        catch (const std::exception& e) {
            result.message = std::string("Load failed: ") + e.what();
        }

        return result;
    }

    /**
     * @brief Delete calibration file for current GPU
     *
     * @return true if file was deleted or didn't exist
     */
    bool Delete() {
        auto filePath = GetFilePath();
        if (std::filesystem::exists(filePath)) {
            return std::filesystem::remove(filePath);
        }
        return true;  // Already doesn't exist
    }

    /**
     * @brief Check if calibration file exists
     */
    [[nodiscard]] bool Exists() const {
        return std::filesystem::exists(GetFilePath());
    }

    /**
     * @brief Get full path to calibration file
     */
    [[nodiscard]] std::filesystem::path GetFilePath() const {
        return baseDir_ / (gpu_.ToFilename() + ".json");
    }

    /**
     * @brief List all calibration files in directory
     *
     * @return Vector of GPU filenames (without .json extension)
     */
    [[nodiscard]] std::vector<std::string> ListCalibrationFiles() const {
        std::vector<std::string> files;

        if (!std::filesystem::exists(baseDir_)) {
            return files;
        }

        for (const auto& entry : std::filesystem::directory_iterator(baseDir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path().stem().string());
            }
        }

        return files;
    }

private:
    [[nodiscard]] static std::string GetISOTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&time), "%FT%TZ");
        return oss.str();
    }

    std::filesystem::path baseDir_;
    GPUIdentifier gpu_;
};

} // namespace Vixen::RenderGraph
