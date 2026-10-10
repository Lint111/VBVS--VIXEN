// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file CalibrationStore.h
 * @brief Event-driven file persistence for TaskProfile calibration data
 *
 * Sprint 6.3: Phase 7.1 - Persistence Layer (Event-Driven)
 * Design Element: #38 Timeline Capacity Tracker
 *
 * CalibrationStore is an autonomous component that:
 * - Subscribes to DeviceMetadataEvent to configure GPU identity and load data
 * - Subscribes to ApplicationShuttingDownEvent to save data
 * - Manages its own lifecycle without external orchestration
 *
 * File format:
 * {
 *   "version": 2,
 *   "gpuName": "NVIDIA GeForce RTX 3080",
 *   "gpuVendorId": 4318,
 *   "timestamp": "2025-01-08T12:00:00Z",
 *   "profiles": [ ... ]
 * }
 *
 * @see TaskProfileRegistry for runtime management
 */

#include "TaskProfileRegistry.h"
#include <CacheCodec.h>
#include "MessageBus.h"
#include "Message.h"
#include <nlohmann/json.hpp>
#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace Vixen::RenderGraph {

/**
 * @brief GPU identification for calibration file selection
 *
 * Identifies GPU hardware for cross-session calibration persistence.
 * Driver version is tracked to invalidate calibration when drivers change
 * (timing characteristics may differ between driver versions).
 */
struct GPUIdentifier {
    std::string name;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint32_t driverVersion = 0;  // Phase 7.2: Driver version tracking

    /**
     * @brief Generate filename-safe identifier
     *
     * Does NOT include driver version - same GPU uses same file.
     * Driver version mismatch is handled during Load().
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

    /**
     * @brief Check if hardware matches (ignoring driver version)
     */
    [[nodiscard]] bool SameHardware(const GPUIdentifier& other) const {
        return vendorId == other.vendorId && deviceId == other.deviceId;
    }

    /**
     * @brief Check if driver version matches
     */
    [[nodiscard]] bool SameDriver(const GPUIdentifier& other) const {
        return driverVersion == other.driverVersion;
    }

