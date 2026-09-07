// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file FramePipeline.h
 * @brief Epoch-aware admission and deterministic commit for recurring frame work.
 *
 * A FramePipeline is the small bridge between a domain owner (for example RenderGraph's loop
 * manager) and the shared KernelDispatch executor. It does not own live node state. Callers submit
 * tasks that read an immutable frame snapshot and provide a commit callback that is safe to invoke
 * at the caller's frame boundary.
 */

#include "Abi.h"
#include "TaskExecutor.h"

#include <cstdint>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <limits>
#include <utility>
#include <vector>

namespace Vixen::KernelDispatch {

/**
 * @brief One recurring domain submission.
 *
 * `epoch == kUseCurrentEpoch` binds the submission to the pipeline's current graph/device epoch.
 * A submission with a non-current epoch is rejected before it can reach the executor.
 */
struct FramePipelineWork {
    static constexpr uint64_t kUseCurrentEpoch = std::numeric_limits<uint64_t>::max();

    uint64_t frameIndex = 0;
    uint64_t epoch = kUseCurrentEpoch;
    LoopDomainMetadata domain{};
    std::vector<VirtualTask> tasks;
    std::vector<std::vector<TaskId>> waves;
    int workerCount = 0;
    std::function<void(const AsyncRunResult&)> commit;
};

/**
 * @brief Bounded F/F+1 pipeline with ordered, epoch-checked publication.
 *
 * Work is admitted through the supplied TaskExecutor. Completion is observed in submission order,
 * not worker completion order. `BeginEpoch` invalidates unpublished work from older graph/device
 * generations; running callables receive the stop token and must obey the executor's cooperative
 * cancellation contract. A stale result is discarded and never reaches its commit callback.
 */
class FramePipeline {
public:
    explicit FramePipeline(TaskExecutor& executor, size_t maxInFlight = 2)
        : executor_(executor), maxInFlight_(maxInFlight == 0 ? 1 : maxInFlight) {}

    ~FramePipeline() { Drain(); }

    FramePipeline(const FramePipeline&) = delete;
    FramePipeline& operator=(const FramePipeline&) = delete;

    void BeginEpoch(uint64_t epoch) {
        epochStopSource_.request_stop();
        currentEpoch_ = epoch;
        epochStopSource_ = std::stop_source{};
    }

    void InvalidateEpoch() {
        epochStopSource_.request_stop();
        ++currentEpoch_;
    }

    [[nodiscard]] uint64_t CurrentEpoch() const noexcept { return currentEpoch_; }
    [[nodiscard]] size_t PendingCount() const noexcept { return pending_.size(); }

    /**
     * @brief Submit work for a due domain.
     *
     * Returns false for non-due cadence, stale epoch, disabled pipelining, or a full pipeline. The
     * caller can call CommitReady() and retry when the in-flight bound is reached.
     */
    bool Submit(FramePipelineWork work) {
        if (!work.domain.IsDue(work.frameIndex) || !work.domain.allowFramePipelining) return false;
        if (pending_.size() >= maxInFlight_) return false;

        const uint64_t epoch = work.epoch == FramePipelineWork::kUseCurrentEpoch
            ? currentEpoch_ : work.epoch;
        if (epoch != currentEpoch_) return false;

        PendingWork pending;
        pending.frameIndex = work.frameIndex;
        pending.epoch = epoch;
        pending.result = executor_.RunAsync(
            std::move(work.tasks), std::move(work.waves), work.workerCount,
            epochStopSource_.get_token());
        pending.commit = std::move(work.commit);
        pending_.push_back(std::move(pending));
        return true;
    }

    /**
     * @brief Commit completed work in submission order.
     *
     * With `waitForOldest == false`, later completed frames wait behind the oldest pending frame;
     * this keeps publication deterministic. Returns the number of non-stale commits performed.
     */
    size_t CommitReady(bool waitForOldest = false) {
        size_t committed = 0;
        while (!pending_.empty()) {
            PendingWork& pending = pending_.front();
            if (waitForOldest) {
                pending.result.wait();
            } else if (pending.result.wait_for(std::chrono::milliseconds(0)) !=
                       std::future_status::ready) {
                break;
            }

            AsyncRunResult result = pending.result.get();
            const bool current = pending.epoch == currentEpoch_;
            if (current && result.succeeded && pending.commit) {
                pending.commit(result);
                ++committed;
            }
            pending_.pop_front();
        }
        return committed;
    }

    void Drain() {
        CommitReady(true);
    }

private:
    struct PendingWork {
        uint64_t frameIndex = 0;
        uint64_t epoch = 0;
        std::future<AsyncRunResult> result;
        std::function<void(const AsyncRunResult&)> commit;
    };

    TaskExecutor& executor_;
    size_t maxInFlight_ = 2;
    uint64_t currentEpoch_ = 0;
    std::stop_source epochStopSource_;
    std::deque<PendingWork> pending_;
};

}  // namespace Vixen::KernelDispatch
