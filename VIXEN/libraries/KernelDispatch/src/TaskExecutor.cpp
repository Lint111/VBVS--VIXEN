// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "KernelDispatch/TaskExecutor.h"

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_arena.h>
#include <algorithm>
#include <exception>
#include <iterator>
#include <optional>
#include <utility>

// Tier-A extraction (E7 dispatch D1): the wave-then-parallel shape is the shared implementation for
// domain-blind task execution. A per-run tbb::task_arena caps concurrency so Run() is deterministic
// across worker counts (spec :1387).

namespace Vixen::KernelDispatch {

namespace {

constexpr uint32_t kDefaultBlockingWorkerBudget = 4;
thread_local TaskExecutor* g_activeBlockingExecutor = nullptr;

uint32_t DefaultBlockingWorkerCount() {
    const auto hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    return std::max(1u, std::min(kDefaultBlockingWorkerBudget, hardwareThreads));
}

} // namespace

struct TaskExecutor::BatchState {
    // The last task to finish resolves the promise; earlier ones only fold their outcome into
    // `success` (relaxed) before the acq_rel decrement that orders it for the resolver.
    std::promise<bool> result;
    std::atomic<size_t> remaining{0};
    std::atomic<bool> success{true};
};

bool CompletionHandle::State::TryStart() noexcept {
    uint32_t expected = static_cast<uint32_t>(CompletionStatus::Pending);
    return phase.compare_exchange_strong(expected, static_cast<uint32_t>(CompletionStatus::Running),
                                         std::memory_order_acq_rel, std::memory_order_acquire);
}

bool CompletionHandle::State::TryCancel(const char* message) {
    uint32_t expected = static_cast<uint32_t>(CompletionStatus::Pending);
    if (!phase.compare_exchange_strong(expected, kPublishing,
                                       std::memory_order_acq_rel, std::memory_order_acquire)) {
        return false;
    }
    errorMessage = message;
    phase.store(static_cast<uint32_t>(CompletionStatus::Cancelled), std::memory_order_release);
    signal.release();
    return true;
}

void CompletionHandle::State::Finish(CompletionResult result) {
    errorMessage = std::move(result.errorMessage);
    phase.store(static_cast<uint32_t>(result.status), std::memory_order_release);
    signal.release();
}

CompletionResult CompletionHandle::State::Snapshot() const {
    const uint32_t value = phase.load(std::memory_order_acquire);
    if (IsTerminal(value)) {
        return {static_cast<CompletionStatus>(value), errorMessage};
    }
    // Publishing is an internal hand-off to a terminal phase; callers see it as still pending.
    return {value == static_cast<uint32_t>(CompletionStatus::Running)
                ? CompletionStatus::Running : CompletionStatus::Pending, {}};
}

uint64_t CompletionHandle::SubmissionOrder() const noexcept {
    return state_ ? state_->submissionOrder : 0;
}

bool CompletionHandle::IsReady() const {
    if (!state_) return false;
    return State::IsTerminal(state_->phase.load(std::memory_order_acquire));
}

CompletionResult CompletionHandle::Wait() const {
    if (!state_) {
        return {CompletionStatus::Failed, "Invalid completion handle"};
    }

    // Off-frame only (shutdown, tests, load-time joins). The single completion permit is passed
    // on to the next waiter, so any number of copies of the handle can wait.
    if (!State::IsTerminal(state_->phase.load(std::memory_order_acquire))) {
        state_->signal.acquire();
        state_->signal.release();
    }
    return state_->Snapshot();
}

bool CompletionHandle::WaitFor(std::chrono::milliseconds timeout) const {
    if (!state_) return false;
    if (State::IsTerminal(state_->phase.load(std::memory_order_acquire))) return true;
    if (!state_->signal.try_acquire_for(timeout)) return false;
    state_->signal.release();
    return true;
}

CompletionResult CompletionHandle::Result() const {
    if (!state_) {
        return {CompletionStatus::Failed, "Invalid completion handle"};
    }
    return state_->Snapshot();
}

TaskExecutor::TaskExecutor(DispatcherProfile profile)
    : profile_(std::move(profile)) {
}

TaskExecutor::~TaskExecutor() {
    // Teardown is allowed blocking: drain the async lane, then stop and join the blocking lane.
    for (uint32_t value = asyncLane_->outstanding.load(std::memory_order_acquire);
         value != 0;
         value = asyncLane_->outstanding.load(std::memory_order_acquire)) {
        asyncLane_->outstanding.wait(value, std::memory_order_acquire);
    }
    blockingStopping_.store(true, std::memory_order_release);
    blockingPermits_.release(static_cast<std::ptrdiff_t>(blockingWorkers_.size()));
    for (auto& worker : blockingWorkers_) {
        if (worker.joinable()) worker.join();
    }
}

void TaskExecutor::EnsureBlockingWorkers() {
    uint32_t expected = 0;
    if (blockingStartState_.load(std::memory_order_acquire) != 0
        || !blockingStartState_.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        return;  // Started or starting: queued work is picked up by the workers once they run.
    }

    const auto workerCount = profile_.WorkerCount(TaskLane::BlockingIO, DefaultBlockingWorkerCount());
    blockingWorkers_.reserve(workerCount);
    for (uint32_t index = 0; index < workerCount; ++index) {
        blockingWorkers_.emplace_back(&TaskExecutor::BlockingWorkerLoop, this);
    }
    blockingStartState_.store(2, std::memory_order_release);
}

void TaskExecutor::EnqueueBlocking(BlockingWork work) {
    if (g_activeBlockingExecutor == this) {
        ExecuteBlockingWork(work);
        return;
    }

    if (blockingStopping_.load(std::memory_order_acquire)) {
        if (work.completion) work.completion->TryCancel("Task executor is stopping");
        if (work.batch) {
            work.batch->success.store(false, std::memory_order_relaxed);
            if (work.batch->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                work.batch->result.set_value(false);
            }
        }
        return;
    }

    blockingOutstanding_.fetch_add(1, std::memory_order_acq_rel);
    blockingQueue_.push(std::move(work));
    blockingPermits_.release();
    EnsureBlockingWorkers();
}

void TaskExecutor::ExecuteBlockingWork(BlockingWork& work) {
    CompletionResult result;
    bool invokeTask = true;
    if (work.completion) {
        if (work.stopToken.stop_requested()) {
            // Whoever loses the Pending claim (a cancellation callback) already published.
            work.completion->TryCancel("Task cancelled before invocation");
            invokeTask = false;
        } else if (!work.completion->TryStart()) {
            invokeTask = false;
        }
    } else if (work.stopToken.stop_requested()) {
        result = {CompletionStatus::Cancelled, "Task cancelled before invocation"};
        invokeTask = false;
    }

    if (invokeTask) {
        try {
            const bool succeeded = work.task && work.task();
            result.status = succeeded ? CompletionStatus::Succeeded : CompletionStatus::Failed;
            if (!succeeded) result.errorMessage = "Blocking task returned false";
        } catch (const std::exception& error) {
            result = {CompletionStatus::Failed, error.what()};
        } catch (...) {
            result = {CompletionStatus::Failed, "Unknown exception"};
        }
    }

    if (work.completion && invokeTask) {
        work.completion->Finish(result);
    }

    if (work.batch) {
        if (!result.Succeeded()) work.batch->success.store(false, std::memory_order_relaxed);
        if (work.batch->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            work.batch->result.set_value(work.batch->success.load(std::memory_order_acquire));
        }
    }
}

void TaskExecutor::BlockingWorkerLoop() {
    TaskExecutor* previousExecutor = g_activeBlockingExecutor;
    g_activeBlockingExecutor = this;

    for (;;) {
        blockingPermits_.acquire();  // idle park: the only wait on this lane, and only with no work
        BlockingWork work;
        if (!blockingQueue_.try_pop(work)) {
            if (blockingStopping_.load(std::memory_order_acquire)) break;
            continue;
        }
        ExecuteBlockingWork(work);
        if (blockingOutstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            blockingOutstanding_.notify_all();
        }
    }

    g_activeBlockingExecutor = previousExecutor;
}

CompletionHandle TaskExecutor::SubmitBlocking(std::function<bool()> task,
                                              std::stop_token stopToken) {
    auto state = std::make_shared<CompletionHandle::State>(
        nextSubmissionOrder_.fetch_add(1, std::memory_order_relaxed) + 1);
    CompletionHandle handle(state);
    auto cancellationCallback = std::make_shared<BlockingWork::CancellationCallback>(
        stopToken, [weakState = std::weak_ptr<CompletionHandle::State>(state)] {
            if (const auto sharedState = weakState.lock()) {
                sharedState->TryCancel("Task cancelled before invocation");
            }
        });
    EnqueueBlocking(BlockingWork{
        std::move(task), stopToken, std::move(state), nullptr, std::move(cancellationCallback)});
    return handle;
}

std::future<bool> TaskExecutor::SubmitBlockingBatch(std::vector<std::function<bool()>> tasks,
                                                    std::stop_token stopToken) {
    auto batch = std::make_shared<BatchState>();
    auto future = batch->result.get_future();
    if (tasks.empty()) {
        batch->result.set_value(true);
        return future;
    }

    batch->remaining.store(tasks.size(), std::memory_order_relaxed);
    for (auto& task : tasks) {
        nextSubmissionOrder_.fetch_add(1, std::memory_order_relaxed);
        EnqueueBlocking(BlockingWork{std::move(task), stopToken, nullptr, batch, nullptr});
    }
    return future;
}

void TaskExecutor::WaitForBlocking() {
    if (g_activeBlockingExecutor == this) return;
    // Owner-called join (cacher teardown); never on a frame path.
    for (uint32_t value = blockingOutstanding_.load(std::memory_order_acquire);
         value != 0;
         value = blockingOutstanding_.load(std::memory_order_acquire)) {
        blockingOutstanding_.wait(value, std::memory_order_acquire);
    }
}


VirtualTask* TaskExecutor::FindTask(std::vector<VirtualTask>& tasks, const TaskId& id) const {
    for (auto& t : tasks)
        if (t.id == id) return &t;
    return nullptr;
}

bool TaskExecutor::RunWave(std::vector<VirtualTask>& tasks,
                           const std::vector<TaskId>& wave,
                           std::stop_token stopToken,
                           std::vector<TaskError>& errors) {
    if (wave.empty()) return true;

    // Each worker writes only its own result slot. Errors are gathered after the wave and sorted by
    // logical task index, so completion timing can never choose the primary error.
    std::vector<std::optional<TaskError>> waveErrors(wave.size());
    const auto runTask = [&](size_t waveIndex) {
        if (stopToken.stop_requested()) return;

        VirtualTask* task = FindTask(tasks, wave[waveIndex]);
        if (!task) return;

        task->state = VirtualTaskState::Running;
        try {
            if (task->execute) task->execute();
            task->MarkCompleted();
        } catch (const std::exception& e) {
            task->MarkFailed(e.what());
            waveErrors[waveIndex] = TaskError{task->id, e.what()};
        } catch (...) {
            task->MarkFailed("Unknown exception");
            waveErrors[waveIndex] = TaskError{task->id, "Unknown exception"};
        }
    };

    // One task in the wave: run inline (matches the render original's single-task fast path).
    if (wave.size() == 1) {
        runTask(0);
    } else {
        tbb::parallel_for(size_t{0}, wave.size(), runTask);
    }

    std::vector<TaskError> orderedErrors;
    orderedErrors.reserve(waveErrors.size());
    for (auto& error : waveErrors) {
        if (error) orderedErrors.push_back(std::move(*error));
    }
    std::sort(orderedErrors.begin(), orderedErrors.end(), [](const TaskError& lhs, const TaskError& rhs) {
        if (lhs.task.taskIndex != rhs.task.taskIndex) {
            return lhs.task.taskIndex < rhs.task.taskIndex;
        }
        return lhs.task.owner < rhs.task.owner;
    });
    errors.insert(errors.end(),
                   std::make_move_iterator(orderedErrors.begin()),
                   std::make_move_iterator(orderedErrors.end()));

    return orderedErrors.empty() && !stopToken.stop_requested();
}

bool TaskExecutor::RunInCurrentArena(
    std::vector<VirtualTask>& tasks,
    const std::vector<std::vector<TaskId>>& waves,
    std::stop_token stopToken,
    std::vector<TaskError>& errors) {
    bool success = true;
    for (const auto& wave : waves) {
        const bool waveIssued = !stopToken.stop_requested();
        const bool waveSucceeded = waveIssued && RunWave(tasks, wave, stopToken, errors);
        if (!waveSucceeded) {
            success = false;
            break;
        }
    }
    return success;
}

bool TaskExecutor::Run(std::vector<VirtualTask>& tasks,
                       const std::vector<std::vector<TaskId>>& waves,
                       int workerCount,
                       std::stop_token stopToken,
                       WaveCompletionCallback onWaveComplete) {
    errors_.clear();
    if (workerCount < 1) {
        workerCount = static_cast<int>(profile_.WorkerCount(TaskLane::FrameCompute, 1));
    }
    if (stopToken.stop_requested()) return false;

    // A per-run arena caps concurrency: workerCount==1 forces serial execution, N gives N workers.
    // This is the determinism knob the spec's 1/2/N gate exercises.
    tbb::task_arena arena(workerCount);
    bool success = false;
    arena.execute([&] {
        for (const auto& wave : waves) {
            const bool waveIssued = !stopToken.stop_requested();
            const bool waveSucceeded = waveIssued && RunWave(tasks, wave, stopToken, errors_);
            if (waveIssued && onWaveComplete) onWaveComplete(wave);
            if (!waveSucceeded) {
                success = false;
                return;
            }
        }
        success = !stopToken.stop_requested();
    });
    return success;
}

std::future<AsyncRunResult> TaskExecutor::RunAsync(
    std::vector<VirtualTask> tasks,
    std::vector<std::vector<TaskId>> waves,
    int workerCount,
    std::stop_token stopToken) {
    auto promise = std::make_shared<std::promise<AsyncRunResult>>();
    auto result = promise->get_future();
    if (workerCount < 1) {
        workerCount = static_cast<int>(profile_.WorkerCount(TaskLane::FrameCompute, 1));
    }

    // First submission sizes the arena; later submissions pass straight through (call_once only
    // makes a racing first caller wait for that one initialisation).
    std::call_once(asyncArenaOnce_, [&] {
        // TBB reserves one arena slot for the thread admitting queued work. Keep an
        // additional slot so two adjacent frame submissions can overlap even when each
        // submission requests a two-worker frame budget.
        asyncArena_.initialize(std::max(2, workerCount + 1));
    });
    asyncLane_->outstanding.fetch_add(1, std::memory_order_acq_rel);

    auto asyncTasks = std::make_shared<std::vector<VirtualTask>>(std::move(tasks));
    auto asyncWaves = std::make_shared<std::vector<std::vector<TaskId>>>(std::move(waves));
    asyncArena_.enqueue([this, lane = asyncLane_, promise, asyncTasks, asyncWaves, stopToken] {
        AsyncRunResult asyncResult;
        try {
            if (!stopToken.stop_requested()) {
                // The enqueue body already executes inside asyncArena_. Calling execute() again
                // here would consume the arena's second slot and serialize adjacent submissions.
                asyncResult.succeeded = RunInCurrentArena(
                    *asyncTasks, *asyncWaves, stopToken, asyncResult.errors);
            }
        } catch (const std::exception& error) {
            asyncResult.errors.push_back(TaskError{{}, error.what()});
        } catch (...) {
            asyncResult.errors.push_back(TaskError{{}, "Unknown exception"});
        }
        promise->set_value(std::move(asyncResult));
        if (lane->outstanding.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            lane->outstanding.notify_all();
        }
    });
    return result;
}

}  // namespace Vixen::KernelDispatch
