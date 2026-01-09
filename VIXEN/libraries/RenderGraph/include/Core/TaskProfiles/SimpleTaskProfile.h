// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file SimpleTaskProfile.h
 * @brief Basic task profile with linear cost model
 *
 * Sprint 6.3: Phase 3.2 - Concrete TaskProfile for Simple Use Cases
 *
 * Default implementation for tasks that don't need complex cost models.
 * Uses linear scaling: cost = baseline + (workUnits * costPerUnit)
 *
 * Suitable for:
 * - Batch processing tasks (cost scales linearly with batch size)
 * - Simple dispatch tasks
 * - Tasks where measurement data is sparse
 */

#include "Core/ITaskProfile.h"
#include <sstream>
#include <iomanip>

namespace Vixen::RenderGraph {

/**
 * @brief Simple task profile with linear cost model
 *
 * Cost model: cost = costAtBaselineNs + (workUnits * costPerUnitNs)
 * - workUnits = 0: baseline cost (first measurement)
 * - workUnits > 0: increased cost (more work)
 * - workUnits < 0: decreased cost (less work)
 *
 * The costPerUnit is learned from measurements at different workUnit levels.
 */
class SimpleTaskProfile : public ITaskProfile {
public:
    /**
     * @brief Construct with task identity
     *
     * @param name Display name for debugging (e.g., "compute_dispatch_0")
     * @param category Category for grouping (e.g., "compute")
     */
    SimpleTaskProfile(
        const std::string& name,
        const std::string& category = ""
    ) {
        name_ = name;
        category_ = category;
        workUnitType_ = WorkUnitType::Custom;
    }

    /**
     * @brief Default constructor for deserialization
     */
    SimpleTaskProfile() {
        workUnitType_ = WorkUnitType::Custom;
    }

    // =========================================================================
    // ITaskProfile Implementation
    // =========================================================================

    void OnWorkUnitsChanged(int32_t /*oldUnits*/, int32_t /*newUnits*/) override {
        // Simple profile has no special reaction to workUnit changes
        // Derived classes (like ResolutionTaskProfile) override this
    }

    [[nodiscard]] uint64_t GetEstimatedCostNs() const override {
        if (!isCalibrated_) {
            return 0;
        }
        // Linear model: baseline + (workUnits * costPerUnit)
        int64_t estimate = static_cast<int64_t>(costAtBaselineNs_) +
                          (static_cast<int64_t>(workUnits_) * static_cast<int64_t>(costPerUnitNs_));
        return static_cast<uint64_t>(std::max(estimate, int64_t{0}));
    }

    void RecordMeasurement(uint64_t actualNs) override {
        // Check if this is first measurement BEFORE calling base (base sets isCalibrated_)
        bool wasCalibrated = isCalibrated_;

        // Call base to add to pending samples
        ITaskProfile::RecordMeasurement(actualNs);

        // Process immediately for SimpleTaskProfile (backward compatibility)
        // More complex profiles may want to batch process
        ProcessSamples();

        if (!wasCalibrated) {
            // First measurement becomes baseline
            costAtBaselineNs_ = actualNs;
        } else if (workUnits_ == 0) {
            // At baseline: EMA smooth the baseline estimate
            costAtBaselineNs_ = static_cast<uint64_t>(
                costAtBaselineNs_ * (1.0f - SMOOTHING_ALPHA) +
                actualNs * SMOOTHING_ALPHA
            );
        } else {
            // Not at baseline: learn costPerUnit from delta
            // delta = actualCost - baseline = workUnits * costPerUnit
            // costPerUnit = delta / workUnits
            int64_t delta = static_cast<int64_t>(actualNs) - static_cast<int64_t>(costAtBaselineNs_);
            int64_t measuredCostPerUnit = delta / workUnits_;

            if (measuredCostPerUnit > 0) {
                costPerUnitNs_ = static_cast<uint64_t>(
                    costPerUnitNs_ * (1.0f - SMOOTHING_ALPHA) +
                    static_cast<uint64_t>(measuredCostPerUnit) * SMOOTHING_ALPHA
                );
            }
        }
    }

    void SaveState(nlohmann::json& j) const override {
        // Save base class state
        ITaskProfile::SaveState(j);

        // Save task-specific members
        j["costAtBaselineNs"] = costAtBaselineNs_;
        j["costPerUnitNs"] = costPerUnitNs_;
    }

    void LoadState(const nlohmann::json& j) override {
        // Load base class state
        ITaskProfile::LoadState(j);

        // Load task-specific members
        if (j.contains("costAtBaselineNs")) {
            costAtBaselineNs_ = j["costAtBaselineNs"];
        }
        if (j.contains("costPerUnitNs")) {
            costPerUnitNs_ = j["costPerUnitNs"];
        }
    }

    [[nodiscard]] std::string GetTypeName() const override {
        return "SimpleTaskProfile";
    }

    [[nodiscard]] std::string GetStateDescription() const override {
        std::ostringstream oss;
        oss << name_ << ": "
            << "workUnits=" << (workUnits_ >= 0 ? "+" : "") << workUnits_
            << ", est=" << std::fixed << std::setprecision(2)
            << (GetEstimatedCostNs() / 1'000'000.0) << "ms";
        if (isCalibrated_) {
            oss << " (calibrated, " << sampleCount_ << " samples)";
        } else {
            oss << " (uncalibrated)";
        }
        return oss.str();
    }

    void ResetCalibration() override {
        ITaskProfile::ResetCalibration();
        costAtBaselineNs_ = 0;
        costPerUnitNs_ = 0;
    }

    // =========================================================================
    // Simple Profile Specific API
    // =========================================================================

    /**
     * @brief Get baseline cost (at workUnits = 0)
     */
    [[nodiscard]] uint64_t GetBaselineCostNs() const { return costAtBaselineNs_; }

    /**
     * @brief Get learned cost per work unit
     */
    [[nodiscard]] uint64_t GetCostPerUnitNs() const { return costPerUnitNs_; }

    /**
     * @brief Manually set baseline cost (for pre-calibration)
     */
    void SetBaselineCostNs(uint64_t cost) {
        costAtBaselineNs_ = cost;
        isCalibrated_ = true;
    }

    /**
     * @brief Manually set cost per unit (for pre-calibration)
     */
    void SetCostPerUnitNs(uint64_t costPerUnit) {
        costPerUnitNs_ = costPerUnit;
    }

    /**
     * @brief Check if this profile has reliable calibration (enough samples)
     */
    [[nodiscard]] bool HasReliableCalibration() const {
        return isCalibrated_ && sampleCount_ >= 10;
    }

private:
    static constexpr float SMOOTHING_ALPHA = 0.1f;

    // Cost model: baseline + (workUnits * costPerUnit)
    uint64_t costAtBaselineNs_ = 0;
    uint64_t costPerUnitNs_ = 0;
};

} // namespace Vixen::RenderGraph
