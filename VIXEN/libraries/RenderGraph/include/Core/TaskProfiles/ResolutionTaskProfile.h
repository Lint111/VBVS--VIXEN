// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file ResolutionTaskProfile.h
 * @brief Task profile for resolution-based workloads (shadows, reflections, etc.)
 *
 * Sprint 6.3: Phase 3.2 - Concrete TaskProfile Example
 *
 * Demonstrates polymorphic task profile where:
 * - workUnits maps to resolution via lookup table
 * - Cost scales quadratically with resolution
 * - Task-specific calibration (measured cost per resolution level)
 */

#include "Core/ITaskProfile.h"
#include <array>
#include <sstream>
#include <iomanip>

namespace Vixen::RenderGraph {

/**
 * @brief Task profile for resolution-based workloads
 *
 * Maps workUnits to resolution levels:
 * - workUnits = -5 → minimum resolution (e.g., 128x128)
 * - workUnits = 0  → baseline resolution (e.g., 1024x1024)
 * - workUnits = +5 → maximum resolution (e.g., 4096x4096)
 *
 * Cost model: Quadratic scaling with resolution (pixels processed)
 *
 * Task-specific members persisted:
 * - Resolution table
 * - Per-level measured costs
 * - Baseline cost at default resolution
 */
class ResolutionTaskProfile : public ITaskProfile {
public:
    static constexpr size_t NUM_LEVELS = 11;  // -5 to +5 = 11 levels

    /**
     * @brief Construct with resolution table
     *
     * @param name Display name (e.g., "shadowMap_cascade0")
     * @param category Category for grouping (e.g., "shadow")
     * @param resolutions Resolution table indexed by workUnits+5 (must have 11 entries)
     */
    ResolutionTaskProfile(
        const std::string& name,
        const std::string& category,
        const std::array<uint32_t, NUM_LEVELS>& resolutions
    ) : resolutions_(resolutions) {
        name_ = name;
        category_ = category;
        workUnitType_ = WorkUnitType::Resolution;
        minWorkUnits_ = -5;
        maxWorkUnits_ = +5;
        workUnits_ = 0;

        // Initialize per-level costs to 0 (uncalibrated)
        measuredCostsPerLevel_.fill(0);

        // Set baseline cost estimate (will be refined by measurements)
        UpdateCurrentResolution();
    }

    /**
     * @brief Default constructor for deserialization
     */
    ResolutionTaskProfile() {
        workUnitType_ = WorkUnitType::Resolution;
        minWorkUnits_ = -5;
        maxWorkUnits_ = +5;

        // Default resolution table (power-of-2 friendly)
        resolutions_ = {128, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 4096};
        measuredCostsPerLevel_.fill(0);
    }

    // =========================================================================
    // ITaskProfile Implementation
    // =========================================================================

    void OnWorkUnitsChanged(int32_t oldUnits, int32_t newUnits) override {
        UpdateCurrentResolution();
        // Could trigger descriptor set rebuild, framebuffer resize, etc.
    }

    [[nodiscard]] uint64_t GetEstimatedCostNs() const override {
        size_t index = static_cast<size_t>(workUnits_ + 5);

        // If we have measured cost for this level, use it
        if (measuredCostsPerLevel_[index] > 0) {
            return measuredCostsPerLevel_[index];
        }

        // Otherwise, estimate from baseline using quadratic scaling
        if (baselineCostNs_ == 0) {
            return 0;  // Not yet calibrated
        }

        float scaleFactor = static_cast<float>(currentResolution_ * currentResolution_) /
                           static_cast<float>(baselineResolution_ * baselineResolution_);
        return static_cast<uint64_t>(baselineCostNs_ * scaleFactor);
    }

    void RecordMeasurement(uint64_t actualNs) override {
        // Call base to update common stats
        ITaskProfile::RecordMeasurement(actualNs);

        // Record measurement for current level
        size_t index = static_cast<size_t>(workUnits_ + 5);

        if (measuredCostsPerLevel_[index] == 0) {
            // First measurement for this level
            measuredCostsPerLevel_[index] = actualNs;
        } else {
            // EMA smoothing (α = 0.1)
            measuredCostsPerLevel_[index] = static_cast<uint64_t>(
                measuredCostsPerLevel_[index] * 0.9 + actualNs * 0.1
            );
        }

        // Update baseline if at default level (workUnits = 0)
        if (workUnits_ == 0) {
            if (baselineCostNs_ == 0) {
                baselineCostNs_ = actualNs;
                baselineResolution_ = currentResolution_;
            } else {
                baselineCostNs_ = static_cast<uint64_t>(
                    baselineCostNs_ * 0.9 + actualNs * 0.1
                );
            }
        }
    }

