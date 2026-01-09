// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file TaskProfileRegistry.h
 * @brief Central registry for polymorphic task calibration profiles
 *
 * Sprint 6.3: Phase 3.2a - TaskProfileRegistry (Polymorphic)
 * Design Element: #38 Timeline Capacity Tracker
 *
 * Manages all ITaskProfile instances in the system:
 * - Registration of polymorphic task profiles
 * - Factory pattern for deserialization
 * - Measurement recording and calibration
 * - Priority-based pressure adjustment
 * - Category-based bulk operations
 *
 * @see ITaskProfile for abstract interface
 * @see TimelineCapacityTracker for budget monitoring
 */

#include "ITaskProfile.h"
#include "MessageBus.h"  // Sprint 6.3: Event-driven architecture
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <string>
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Central registry for managing polymorphic task profiles
 *
 * Provides unified access to all task profiles for:
 * - Registration (nodes register their tasks with specific profile types)
 * - Factory pattern (recreate correct derived type from JSON)
 * - Measurement recording (after execution)
 * - Pressure adjustment (based on budget utilization)
 * - Category operations (adjust all shadows at once)
 *
 * Usage:
 * @code
 * TaskProfileRegistry registry;
 *
 * // Register factory for custom profile types
 * registry.RegisterFactory("ResolutionTaskProfile", []() {
 *     return std::make_unique<ResolutionTaskProfile>();
 * });
 *
 * // Node registration during Setup
 * auto profile = std::make_unique<ResolutionTaskProfile>(
 *     "shadowMap", "shadow", resolutionTable);
 * registry.RegisterTask(std::move(profile));
 *
 * // After execution
 * registry.RecordMeasurement("shadowMap", actualNs);
 *
 * // When over budget
 * registry.DecreaseLowestPriority();
 *
 * // When under budget
 * registry.IncreaseHighestPriority();
 * @endcode
 */
class TaskProfileRegistry {
public:
    TaskProfileRegistry() = default;
    ~TaskProfileRegistry() = default;

