// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file ITaskProfile.h
 * @brief Abstract interface for polymorphic task profiles
 *
 * Sprint 6.3: Phase 3.2 - Abstract TaskProfile System
 * Design Element: #38 Timeline Capacity Tracker
 *
 * Each task type derives from ITaskProfile and implements:
 * - OnWorkUnitsChanged(): React to pressure valve changes
 * - GetEstimatedCostNs(): Task-specific cost model
 * - RecordMeasurement(): Task-specific calibration
 * - SaveState()/LoadState(): Task-specific persistence
 *
 * This enables true polymorphic pressure valves where:
 * - ShadowMapProfile changes resolution
 * - BatchDispatchProfile changes batch size
 * - VoxelTraversalProfile changes max ray steps
 *
 * @see TaskProfileRegistry for central management
 */

#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <memory>
#include <algorithm>
#include <functional>

namespace Vixen::RenderGraph {

/**
 * @brief How a task interprets its workUnits value
 */
enum class WorkUnitType : uint8_t {
    BatchSize,          ///< Items per dispatch (queue tasks)
    Resolution,         ///< Quality level index (render tasks)
    ThreadCount,        ///< Parallel workers (CPU tasks)
    IterationLimit,     ///< Max steps/iterations (traversal)
    LODLevel,           ///< Level of detail index
    Custom              ///< Task-specific interpretation
};

/**
 * @brief Convert WorkUnitType to string for serialization
 */
inline const char* WorkUnitTypeToString(WorkUnitType type) {
    switch (type) {
        case WorkUnitType::BatchSize:      return "BatchSize";
        case WorkUnitType::Resolution:     return "Resolution";
        case WorkUnitType::ThreadCount:    return "ThreadCount";
        case WorkUnitType::IterationLimit: return "IterationLimit";
        case WorkUnitType::LODLevel:       return "LODLevel";
        case WorkUnitType::Custom:         return "Custom";
        default:                           return "Unknown";
    }
}

/**
 * @brief Parse WorkUnitType from string
 */
inline WorkUnitType WorkUnitTypeFromString(const std::string& str) {
    if (str == "BatchSize")      return WorkUnitType::BatchSize;
    if (str == "Resolution")     return WorkUnitType::Resolution;
    if (str == "ThreadCount")    return WorkUnitType::ThreadCount;
    if (str == "IterationLimit") return WorkUnitType::IterationLimit;
    if (str == "LODLevel")       return WorkUnitType::LODLevel;
    return WorkUnitType::Custom;
}

/**
 * @brief Abstract interface for task profiles
 *
 * Base class provides:
 * - Common state management (workUnits, bounds, priority)
 * - Pressure valve mechanics (Increase/Decrease with bounds checking)
 * - Hooks for derived classes to implement task-specific behavior
 *
 * Derived classes MUST implement:
 * - OnWorkUnitsChanged(): Apply the workUnit change to actual task config
 * - GetEstimatedCostNs(): Return task-specific cost estimate
 * - RecordMeasurement(): Update task-specific calibration
 * - SaveState()/LoadState(): Persist task-specific members
 * - GetTypeName(): Return type identifier for factory pattern
 *
 * Usage:
 * @code
 * class ShadowMapProfile : public ITaskProfile {
 *     void OnWorkUnitsChanged(int32_t old, int32_t newVal) override {
 *         resolution_ = RESOLUTION_TABLE[newVal + 5];  // -5..+5 → 0..10
 *     }
 *     // ... other overrides
 * };
 * @endcode
 */
class ITaskProfile {
public:
    virtual ~ITaskProfile() = default;

    // =========================================================================
    // Identity (concrete - common to all profiles)
    // =========================================================================

    /// Internal ID (auto-generated, used by registry for O(1) lookup)
    [[nodiscard]] uint64_t GetProfileId() const { return profileId_; }

    /// Display name for debugging/logging
    [[nodiscard]] const std::string& GetName() const { return name_; }

    /// Category for bulk operations
    [[nodiscard]] const std::string& GetCategory() const { return category_; }

    [[nodiscard]] uint8_t GetPriority() const { return priority_; }
    [[nodiscard]] WorkUnitType GetWorkUnitType() const { return workUnitType_; }

    // Internal - called by registry during registration
    void SetProfileId(uint64_t id) { profileId_ = id; }
    void SetName(const std::string& name) { name_ = name; }
    void SetCategory(const std::string& cat) { category_ = cat; }
    void SetPriority(uint8_t p) { priority_ = p; }
    void SetWorkUnitType(WorkUnitType t) { workUnitType_ = t; }

    // Legacy compatibility - maps to GetName()
    [[nodiscard]] const std::string& GetTaskId() const { return name_; }
    void SetTaskId(const std::string& id) { name_ = id; }

    // =========================================================================
    // Pressure Valve (concrete base implementation)
    // =========================================================================

    [[nodiscard]] int32_t GetWorkUnits() const { return workUnits_; }
    [[nodiscard]] int32_t GetMinWorkUnits() const { return minWorkUnits_; }
    [[nodiscard]] int32_t GetMaxWorkUnits() const { return maxWorkUnits_; }

