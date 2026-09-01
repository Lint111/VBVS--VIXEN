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
    std::mutex mutex;
    std::promise<bool> result;
    size_t remaining = 0;
    bool success = true;
};

uint64_t CompletionHandle::SubmissionOrder() const noexcept {
    return state_ ? state_->submissionOrder : 0;
}

bool CompletionHandle::IsReady() const {
    if (!state_) return false;
    std::lock_guard lock(state_->mutex);
    return state_->result.status == CompletionStatus::Succeeded
        || state_->result.status == CompletionStatus::Failed
        || state_->result.status == CompletionStatus::Cancelled;
}

CompletionResult CompletionHandle::Wait() const {
    if (!state_) {
        return {CompletionStatus::Failed, "Invalid completion handle"};
    }

    std::unique_lock lock(state_->mutex);
    state_->condition.wait(lock, [this] {
        return state_->result.status == CompletionStatus::Succeeded
            || state_->result.status == CompletionStatus::Failed
            || state_->result.status == CompletionStatus::Cancelled;
    });
    return state_->result;
}

bool CompletionHandle::WaitFor(std::chrono::milliseconds timeout) const {
    if (!state_) return false;
    std::unique_lock lock(state_->mutex);
    return state_->condition.wait_for(lock, timeout, [this] {
        return state_->result.status == CompletionStatus::Succeeded
            || state_->result.status == CompletionStatus::Failed
            || state_->result.status == CompletionStatus::Cancelled;
    });
}

CompletionResult CompletionHandle::Result() const {
    if (!state_) {
        return {CompletionStatus::Failed, "Invalid completion handle"};
    }
    std::lock_guard lock(state_->mutex);
    return state_->result;
}

TaskExecutor::TaskExecutor(DispatcherProfile profile)
    : profile_(std::move(profile)) {
}

TaskExecutor::~TaskExecutor() {
    {
        std::lock_guard lock(blockingMutex_);
        blockingStopping_ = true;
    }
    blockingCondition_.notify_all();
    for (auto& worker : blockingWorkers_) {
        if (worker.joinable()) worker.join();
    }
}

void TaskExecutor::EnsureBlockingWorkersLocked() {
    if (!blockingWorkers_.empty()) return;

    const auto workerCount = profile_.WorkerCount(TaskLane::BlockingIO, DefaultBlockingWorkerCount());
    blockingWorkers_.reserve(workerCount);
    for (uint32_t index = 0; index < workerCount; ++index) {
        blockingWorkers_.emplace_back(&TaskExecutor::BlockingWorkerLoop, this);
    }
}

void TaskExecutor::EnqueueBlocking(BlockingWork work) {
    if (g_activeBlockingExecutor == this) {
        ExecuteBlockingWork(work);
        return;
    }

    {
        std::lock_guard lock(blockingMutex_);
        if (blockingStopping_) {
            if (work.completion) {
                std::lock_guard completionLock(work.completion->mutex);
                work.completion->result = {CompletionStatus::Cancelled, "Task executor is stopping"};
                work.completion->condition.notify_all();
            }
            if (work.batch) {
                std::lock_guard batchLock(work.batch->mutex);
                work.batch->success = false;
                if (--work.batch->remaining == 0) work.batch->result.set_value(false);
            }
            return;
        }
        EnsureBlockingWorkersLocked();
        ++blockingOutstanding_;
        blockingQueue_.push_back(std::move(work));
    }
    blockingCondition_.notify_one();
}

void TaskExecutor::ExecuteBlockingWork(BlockingWork& work) {
    CompletionResult result;
    bool invokeTask = true;
    if (work.completion) {
        std::lock_guard lock(work.completion->mutex);
        if (work.completion->result.status != CompletionStatus::Pending) {
            invokeTask = false;
        } else if (work.stopToken.stop_requested()) {
            work.completion->result = {
                CompletionStatus::Cancelled, "Task cancelled before invocation"};
            invokeTask = false;
        } else {
            work.completion->result.status = CompletionStatus::Running;
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
        {
            std::lock_guard lock(work.completion->mutex);
            work.completion->result = result;
        }
        work.completion->condition.notify_all();
    }

    if (work.batch) {
        bool resolve = false;
        bool batchSuccess = false;
        {
            std::lock_guard lock(work.batch->mutex);
            work.batch->success &= result.Succeeded();
            resolve = --work.batch->remaining == 0;
            batchSuccess = work.batch->success;
        }
        if (resolve) work.batch->result.set_value(batchSuccess);
    }
}

void TaskExecutor::BlockingWorkerLoop() {
    TaskExecutor* previousExecutor = g_activeBlockingExecutor;
    g_activeBlockingExecutor = this;

    for (;;) {
        BlockingWork work;
        {
            std::unique_lock lock(blockingMutex_);
            blockingCondition_.wait(lock, [this] {
                return blockingStopping_ || !blockingQueue_.empty();
            });
            if (blockingQueue_.empty()) {
                if (blockingStopping_) break;
                continue;
            }
            work = std::move(blockingQueue_.front());
            blockingQueue_.pop_front();
        }
        ExecuteBlockingWork(work);
        {
            std::lock_guard lock(blockingMutex_);
            if (--blockingOutstanding_ == 0) blockingIdleCondition_.notify_all();
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
                std::lock_guard lock(sharedState->mutex);
                if (sharedState->result.status == CompletionStatus::Pending) {
                    sharedState->result = {
                        CompletionStatus::Cancelled, "Task cancelled before invocation"};
                    sharedState->condition.notify_all();
                }
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

    batch->remaining = tasks.size();
    for (auto& task : tasks) {
        nextSubmissionOrder_.fetch_add(1, std::memory_order_relaxed);
        EnqueueBlocking(BlockingWork{std::move(task), stopToken, nullptr, batch, nullptr});
    }
    return future;
}

void TaskExecutor::WaitForBlocking() {
    if (g_activeBlockingExecutor == this) return;
    std::unique_lock lock(blockingMutex_);
    blockingIdleCondition_.wait(lock, [this] { return blockingOutstanding_ == 0; });
}

VirtualTask* TaskExecutor::FindTask(std::vector<VirtualTask>& tasks, const TaskId& id) const {
    for (auto& t : tasks)
        if (t.id == id) return &t;
    return nullptr;
}

bool TaskExecutor::RunWave(std::vector<VirtualTask>& tasks,
                           const std::vector<TaskId>& wave,
                           std::stop_token stopToken) {
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
    errors_.insert(errors_.end(),
                   std::make_move_iterator(orderedErrors.begin()),
                   std::make_move_iterator(orderedErrors.end()));

    return orderedErrors.empty() && !stopToken.stop_requested();
}

bool TaskExecutor::Run(std::vector<VirtualTask>& tasks,
                       const std::vector<std::vector<TaskId>>& waves,
                       int workerCount,
                       std::stop_token stopToken) {
    errors_.clear();
    if (workerCount < 1) {
        workerCount = static_cast<int>(profile_.WorkerCount(TaskLane::FrameCompute, 1));
    }
    if (stopToken.stop_requested()) return false;

    // A per-run arena caps concurrency: workerCount==1 forces serial execution, N gives N workers.
    // This is the determinism knob the spec's 1/2/N gate exercises.
    tbb::task_arena arena(workerCount);
    bool success = true;
    arena.execute([&] {
        for (const auto& wave : waves) {
            if (stopToken.stop_requested() || !RunWave(tasks, wave, stopToken)) {
                success = false;
                break;
            }
        }
    });
    return success;
}

}  // namespace Vixen::KernelDispatch