    // Non-copyable, movable
    TaskProfileRegistry(const TaskProfileRegistry&) = delete;
    TaskProfileRegistry& operator=(const TaskProfileRegistry&) = delete;
    TaskProfileRegistry(TaskProfileRegistry&&) noexcept = default;
    TaskProfileRegistry& operator=(TaskProfileRegistry&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /**
     * @brief Initialize registry with built-in profile factories
     *
     * Registers factories for SimpleTaskProfile and ResolutionTaskProfile.
     * Must be called before LoadState() to enable deserialization.
     *
     * Safe to call multiple times (idempotent).
     */
    void Init();

    /**
     * @brief Check if Init() has been called
     */
    [[nodiscard]] bool IsInitialized() const { return initialized_; }

    // =========================================================================
    // Factory Registration (for polymorphic deserialization)
    // =========================================================================

    /**
     * @brief Register a factory for creating task profiles from type name
     *
     * Required for loading profiles from JSON - each derived type needs
     * a factory registered before it can be deserialized.
     *
     * @param typeName The GetTypeName() return value for this type
     * @param factory Function that creates a default instance of this type
     */
    void RegisterFactory(const std::string& typeName, TaskProfileFactory factory) {
        factories_[typeName] = std::move(factory);
    }

    /**
     * @brief Check if a factory is registered for a type
     */
    [[nodiscard]] bool HasFactory(const std::string& typeName) const {
        return factories_.find(typeName) != factories_.end();
    }

    // =========================================================================
    // Task Registration
    // =========================================================================

    /**
     * @brief Register a task profile (takes ownership)
     *
     * If a profile with the same taskId exists, it is replaced.
     *
     * @param profile Profile to register (ownership transferred)
     * @return Pointer to registered profile (non-owning)
     */
    ITaskProfile* RegisterTask(std::unique_ptr<ITaskProfile> profile) {
        if (!profile) return nullptr;

        std::string taskId = profile->GetTaskId();
        auto* ptr = profile.get();
        profiles_[taskId] = std::move(profile);
        InvalidateSortedCache();
        return ptr;
    }

    /**
     * @brief Get a profile by task ID
     *
     * @param taskId Task identifier
     * @return Pointer to profile (nullptr if not found)
     */
    [[nodiscard]] ITaskProfile* GetProfile(const std::string& taskId) {
        auto it = profiles_.find(taskId);
        return (it != profiles_.end()) ? it->second.get() : nullptr;
    }

    [[nodiscard]] const ITaskProfile* GetProfile(const std::string& taskId) const {
        auto it = profiles_.find(taskId);
        return (it != profiles_.end()) ? it->second.get() : nullptr;
    }

    /**
     * @brief Check if a task is registered
     */
    [[nodiscard]] bool HasTask(const std::string& taskId) const {
        return profiles_.find(taskId) != profiles_.end();
    }

    /**
     * @brief Unregister a task
     */
    void UnregisterTask(const std::string& taskId) {
        profiles_.erase(taskId);
        InvalidateSortedCache();
    }

    /**
     * @brief Get number of registered tasks
     */
    [[nodiscard]] size_t GetTaskCount() const {
        return profiles_.size();
    }

    /**
     * @brief Get all task IDs
     */
    [[nodiscard]] std::vector<std::string> GetTaskIds() const {
        std::vector<std::string> ids;
        ids.reserve(profiles_.size());
        for (const auto& [id, _] : profiles_) {
            ids.push_back(id);
        }
        return ids;
    }

    // =========================================================================
    // Measurement Recording
    // =========================================================================

    /**
     * @brief Record execution time for a task
     *
     * Updates the task's calibration with the actual measurement.
     *
     * @param taskId Task identifier
     * @param actualNs Measured execution time in nanoseconds
     * @return true if task exists and measurement recorded
     */
    bool RecordMeasurement(const std::string& taskId, uint64_t actualNs) {
        auto* profile = GetProfile(taskId);
        if (profile) {
            profile->RecordMeasurement(actualNs);
            return true;
        }
        return false;
    }

    // =========================================================================
    // Pressure Adjustment
    // =========================================================================

    /**
     * @brief Decrease workUnits on lowest-priority task that can decrease
     *
     * Used when system is over budget. Finds the task with lowest priority
     * that still has room to decrease, and reduces its workUnits by 1.
     *
     * @return taskId of adjusted task (empty if none available)
     */
    std::string DecreaseLowestPriority() {
        EnsureSortedCache();

        // sortedByPriority_ is sorted ascending (lowest priority first)
        for (auto* profile : sortedByPriority_) {
            if (profile->CanDecrease()) {
                int32_t oldUnits = profile->GetWorkUnits();
                profile->Decrease();
                NotifyChange(profile->GetTaskId(), oldUnits, profile->GetWorkUnits());
                return profile->GetTaskId();
            }
        }
        return "";  // No task can decrease
    }

    /**
     * @brief Increase workUnits on highest-priority task that can increase
     *
     * Used when system is under budget. Finds the task with highest priority
     * that still has room to increase, and increases its workUnits by 1.
     *
     * @return taskId of adjusted task (empty if none available)
     */
    std::string IncreaseHighestPriority() {
        EnsureSortedCache();

        // sortedByPriority_ is sorted ascending, so iterate in reverse
        for (auto it = sortedByPriority_.rbegin(); it != sortedByPriority_.rend(); ++it) {
            ITaskProfile* profile = *it;
            if (profile->CanIncrease()) {
                int32_t oldUnits = profile->GetWorkUnits();
                profile->Increase();
                NotifyChange(profile->GetTaskId(), oldUnits, profile->GetWorkUnits());
                return profile->GetTaskId();
            }
        }
        return "";  // No task can increase
    }

    /**
     * @brief Apply global pressure adjustment
     *
     * Adjusts workUnits across all tasks to approach target utilization.
     * - If current > target: decrease lowest priority tasks
     * - If current < target: increase highest priority tasks
     *
     * @param currentUtilization Current system utilization (0.0-1.0+)
     * @param targetUtilization Target utilization (default 0.9 = 90%)
     * @return Number of tasks adjusted
     */
    uint32_t ApplyPressure(float currentUtilization, float targetUtilization = 0.9f) {
        constexpr float DEADBAND = 0.05f;  // ±5% deadband
        uint32_t adjusted = 0;

        float delta = currentUtilization - targetUtilization;

        if (delta > DEADBAND) {
            // Over target: decrease one task
            if (!DecreaseLowestPriority().empty()) {
                ++adjusted;
            }
        } else if (delta < -DEADBAND) {
            // Under target: increase one task
            if (!IncreaseHighestPriority().empty()) {
                ++adjusted;
            }
        }
        // Within deadband: no adjustment

        return adjusted;
    }

    // =========================================================================
    // Category Operations
    // =========================================================================

    /**
     * @brief Get all tasks in a category
     *
     * @param category Category name
     * @return Vector of profile pointers (non-owning)
     */
    [[nodiscard]] std::vector<ITaskProfile*> GetTasksByCategory(const std::string& category) {
        std::vector<ITaskProfile*> result;
        for (auto& [id, profile] : profiles_) {
            if (profile->GetCategory() == category) {
                result.push_back(profile.get());
            }
        }
        return result;
    }

    /**
     * @brief Set priority for all tasks in a category
     *
     * @param category Category name
     * @param priority New priority for all tasks
     */
    void SetCategoryPriority(const std::string& category, uint8_t priority) {
        for (auto& [id, profile] : profiles_) {
            if (profile->GetCategory() == category) {
                profile->SetPriority(priority);
            }
        }
        InvalidateSortedCache();
    }

    /**
     * @brief Decrease all tasks in a category by 1 workUnit
     *
     * @param category Category name
     * @return Number of tasks decreased
     */
    uint32_t DecreaseCategoryWorkUnits(const std::string& category) {
        uint32_t count = 0;
        for (auto& [id, profile] : profiles_) {
            if (profile->GetCategory() == category && profile->CanDecrease()) {
                int32_t oldUnits = profile->GetWorkUnits();
                profile->Decrease();
                NotifyChange(profile->GetTaskId(), oldUnits, profile->GetWorkUnits());
                ++count;
            }
        }
        return count;
    }

    /**
     * @brief Increase all tasks in a category by 1 workUnit
     *
     * @param category Category name
     * @return Number of tasks increased
     */
    uint32_t IncreaseCategoryWorkUnits(const std::string& category) {
        uint32_t count = 0;
        for (auto& [id, profile] : profiles_) {
            if (profile->GetCategory() == category && profile->CanIncrease()) {
                int32_t oldUnits = profile->GetWorkUnits();
                profile->Increase();
                NotifyChange(profile->GetTaskId(), oldUnits, profile->GetWorkUnits());
                ++count;
            }
        }
        return count;
    }

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get total estimated cost across all tasks
     */
    [[nodiscard]] uint64_t GetTotalEstimatedCostNs() const {
        uint64_t total = 0;
        for (const auto& [id, profile] : profiles_) {
            total += profile->GetEstimatedCostNs();
        }
        return total;
    }

    /**
     * @brief Get average pressure across all tasks
     *
     * Returns value in range [-1, 1]:
     * - Negative: tasks are below baseline (reduced quality)
     * - Zero: tasks at baseline
     * - Positive: tasks above baseline (increased quality)
     */
    [[nodiscard]] float GetAveragePressure() const {
        if (profiles_.empty()) return 0.0f;

        float sum = 0.0f;
        for (const auto& [id, profile] : profiles_) {
            sum += profile->GetPressure();
        }
        return sum / static_cast<float>(profiles_.size());
    }

    /**
     * @brief Get number of calibrated tasks
     */
    [[nodiscard]] size_t GetCalibratedCount() const {
        size_t count = 0;
        for (const auto& [id, profile] : profiles_) {
            if (profile->IsCalibrated()) {
                ++count;
            }
        }
        return count;
    }

    // =========================================================================
    // Change Notification
    // =========================================================================

    /**
     * @brief Set callback for workUnit changes
     *
     * Called whenever a task's workUnits changes via pressure adjustment.
     * Nodes use this to adapt their workload (e.g., reduce shadow resolution).
     *
     * @param callback Function to call on change (nullptr to disable)
     */
    void SetChangeCallback(WorkUnitChangeCallback callback) {
        changeCallback_ = std::move(callback);
    }

    // =========================================================================
    // Persistence
    // =========================================================================

    /**
     * @brief Save all profiles to JSON
     *
     * @param j JSON object to write to
     */
    void SaveState(nlohmann::json& j) const {
        j["profiles"] = nlohmann::json::array();
        for (const auto& [id, profile] : profiles_) {
            nlohmann::json profileJson;
            profile->SaveState(profileJson);
            j["profiles"].push_back(profileJson);
        }
    }

    /**
     * @brief Load profiles from JSON
     *
     * Requires factories to be registered for each profile type.
     * Profiles with unregistered types are skipped.
     *
     * @param j JSON object to read from
     * @return Number of profiles loaded
     */
    size_t LoadState(const nlohmann::json& j) {
        size_t loaded = 0;
        if (!j.contains("profiles") || !j["profiles"].is_array()) {
            return 0;
        }

        for (const auto& profileJson : j["profiles"]) {
            if (!profileJson.contains("typeName")) {
                continue;
            }

            std::string typeName = profileJson["typeName"];
            auto factoryIt = factories_.find(typeName);
            if (factoryIt == factories_.end()) {
                // No factory registered for this type
                continue;
            }

            // Create instance via factory
            auto profile = factoryIt->second();
            if (!profile) continue;

            // Load state
            profile->LoadState(profileJson);

            // Register
            RegisterTask(std::move(profile));
            ++loaded;
        }

        return loaded;
    }

    // =========================================================================
    // Bulk Operations
    // =========================================================================

    /**
     * @brief Reset calibration for all tasks
     */
    void ResetAllCalibration() {
        for (auto& [id, profile] : profiles_) {
            profile->ResetCalibration();
        }
    }

    /**
     * @brief Clear all registered tasks
     */
    void Clear() {
        profiles_.clear();
        InvalidateSortedCache();
    }

    // =========================================================================
    // Event-Driven Architecture (Sprint 6.3)
    // =========================================================================

    /**
     * @brief Subscribe to budget events via MessageBus
     *
     * Enables autonomous pressure adjustment. When subscribed:
     * - BudgetOverrunEvent → queues DecreaseLowestPriority() for deferred execution
     * - BudgetAvailableEvent → queues IncreaseHighestPriority() for deferred execution
     *
     * IMPORTANT: Event handlers queue actions instead of executing immediately
     * to prevent deadlock when events are published during locks.
     * Call ProcessDeferredActions() at a safe point outside of locks.
     *
     * This decouples TaskProfileRegistry from RenderGraph - it reacts
     * directly to events published by TimelineCapacityTracker.
     *
     * @param messageBus MessageBus to subscribe to (non-owning)
     */
    void SubscribeToBudgetEvents(EventBus::MessageBus* messageBus) {
        if (!messageBus) return;

        // ScopedSubscriptions handles unsubscribe automatically
        subscriptions_.SetBus(messageBus);

        // Subscribe to BudgetOverrunEvent (type-safe, clean syntax)
        // Uses deferred execution to avoid deadlock during event dispatch
        subscriptions_.Subscribe<EventBus::BudgetOverrunEvent>(
            [this](const EventBus::BudgetOverrunEvent& e) {
                // Over budget: queue workload reduction (deferred to avoid deadlock)
                pendingDecrease_ = true;
            }
        );

        // Subscribe to BudgetAvailableEvent
        // Uses deferred execution to avoid deadlock during event dispatch
        subscriptions_.Subscribe<EventBus::BudgetAvailableEvent>(
            [this](const EventBus::BudgetAvailableEvent& e) {
                // Under threshold: queue workload increase (deferred to avoid deadlock)
                pendingIncrease_ = true;
            }
        );
    }

    /**
     * @brief Process deferred pressure adjustments
     *
     * Call this at a safe point outside of locks (e.g., at frame end
     * after event dispatch completes). Executes any queued pressure
     * adjustments from budget events.
     *
     * @return Number of adjustments made (0-2)
     */
    uint32_t ProcessDeferredActions() {
        uint32_t adjustments = 0;

        // Process pending decrease first (higher priority - prevent overrun)
        if (pendingDecrease_) {
            pendingDecrease_ = false;
            if (!DecreaseLowestPriority().empty()) {
                ++adjustments;
            }
        }

        // Process pending increase
        if (pendingIncrease_) {
            pendingIncrease_ = false;
            if (!IncreaseHighestPriority().empty()) {
                ++adjustments;
            }
        }

        return adjustments;
    }

    /**
     * @brief Check if there are pending deferred actions
     */
    [[nodiscard]] bool HasPendingActions() const {
        return pendingDecrease_ || pendingIncrease_;
    }

    /**
     * @brief Unsubscribe from budget events
     *
     * Note: Also happens automatically via RAII when object is destroyed.
     */
    void UnsubscribeFromBudgetEvents() { subscriptions_.UnsubscribeAll(); }

    /**
     * @brief Check if subscribed to budget events
     */
    [[nodiscard]] bool IsSubscribed() const { return subscriptions_.HasSubscriptions(); }

private:
    void InvalidateSortedCache() {
        sortedCacheValid_ = false;
    }

    void EnsureSortedCache() {
        if (sortedCacheValid_) return;

        sortedByPriority_.clear();
        sortedByPriority_.reserve(profiles_.size());

        for (auto& [id, profile] : profiles_) {
            sortedByPriority_.push_back(profile.get());
        }

        // Sort by priority ascending (lowest first for decrease operations)
        std::sort(sortedByPriority_.begin(), sortedByPriority_.end(),
            [](const ITaskProfile* a, const ITaskProfile* b) {
                return a->GetPriority() < b->GetPriority();
            });

        sortedCacheValid_ = true;
    }

    void NotifyChange(const std::string& taskId, int32_t oldUnits, int32_t newUnits) {
        if (changeCallback_ && oldUnits != newUnits) {
            changeCallback_(taskId, oldUnits, newUnits);
        }
    }

    // Profile storage (owns the profiles)
    std::unordered_map<std::string, std::unique_ptr<ITaskProfile>> profiles_;

    // Factory registry for polymorphic deserialization
    std::unordered_map<std::string, TaskProfileFactory> factories_;

    // Cached sorted list for priority-based operations
    std::vector<ITaskProfile*> sortedByPriority_;
    bool sortedCacheValid_ = false;

    WorkUnitChangeCallback changeCallback_;

    // Sprint 6.3: Event-driven architecture (RAII subscriptions)
    EventBus::ScopedSubscriptions subscriptions_;

    // Sprint 6.3: Deferred action flags (prevents deadlock during event dispatch)
    // These are set by event handlers and processed by ProcessDeferredActions()
    bool pendingDecrease_ = false;
    bool pendingIncrease_ = false;

    // Sprint 6.5: Initialization flag for built-in factories
    bool initialized_ = false;
};

} // namespace Vixen::RenderGraph