    void SetBounds(int32_t min, int32_t max) {
        minWorkUnits_ = min;
        maxWorkUnits_ = max;
        workUnits_ = std::clamp(workUnits_, minWorkUnits_, maxWorkUnits_);
    }

    [[nodiscard]] bool CanIncrease() const { return workUnits_ < maxWorkUnits_; }
    [[nodiscard]] bool CanDecrease() const { return workUnits_ > minWorkUnits_; }

    /**
     * @brief Increase workUnits by 1 (if below max)
     *
     * Calls OnWorkUnitsChanged() virtual hook for task-specific reaction.
     *
     * @return true if increased, false if already at max
     */
    bool Increase() {
        if (!CanIncrease()) return false;
        int32_t oldUnits = workUnits_;
        ++workUnits_;
        OnWorkUnitsChanged(oldUnits, workUnits_);
        return true;
    }

    /**
     * @brief Decrease workUnits by 1 (if above min)
     *
     * Calls OnWorkUnitsChanged() virtual hook for task-specific reaction.
     *
     * @return true if decreased, false if already at min
     */
    bool Decrease() {
        if (!CanDecrease()) return false;
        int32_t oldUnits = workUnits_;
        --workUnits_;
        OnWorkUnitsChanged(oldUnits, workUnits_);
        return true;
    }

    /**
     * @brief Set workUnits to specific value (clamped)
     *
     * Calls OnWorkUnitsChanged() if value actually changed.
     */
    void SetWorkUnits(int32_t units) {
        int32_t clamped = std::clamp(units, minWorkUnits_, maxWorkUnits_);
        if (clamped != workUnits_) {
            int32_t oldUnits = workUnits_;
            workUnits_ = clamped;
            OnWorkUnitsChanged(oldUnits, workUnits_);
        }
    }

    /**
     * @brief Get normalized pressure level
     *
     * @return -1.0 (min) to 0.0 (baseline) to +1.0 (max)
     */
    [[nodiscard]] float GetPressure() const {
        if (workUnits_ == 0) return 0.0f;
        if (workUnits_ > 0 && maxWorkUnits_ > 0) {
            return static_cast<float>(workUnits_) / static_cast<float>(maxWorkUnits_);
        }
        if (workUnits_ < 0 && minWorkUnits_ < 0) {
            return static_cast<float>(workUnits_) / static_cast<float>(-minWorkUnits_);
        }
        return 0.0f;
    }

    // =========================================================================
    // Statistics (concrete - tracked by base class)
    // =========================================================================

    [[nodiscard]] uint32_t GetSampleCount() const { return sampleCount_; }
    [[nodiscard]] uint64_t GetLastMeasuredCostNs() const { return lastMeasuredCostNs_; }
    [[nodiscard]] uint64_t GetPeakMeasuredCostNs() const { return peakMeasuredCostNs_; }
    [[nodiscard]] bool IsCalibrated() const { return isCalibrated_; }

    // =========================================================================
    // Virtual Interface - Derived classes MUST implement
    // =========================================================================

    /**
     * @brief Called when workUnits changes - task applies the change
     *
     * Override to implement task-specific behavior:
     * - ShadowMap: change resolution via lookup table
     * - BatchDispatch: adjust items per dispatch
     * - VoxelTraversal: modify max ray steps
     *
     * @param oldUnits Previous workUnits value
     * @param newUnits New workUnits value
     */
    virtual void OnWorkUnitsChanged(int32_t oldUnits, int32_t newUnits) = 0;

    /**
     * @brief Get estimated cost at current workUnits
     *
     * Task knows its own cost model:
     * - Some scale linearly with workUnits
     * - Some scale quadratically (resolution-based)
     * - Some have stepped costs (LOD levels)
     *
     * @return Estimated execution time in nanoseconds
     */
    [[nodiscard]] virtual uint64_t GetEstimatedCostNs() const = 0;

    /**
     * @brief Record actual measurement - task updates its calibration
     *
     * Called after task execution with actual measured time.
     * Task can use this to refine its cost model.
     *
     * @param actualNs Measured execution time in nanoseconds
     */
    virtual void RecordMeasurement(uint64_t actualNs) {
        lastMeasuredCostNs_ = actualNs;
        peakMeasuredCostNs_ = std::max(peakMeasuredCostNs_, actualNs);
        ++sampleCount_;
        isCalibrated_ = true;

        // Derived classes should call base then add their logic:
        // ITaskProfile::RecordMeasurement(actualNs);
        // // ... task-specific calibration update
    }