    void SaveState(nlohmann::json& j) const override {
        // Save base class state
        ITaskProfile::SaveState(j);

        // Save task-specific members
        j["currentResolution"] = currentResolution_;
        j["baselineResolution"] = baselineResolution_;
        j["baselineCostNs"] = baselineCostNs_;

        // Save resolution table
        j["resolutions"] = nlohmann::json::array();
        for (uint32_t res : resolutions_) {
            j["resolutions"].push_back(res);
        }

        // Save per-level measured costs
        j["measuredCostsPerLevel"] = nlohmann::json::array();
        for (uint64_t cost : measuredCostsPerLevel_) {
            j["measuredCostsPerLevel"].push_back(cost);
        }
    }

    void LoadState(const nlohmann::json& j) override {
        // Load base class state
        ITaskProfile::LoadState(j);

        // Load task-specific members
        if (j.contains("currentResolution")) {
            currentResolution_ = j["currentResolution"];
        }
        if (j.contains("baselineResolution")) {
            baselineResolution_ = j["baselineResolution"];
        }
        if (j.contains("baselineCostNs")) {
            baselineCostNs_ = j["baselineCostNs"];
        }

        // Load resolution table
        if (j.contains("resolutions") && j["resolutions"].is_array()) {
            size_t i = 0;
            for (const auto& res : j["resolutions"]) {
                if (i < NUM_LEVELS) {
                    resolutions_[i++] = res.get<uint32_t>();
                }
            }
        }

        // Load per-level measured costs
        if (j.contains("measuredCostsPerLevel") && j["measuredCostsPerLevel"].is_array()) {
            size_t i = 0;
            for (const auto& cost : j["measuredCostsPerLevel"]) {
                if (i < NUM_LEVELS) {
                    measuredCostsPerLevel_[i++] = cost.get<uint64_t>();
                }
            }
        }

        // Update current resolution from workUnits
        UpdateCurrentResolution();
    }

    [[nodiscard]] std::string GetTypeName() const override {
        return "ResolutionTaskProfile";
    }

    [[nodiscard]] std::string GetStateDescription() const override {
        std::ostringstream oss;
        oss << name_ << ": "
            << currentResolution_ << "x" << currentResolution_
            << " (workUnits=" << (workUnits_ >= 0 ? "+" : "") << workUnits_
            << ", est=" << std::fixed << std::setprecision(2)
            << (GetEstimatedCostNs() / 1'000'000.0) << "ms)";
        return oss.str();
    }

    void ResetCalibration() override {
        ITaskProfile::ResetCalibration();
        measuredCostsPerLevel_.fill(0);
        baselineCostNs_ = 0;
        baselineResolution_ = resolutions_[5];  // Default resolution
        UpdateCurrentResolution();
    }

    // =========================================================================
    // Resolution-Specific API
    // =========================================================================

    /**
     * @brief Get current resolution
     */
    [[nodiscard]] uint32_t GetResolution() const { return currentResolution_; }

    /**
     * @brief Get resolution at specific workUnits level
     */
    [[nodiscard]] uint32_t GetResolutionAtLevel(int32_t units) const {
        int32_t clamped = std::clamp(units, minWorkUnits_, maxWorkUnits_);
        return resolutions_[static_cast<size_t>(clamped + 5)];
    }

    /**
     * @brief Set resolution table
     */
    void SetResolutionTable(const std::array<uint32_t, NUM_LEVELS>& resolutions) {
        resolutions_ = resolutions;
        UpdateCurrentResolution();
    }

    /**
     * @brief Get measured cost at specific level (0 if not measured)
     */
    [[nodiscard]] uint64_t GetMeasuredCostAtLevel(int32_t units) const {
        int32_t clamped = std::clamp(units, minWorkUnits_, maxWorkUnits_);
        return measuredCostsPerLevel_[static_cast<size_t>(clamped + 5)];
    }

    /**
     * @brief Get number of calibrated levels
     */
    [[nodiscard]] size_t GetCalibratedLevelCount() const {
        size_t count = 0;
        for (uint64_t cost : measuredCostsPerLevel_) {
            if (cost > 0) ++count;
        }
        return count;
    }

private:
    void UpdateCurrentResolution() {
        size_t index = static_cast<size_t>(workUnits_ + 5);
        currentResolution_ = resolutions_[index];
    }

    // Resolution table: workUnits+5 → resolution
    std::array<uint32_t, NUM_LEVELS> resolutions_;

    // Current resolution (derived from workUnits)
    uint32_t currentResolution_ = 1024;

    // Baseline for cost estimation
    uint32_t baselineResolution_ = 1024;
    uint64_t baselineCostNs_ = 0;

    // Per-level measured costs for accurate estimation
    std::array<uint64_t, NUM_LEVELS> measuredCostsPerLevel_;
};

} // namespace Vixen::RenderGraph