    /**
     * @brief Full equality (hardware + driver)
     */
    [[nodiscard]] bool operator==(const GPUIdentifier& other) const {
        return SameHardware(other) && SameDriver(other);
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
 * @brief Event-driven file persistence for calibration data
 *
 * CalibrationStore is autonomous - it subscribes to lifecycle events and
 * manages its own load/save timing. No external orchestration required.
 *
 * Usage:
 * @code
 * // Create store with dependencies
 * CalibrationStore store("calibration", registry, messageBus);
 *
 * // That's it! The store handles:
 * // - DeviceMetadataEvent → SetGPU() + Load()
 * // - ApplicationShuttingDownEvent → Save()
 * @endcode
 *
 * Manual API (for testing or explicit control):
 * @code
 * CalibrationStore store("calibration");
 * store.SetRegistry(&registry);
 * store.SetGPU({"RTX 3080", 4318, 8710});
 * store.Load();   // Manual load
 * store.Save();   // Manual save
 * @endcode
 */
class CalibrationStore {
public:
    static constexpr uint32_t CURRENT_VERSION = 2;

    /**
     * @brief Construct autonomous CalibrationStore (event-driven)
     *
     * Subscribes to lifecycle events automatically:
     * - DeviceMetadataEvent: configures GPU and loads calibration
     * - ApplicationShuttingDownEvent: saves calibration
     *
     * @param baseDir Directory for calibration files (created if needed)
     * @param registry TaskProfileRegistry to load/save (non-owning)
     * @param messageBus Event bus for subscriptions (non-owning)
     */
    CalibrationStore(
        const std::filesystem::path& baseDir,
        TaskProfileRegistry& registry,
        EventBus::MessageBus* messageBus
    )
        : baseDir_(baseDir)
        , registry_(&registry)
    {
        if (messageBus) {
            SubscribeToEvents(messageBus);
        }
    }

    /**
     * @brief Construct with base directory only (manual mode)
     *
     * Use SetRegistry() and SetGPU() before Load()/Save().
     * Call SubscribeToEvents() if you want event-driven behavior.
     *
     * @param baseDir Directory for calibration files (created if needed)
     */
    explicit CalibrationStore(const std::filesystem::path& baseDir)
        : baseDir_(baseDir) {}

    ~CalibrationStore() = default;

    // Non-copyable (owns subscriptions)
    CalibrationStore(const CalibrationStore&) = delete;
    CalibrationStore& operator=(const CalibrationStore&) = delete;

    // Movable
    CalibrationStore(CalibrationStore&&) noexcept = default;
    CalibrationStore& operator=(CalibrationStore&&) noexcept = default;

    // =========================================================================
    // Event Subscription (Autonomous Mode)
    // =========================================================================

    /**
     * @brief Subscribe to lifecycle events for autonomous operation
     *
     * After calling this, the store handles load/save automatically:
     * - DeviceMetadataEvent → SetGPU() + Load()
     * - ApplicationShuttingDownEvent → Save()
     *
     * @param messageBus Event bus (non-owning, must outlive CalibrationStore)
     */
    void SubscribeToEvents(EventBus::MessageBus* messageBus) {
        if (!messageBus) return;

        subscriptions_.SetBus(messageBus);

        // Subscribe to DeviceMetadataEvent - configure GPU and load
        subscriptions_.Subscribe<EventBus::DeviceMetadataEvent>(
            [this](const EventBus::DeviceMetadataEvent& e) {
                const auto& device = e.GetSelectedDevice();
                gpu_ = GPUIdentifier{
                    device.deviceName,
                    device.vendorID,
                    device.deviceID,
                    device.driverVersion  // Phase 7.2: Capture driver version
                };
                gpuConfigured_ = true;
                if (registry_) {
                    Load();
                }
            }
        );

        // Subscribe to ApplicationShuttingDownEvent - save on shutdown
        subscriptions_.Subscribe<EventBus::ApplicationShuttingDownEvent>(
            [this](const EventBus::ApplicationShuttingDownEvent&) {
                if (registry_ && gpuConfigured_) {
                    Save();
                }
            }
        );
    }

    /**
     * @brief Unsubscribe from events (automatic on destruction)
     */
    void UnsubscribeFromEvents() {
        subscriptions_.UnsubscribeAll();
    }

    // =========================================================================
    // Manual Configuration (for testing or explicit control)
    // =========================================================================

    /**
     * @brief Set TaskProfileRegistry reference
     *
     * Required for Load()/Save() operations.
     * Not needed if constructed with registry parameter.
     */
    void SetRegistry(TaskProfileRegistry* registry) {
        registry_ = registry;
    }

    /**
     * @brief Set GPU identifier for file selection
     *
     * Calibration files are per-GPU since timing characteristics vary.
     * Not needed if using event-driven mode (DeviceMetadataEvent sets this).
     *
     * @param gpu GPU identification
     */
    void SetGPU(const GPUIdentifier& gpu) {
        gpu_ = gpu;
        gpuConfigured_ = true;
    }

    /**
     * @brief Get current GPU identifier
     */
    [[nodiscard]] const GPUIdentifier& GetGPU() const {
        return gpu_;
    }

    /**
     * @brief Check if GPU is configured
     */
    [[nodiscard]] bool IsGPUConfigured() const {
        return gpuConfigured_;
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /**
     * @brief Save registry state to JSON file
     *
     * Saves to: {baseDir}/{gpuFilename}.json
     *
     * @return Operation result
     */
    CalibrationStoreResult Save() {
        CalibrationStoreResult result;

        if (!registry_) {
            result.message = "No TaskProfileRegistry configured";
            return result;
        }

        if (!gpuConfigured_) {
            result.message = "GPU not configured - cannot determine file path";
            return result;
        }

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
            j["gpuDriverVersion"] = gpu_.driverVersion;  // Phase 7.2
            j["timestamp"] = GetISOTimestamp();

            // Save profiles via registry
            registry_->SaveState(j);

            // Bind the record to this GPU and protect the complete JSON payload from partial
            // writes. The checksum excludes its own field and the finished document is atomically
            // published beside the previous one.
            const auto checksumInput = j.dump();
            j["checksum"] = std::to_string(CashSystem::CacheChecksum64(
                checksumInput.data(), checksumInput.size()));
            const auto jsonText = j.dump(2);
            std::string writeError;
            if (!Vixen::AtomicWriteText(filePath, jsonText, &writeError)) {
                result.message = "Failed to atomically save calibration file: " + writeError;
                return result;
            }

            result.success = true;
            result.profileCount = registry_->GetTaskCount();
            result.message = "Saved " + std::to_string(result.profileCount) +
                           " profiles to " + filePath.string();
        }
        catch (const std::exception& e) {
            result.message = std::string("Save failed: ") + e.what();
        }

        lastResult_ = result;
        return result;
    }

    /**
     * @brief Load calibration data from JSON file
     *
     * Loads from: {baseDir}/{gpuFilename}.json
     * If file doesn't exist, returns success with 0 profiles.
     *
     * @return Operation result
     */
    CalibrationStoreResult Load() {
        CalibrationStoreResult result;

        if (!registry_) {
            result.message = "No TaskProfileRegistry configured";
            return result;
        }

        if (!gpuConfigured_) {
            result.message = "GPU not configured - cannot determine file path";
            return result;
        }

        const auto reject = [this, &result](std::string message) {
            registry_->ResetAllCalibration();
            result.message = std::move(message);
            lastResult_ = result;
            return result;
        };

        try {
            auto filePath = GetFilePath();

            // Check if file exists
            if (!std::filesystem::exists(filePath)) {
                result.success = true;
                result.message = "No calibration file found (first run): " + filePath.string();
                lastResult_ = result;
                return result;
            }

            std::error_code sizeError;
            const auto fileSize = std::filesystem::file_size(filePath, sizeError);
            constexpr std::uintmax_t maxCalibrationBytes = 16ULL * 1024ULL * 1024ULL;
            if (sizeError || fileSize > maxCalibrationBytes) {
                return reject("Calibration file size is invalid or exceeds the limit");
            }

            // Read the bounded file before parsing so an incomplete or oversized write cannot
            // feed unbounded input to the JSON reader.
            std::ifstream file(filePath, std::ios::binary);
            if (!file.is_open()) {
                return reject("Failed to open calibration file for reading: " + filePath.string());
            }
            const std::string jsonText{
                std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            if (file.bad() || jsonText.size() != fileSize) {
                return reject("Calibration file was truncated while reading");
            }
            file.close();

            nlohmann::json j = nlohmann::json::parse(jsonText);
            if (!j.is_object()) {
                return reject("Calibration file is not a JSON object");
            }

            const auto savedChecksum = j.value("checksum", std::string{});
            if (savedChecksum.empty()) {
                return reject("Calibration file has no checksum");
            }
            j.erase("checksum");
            const auto checksumInput = j.dump();
            const auto expectedChecksum = std::to_string(CashSystem::CacheChecksum64(
                checksumInput.data(), checksumInput.size()));
            if (savedChecksum != expectedChecksum) {
                return reject("Calibration checksum mismatch");
            }

            // Version check
            uint32_t version = j.value("version", 0u);
            if (version != CURRENT_VERSION) {
                return reject("Version mismatch: file v" + std::to_string(version) +
                              ", expected v" + std::to_string(CURRENT_VERSION));
            }

            const auto savedVendor = j.value("gpuVendorId", 0u);
            const auto savedDevice = j.value("gpuDeviceId", 0u);
            if (savedVendor != gpu_.vendorId || savedDevice != gpu_.deviceId) {
                return reject("Calibration GPU identity mismatch");
            }

            // Phase 7.2: Driver version check
            uint32_t savedDriverVersion = j.value("gpuDriverVersion", 0u);
            if (savedDriverVersion != 0 && gpu_.driverVersion != 0 &&
                savedDriverVersion != gpu_.driverVersion) {
                // Driver version changed - profiles may be inaccurate
                // We still load them but mark them as needing recalibration
                driverVersionMismatch_ = true;
                // Note: Could clear profiles here, but we prefer to use them as starting estimates
            }

            // Load profiles
            result.profileCount = registry_->LoadState(j);

            result.success = true;
            std::string driverNote = driverVersionMismatch_ ?
                " (driver version changed - recalibration recommended)" : "";
            result.message = "Loaded " + std::to_string(result.profileCount) +
                           " profiles from " + filePath.string() + driverNote;
        }
        catch (const std::exception& e) {
            registry_->ResetAllCalibration();
            result.message = std::string("Load failed: ") + e.what();
        }

        lastResult_ = result;
        return result;
    }

    // Legacy API for backward compatibility (takes registry as parameter)
    CalibrationStoreResult Save(const TaskProfileRegistry& registry) {
        registry_ = const_cast<TaskProfileRegistry*>(&registry);
        return Save();
    }

    CalibrationStoreResult Load(TaskProfileRegistry& registry) {
        registry_ = &registry;
        return Load();
    }

    // =========================================================================
    // File Management
    // =========================================================================

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

    /**
     * @brief Get last operation result (for diagnostics)
     */
    [[nodiscard]] const CalibrationStoreResult& GetLastResult() const {
        return lastResult_;
    }

    /**
     * @brief Check if driver version changed since calibration was saved
     *
     * Phase 7.2: Hardware fingerprint detection.
     * If true, profiles were loaded from a different driver version
     * and may need recalibration for accurate timing estimates.
     */
    [[nodiscard]] bool HasDriverVersionMismatch() const {
        return driverVersionMismatch_;
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
    bool gpuConfigured_ = false;
    bool driverVersionMismatch_ = false;  // Phase 7.2: Track driver changes
    TaskProfileRegistry* registry_ = nullptr;
    EventBus::ScopedSubscriptions subscriptions_;
    CalibrationStoreResult lastResult_;
};

} // namespace Vixen::RenderGraph