    /**
     * @brief Save task-specific state to JSON
     *
     * Base implementation saves common fields. Derived classes
     * should call base then add their own members:
     *
     * @code
     * void SaveState(json& j) const override {
     *     ITaskProfile::SaveState(j);
     *     j["resolution"] = currentResolution_;
     *     j["calibrationData"] = myCalibrationArray_;
     * }
     * @endcode
     *
     * @param j JSON object to write to
     */
    virtual void SaveState(nlohmann::json& j) const {
        j["name"] = name_;
        j["category"] = category_;
        j["typeName"] = GetTypeName();
        j["workUnits"] = workUnits_;
        j["minWorkUnits"] = minWorkUnits_;
        j["maxWorkUnits"] = maxWorkUnits_;
        j["priority"] = priority_;
        j["workUnitType"] = static_cast<int>(workUnitType_);
        j["sampleCount"] = sampleCount_;
        j["lastMeasuredCostNs"] = lastMeasuredCostNs_;
        j["peakMeasuredCostNs"] = peakMeasuredCostNs_;
        j["isCalibrated"] = isCalibrated_;
        // Note: profileId_ not saved - regenerated on load
    }

    /**
     * @brief Load task-specific state from JSON
     *
     * Base implementation loads common fields. Derived classes
     * should call base then load their own members:
     *
     * @code
     * void LoadState(const json& j) override {
     *     ITaskProfile::LoadState(j);
     *     if (j.contains("resolution")) {
     *         currentResolution_ = j["resolution"];
     *     }
     * }
     * @endcode
     *
     * @param j JSON object to read from
     */
    virtual void LoadState(const nlohmann::json& j) {
        // Legacy support: "taskId" maps to name_
        if (j.contains("name")) name_ = j["name"];
        else if (j.contains("taskId")) name_ = j["taskId"];
        if (j.contains("category")) category_ = j["category"];
        if (j.contains("workUnits")) workUnits_ = j["workUnits"];
        if (j.contains("minWorkUnits")) minWorkUnits_ = j["minWorkUnits"];
        if (j.contains("maxWorkUnits")) maxWorkUnits_ = j["maxWorkUnits"];
        if (j.contains("priority")) priority_ = j["priority"];
        if (j.contains("workUnitType")) workUnitType_ = static_cast<WorkUnitType>(j["workUnitType"].get<int>());
        if (j.contains("sampleCount")) sampleCount_ = j["sampleCount"];
        if (j.contains("lastMeasuredCostNs")) lastMeasuredCostNs_ = j["lastMeasuredCostNs"];
        if (j.contains("peakMeasuredCostNs")) peakMeasuredCostNs_ = j["peakMeasuredCostNs"];
        if (j.contains("isCalibrated")) isCalibrated_ = j["isCalibrated"];

        // Clamp workUnits to bounds after loading
        workUnits_ = std::clamp(workUnits_, minWorkUnits_, maxWorkUnits_);
    }

    /**
     * @brief Get type name for factory pattern / serialization
     *
     * Used to recreate the correct derived type when loading from JSON.
     * Return a unique identifier like "ShadowMapProfile", "BatchDispatchProfile".
     *
     * @return Type identifier string
     */
    [[nodiscard]] virtual std::string GetTypeName() const = 0;

    /**
     * @brief Get human-readable description of current state
     *
     * For debugging/logging. Example output:
     * "shadowMap_cascade0: resolution=1024x1024 (workUnits=+2, cost=2.1ms)"
     *
     * @return State description string
     */
    [[nodiscard]] virtual std::string GetStateDescription() const = 0;

    /**
     * @brief Reset calibration to initial state
     *
     * Derived classes should override to reset their specific calibration data.
     */
    virtual void ResetCalibration() {
        workUnits_ = 0;  // Reset to baseline
        sampleCount_ = 0;
        lastMeasuredCostNs_ = 0;
        peakMeasuredCostNs_ = 0;
        isCalibrated_ = false;
    }

protected:
    // Common state - derived classes can access
    uint64_t profileId_ = 0;     ///< Internal ID (auto-assigned by registry)
    std::string name_;           ///< Display name for debugging
    std::string category_;       ///< Category for bulk operations

    int32_t workUnits_ = 0;
    int32_t minWorkUnits_ = -5;
    int32_t maxWorkUnits_ = +5;

    uint8_t priority_ = 128;
    WorkUnitType workUnitType_ = WorkUnitType::Custom;

    // Statistics
    uint32_t sampleCount_ = 0;
    uint64_t lastMeasuredCostNs_ = 0;
    uint64_t peakMeasuredCostNs_ = 0;
    bool isCalibrated_ = false;
};

/**
 * @brief Factory function type for creating task profiles
 *
 * Used by TaskProfileRegistry to recreate derived types from JSON.
 */
using TaskProfileFactory = std::function<std::unique_ptr<ITaskProfile>()>;

/**
 * @brief Callback for workUnit changes
 *
 * Nodes can register to be notified when their workUnits change,
 * allowing them to reconfigure their workload.
 *
 * @param taskId The task that changed
 * @param oldUnits Previous workUnits value (can be negative)
 * @param newUnits New workUnits value (can be negative)
 */
using WorkUnitChangeCallback = std::function<void(const std::string& taskId, int32_t oldUnits, int32_t newUnits)>;

} // namespace Vixen::RenderGraph
