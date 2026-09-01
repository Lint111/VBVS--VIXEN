// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file TaskExecutor.h
 * @brief Renderer-independent task-wave executor and budgeted blocking lane.
 *
 * Extracted from RenderGraph's Core/TBBVirtualTaskExecutor.h (spec 14:1783-1786). The render
 * original's Build() pulled the whole render stack in: it took a VirtualResourceAccessTracker
 * (Tier-B hazard model) and re-fetched each task's callable via NodeInstance::GetExecutionTasks().
 * That coupling is REMOVED -- the caller hands in a `vector<VirtualTask>` and a wave layering
 * directly. What remains is the essence: `tbb::parallel_for_each` over each wave, exception
 * collection, and (D1's determinism gate, spec :1387) an explicit worker-count control so the same
 * input yields byte-identical output under 1 / 2 / N workers.
 *
 * @see TaskDependencyGraph for producing the waves.
 * @see VirtualTask for the task shape.
 */

#include "TaskDependencyGraph.h"
#include "VirtualTask.h"
#include "Abi.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace Vixen::KernelDispatch {

/// Error from a failed task.
struct TaskError {
    TaskId task;
    std::string message;
};

/// Terminal state of a task submitted to the blocking lane.
enum class CompletionStatus : uint8_t {
    Pending,
    Running,
    Succeeded,
    Failed,
    Cancelled,
};

/**
 * @brief Completion record for one submitted blocking-lane task.
 *
 * A false-returning callable is a failed task; an exception is also captured as Failed. A task
 * whose stop token is observed while queued is Cancelled and its callable is not invoked. A stop
 * request after a callable has started is cooperative: the callable is allowed to finish because
 * file and compiler operations cannot be safely interrupted at this seam.
 */
struct CompletionResult {
    CompletionStatus status = CompletionStatus::Pending;
    std::string errorMessage;

    [[nodiscard]] bool Succeeded() const noexcept { return status == CompletionStatus::Succeeded; }
    [[nodiscard]] bool Cancelled() const noexcept { return status == CompletionStatus::Cancelled; }
};

/**
 * @brief Explicitly ordered completion handle for one blocking-lane submission.
 *
 * `SubmissionOrder()` is monotonically increasing within its TaskExecutor. Callers that need
 * deterministic result publication retain handles in submission order and wait/read them in that
 * order; worker completion timing is deliberately not exposed as ordering. The handle is
 * copyable and keeps its completion state alive independently of the submitting subsystem.
 */
class CompletionHandle {
public:
    CompletionHandle() = default;

    [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(state_); }
    [[nodiscard]] uint64_t SubmissionOrder() const noexcept;
    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] CompletionResult Wait() const;
    [[nodiscard]] CompletionResult Get() const { return Wait(); }
    [[nodiscard]] bool WaitFor(std::chrono::milliseconds timeout) const;
    [[nodiscard]] CompletionResult Result() const;

private:
    struct State {
        explicit State(uint64_t order) : submissionOrder(order) {}

        mutable std::mutex mutex;
        mutable std::condition_variable condition;
        uint64_t submissionOrder = 0;
        CompletionResult result;
    };

    explicit CompletionHandle(std::shared_ptr<State> state) : state_(std::move(state)) {}

    std::shared_ptr<State> state_;
    friend class TaskExecutor;
};

/**
 * @brief Runs a set of tasks wave-by-wave; tasks within a wave run in parallel via TBB.
 *
 * `workerCount` selects the TBB arena size for THIS execution: 1 = fully serial, N = N workers.
 * The determinism contract (spec :1387) is that Run() produces identical results for any value --
 * true here because per-element writes are independent (no reducer, no shared accumulator).
 *
 * Thread safety: Run() is single-caller (it may spawn TBB workers internally).
 */
class TaskExecutor {
public:
    explicit TaskExecutor(DispatcherProfile profile = {});
    ~TaskExecutor();

    TaskExecutor(const TaskExecutor&) = delete;
    TaskExecutor& operator=(const TaskExecutor&) = delete;

    /**
     * @brief Execute `tasks` grouped by `waves`, using `workerCount` TBB workers.
     *
     * @param tasks     The work units (by id -> callable).
     * @param waves     Task-id waves from TaskDependencyGraph::GetParallelLevels(); each wave's
     *                  tasks run concurrently, waves run in order.
     * @param workerCount TBB arena concurrency for this run (>=1). 1 forces serial; 0 selects
     *                    the profile's frame-compute budget.
     * @param stopToken Cooperative execution cancellation. A stopped token prevents new tasks and
     *                  later waves from being issued; already-running tasks finish normally.
     * @return true if every task completed without throwing and cancellation was not requested.
     */
    bool Run(std::vector<VirtualTask>& tasks,
             const std::vector<std::vector<TaskId>>& waves,
             int workerCount,
             std::stop_token stopToken = {});

    /**
     * @brief Submit one blocking/I/O task to the separately budgeted lane.
     *
     * The lane starts lazily at the profile's `blockingIO.workerCount` budget (or its bounded
     * default). Queued cancellation is checked before invocation. The returned handle is the
     * ordered result contract for this task.
     */
    CompletionHandle SubmitBlocking(std::function<bool()> task,
                                    std::stop_token stopToken = {});

    /**
     * @brief Submit an aggregate of blocking tasks without creating an ad-hoc thread/future per task.
     *
     * The returned future resolves only after every operation has reached a terminal state and is
     * true only when every operation succeeded. The input vector is the publication order. This
     * adapter preserves CashSystem's existing `std::future<bool>` API while execution remains on
     * the blocking lane.
     */
    std::future<bool> SubmitBlockingBatch(std::vector<std::function<bool()>> tasks,
                                          std::stop_token stopToken = {});

    /**
     * @brief Wait until all currently submitted blocking work has reached a terminal state.
     *
     * Owners call this before destroying data captured by lane tasks. It does not cancel work or
     * prevent a later submission; normal subsystem lifetime rules still require no new submits.
     */
    void WaitForBlocking();

    [[nodiscard]] const DispatcherProfile& Profile() const noexcept { return profile_; }

    [[nodiscard]] const std::vector<TaskError>& GetErrors() const { return errors_; }
    [[nodiscard]] bool HasErrors() const { return !errors_.empty(); }

private:
    struct BatchState;
    struct BlockingWork {
        using CancellationCallback = std::stop_callback<std::function<void()>>;

        std::function<bool()> task;
        std::stop_token stopToken;
        std::shared_ptr<CompletionHandle::State> completion;
        std::shared_ptr<BatchState> batch;
        std::shared_ptr<CancellationCallback> cancellationCallback;
    };

    std::vector<TaskError> errors_;
    DispatcherProfile profile_;

    mutable std::mutex blockingMutex_;
    std::condition_variable blockingCondition_;
    std::deque<BlockingWork> blockingQueue_;
    std::vector<std::thread> blockingWorkers_;
    bool blockingStopping_ = false;
    size_t blockingOutstanding_ = 0;
    std::condition_variable blockingIdleCondition_;
    std::atomic<uint64_t> nextSubmissionOrder_{0};

    VirtualTask* FindTask(std::vector<VirtualTask>& tasks, const TaskId& id) const;
    bool RunWave(std::vector<VirtualTask>& tasks,
                 const std::vector<TaskId>& wave,
                 std::stop_token stopToken);

    void EnsureBlockingWorkersLocked();
    void BlockingWorkerLoop();
    void ExecuteBlockingWork(BlockingWork& work);
    void EnqueueBlocking(BlockingWork work);
};

}  // namespace Vixen::KernelDispatch
