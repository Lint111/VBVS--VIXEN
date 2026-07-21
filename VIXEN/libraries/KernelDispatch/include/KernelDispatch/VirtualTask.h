// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file VirtualTask.h
 * @brief Renderer-independent task-scheduling unit (Tier-A extraction, E7 dispatch D1).
 *
 * Extracted from RenderGraph's Core/VirtualTask.h (spec 14:1783-1786), DECOUPLED from render
 * identity. The render original keyed a task by {NodeInstance*, taskIndex}; here the id is an
 * OPAQUE `owner` uint64_t + taskIndex, so this library depends on NO RenderGraph type. The
 * profiling coupling (ITaskProfile pointers / nlohmann::json via ITaskProfile.h) is DROPPED -- it was
 * render-calibration machinery, not core scheduling. What remains is the essence: a task is a
 * std::function<void()> with an id, dependencies, and a state.
 *
 * @see TaskDependencyGraph for opaque-owner-keyed dependency ordering.
 * @see TaskExecutor for the TBB-wave execution over a task vector.
 */

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Vixen::KernelDispatch {

/**
 * @brief Opaque identity for a virtual task.
 *
 * The render extraction used a NodeInstance* here; we use an opaque `owner` id so the library is
 * domain-blind. `{owner, taskIndex}` is the atomic scheduling unit and a hash key.
 */
struct TaskId {
    uint64_t owner = 0;       ///< Opaque owner id (was NodeInstance* in RenderGraph).
    uint32_t taskIndex = 0;   ///< Index within an owner's bundle (0 for single-task owners).

    bool operator==(const TaskId& o) const { return owner == o.owner && taskIndex == o.taskIndex; }
    bool operator!=(const TaskId& o) const { return !(*this == o); }
    [[nodiscard]] bool IsValid() const { return owner != 0; }
};

/// Hash for TaskId -- enables use in unordered containers (same bit-mix as the render original).
struct TaskIdHash {
    size_t operator()(const TaskId& id) const {
        size_t a = std::hash<uint64_t>{}(id.owner);
        size_t b = std::hash<uint32_t>{}(id.taskIndex);
        return a ^ (b << 16) ^ (b >> 16);
    }
};

/// Execution state of a task (verbatim from the render original, minus the profiling hooks).
enum class VirtualTaskState : uint8_t {
    Pending,    ///< Dependencies not yet satisfied.
    Ready,      ///< Ready to execute.
    Running,    ///< Executing.
    Completed,  ///< Finished successfully.
    Failed,     ///< Threw during execution.
};

/**
 * @brief A schedulable unit of work: an id, a callable, dependencies, and a state.
 *
 * Thread safety: the struct is not thread-safe; TaskExecutor manages concurrent access.
 */
struct VirtualTask {
    TaskId id;
    std::function<void()> execute;          ///< The work (captures whatever it needs).
    std::vector<TaskId> dependencies;       ///< Tasks that must complete first.
    VirtualTaskState state = VirtualTaskState::Pending;
    std::string errorMessage;               ///< Set when state == Failed.

    [[nodiscard]] bool HasDependencies() const { return !dependencies.empty(); }
    void MarkCompleted() { state = VirtualTaskState::Completed; }
    void MarkFailed(const std::string& e) { state = VirtualTaskState::Failed; errorMessage = e; }
};

}  // namespace Vixen::KernelDispatch
