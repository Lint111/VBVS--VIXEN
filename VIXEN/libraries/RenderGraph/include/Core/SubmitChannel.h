#pragma once

// ============================================================================
// SubmitChannel — lock-free federation phase 1: the queue-owner stage for the
// per-frame ASYNC submit path (design 2026-09-08-lockfree-federation-architecture.md §2.5).
//
// The P0 census measured VK2 (the per-physical-VkQueue submit mutex) holding ~5.2ms of a
// ~10ms frame: ~12 graph-node submits/frame each take the mutex serially in-wave, and that
// hold — not any data dependency — is the frame's serial floor (width does not scale w1→w4).
//
// This channel replaces the held mutex with a SINGLE-OWNER DRAIN. Instead of each node calling
// vkQueueSubmit2 under VulkanDevice::SubmitMutex, a node PUBLISHES a SubmitRecord (a deep copy of
// its VkSubmitInfo2 payload) into its OWN pre-assigned slot — no shared mutable cell, so concurrent
// publishing nodes in one wave never contend (P1 owner discipline). After each wave barrier, the
// RenderGraph frame driver (ExecuteLoweredFrame's commitWave, on the graph caller thread) drains the
// slots that were published, in canonical executionOrder position order, and issues each submit.
// Serialization of physical-queue access is preserved as an INVARIANT of "one owner, one thread",
// not as a mutex. Present is drained/issued in its own final wave, naturally last (Present depends on
// the render-complete semaphore, so the schedule already places it after every render submit).
//
// SCOPE (R715 no-stall, op-vixensubmit): RenderGraph::SubmitNodeWork is the one submission point
// for every graph node on both executors. The lowered executor publishes into the node's slot; the
// sequential executor issues at once on the graph thread through the same owner function. Every
// batch the owner issues signals the queue's QueueTimeline, so R503's outstanding-batch cap waits for
// the oldest outstanding batch value (polled first) instead of draining the queue with
// vkQueueWaitIdle, and a one-shot command buffer is released when its value is reached instead of
// after an immediate submit plus wait idle. VK2 stays only inside the owner's issue (and the
// off-graph submitters: uploads, AS builds, readbacks) until those ride the frame stream too.

// The one behaviour change beyond lock removal: a deferred submit's result is observed by the OWNER,
// not by the producing node inline. A failed submit records a fault the drain surfaces (device-loss
// notification fires from the owner), replacing each node's inline throw. This is design §2.5's
// "idle handling becomes a quiescent row" seen from the submit side.
// ============================================================================

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <stop_token>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>
#include "QueueSubmitRequest.h"

namespace Vixen::Vulkan::Resources { class VulkanDevice; }

namespace Vixen::RenderGraph {

/// A bounded host interval in which the graph would otherwise wait for a GPU value or a clock.
/// The stop token is requested by the owner as soon as the awaited event is observed reached.
struct IdleWindow {
    enum class Kind : uint8_t { GpuRetire, FramePace };

    Kind kind = Kind::GpuRetire;
    uint64_t frameIndex = 0;
    uint64_t predictedNs = 0;
    uint64_t waitValue = 0;
    std::stop_token closed;
};

using IdleWindowFiller = std::function<void(const IdleWindow&)>;

/// Fixed-capacity telemetry for one completed graph frame. The rows are rotated with the
/// frames-in-flight ring, so recording a wait never grows a container or allocates.
struct FrameWaitLedger {
    static constexpr std::size_t kFlightCount = 4;
    static constexpr std::size_t kMaxWaitsPerFrame = 64;
    static constexpr std::size_t kErrorHistogramBuckets = 16;

    struct WaitSample {
        IdleWindow::Kind kind = IdleWindow::Kind::GpuRetire;
        uint64_t predictedNs = 0;
        uint64_t actualNs = 0;
        int64_t errorNs = 0;          ///< actual - predicted, signed
        double relativeError = 0.0;
        uint64_t commandKey = 0;      ///< issue ordinal of the awaited batch within its frame (0 = unkeyed)
        uint64_t rawPredictedNs = 0;  ///< prediction before the derived margin was subtracted
        bool cold = false;            ///< the awaited batch had no last-frame timing
        bool violation = false;       ///< predicted > actual: the window would outlive the GPU work
    };

    uint64_t frameIndex = 0;
    uint64_t gpuRetireWaitCount = 0;
    uint64_t gpuRetireWaitNs = 0;
    uint64_t framePaceWaitNs = 0;
    uint64_t windowCount = 0;
    uint64_t windowPredictedNs = 0;
    uint64_t windowActualNs = 0;
    uint64_t filledNs = 0;
    // A violation is predicted > actual, per window (owner 11-10). It must be zero: the window may
    // never end after the GPU work it waits on retires. Unused time is the safe remainder.
    uint64_t violationCount = 0;
    uint64_t violationNs = 0;   ///< sum of (predicted - actual) over violating windows
    uint64_t unusedNs = 0;      ///< sum of (actual - predicted) over windows with actual >= predicted
    uint64_t usedNs = 0;        ///< sum of min(predicted, actual): window time actually usable
    int64_t errorMinNs = 0;
    int64_t errorMaxNs = 0;
    int64_t errorSumNs = 0;
    uint64_t errorCount = 0;
    std::array<uint32_t, kErrorHistogramBuckets> errorHistogram{};
    std::array<WaitSample, kMaxWaitsPerFrame> waits{};
    uint32_t waitSampleCount = 0;

    void Reset(uint64_t newFrameIndex) {
        *this = FrameWaitLedger{};
        frameIndex = newFrameIndex;
    }

    void RecordWait(IdleWindow::Kind kind, uint64_t predictedNs, uint64_t actualNs,
                    uint64_t commandKey = 0, uint64_t rawPredictedNs = 0, bool cold = false) {
        const int64_t errorNs = actualNs >= predictedNs
            ? static_cast<int64_t>(actualNs - predictedNs)
            : -static_cast<int64_t>(predictedNs - actualNs);
        const uint64_t absoluteErrorNs = actualNs >= predictedNs
            ? actualNs - predictedNs : predictedNs - actualNs;
        const double relativeError = predictedNs
            ? static_cast<double>(absoluteErrorNs) / static_cast<double>(predictedNs) : 0.0;
        if (waitSampleCount < waits.size()) {
            waits[waitSampleCount++] = WaitSample{kind, predictedNs, actualNs, errorNs, relativeError,
                commandKey, rawPredictedNs ? rawPredictedNs : predictedNs, cold, predictedNs > actualNs};
        }
        ++windowCount;
        windowPredictedNs += predictedNs;
        windowActualNs += actualNs;
        if (predictedNs > actualNs) {
            violationNs += predictedNs - actualNs;
            ++violationCount;
            usedNs += actualNs;
        } else {
            unusedNs += actualNs - predictedNs;
            usedNs += predictedNs;
        }
        if (kind == IdleWindow::Kind::GpuRetire) {
            ++gpuRetireWaitCount;
            gpuRetireWaitNs += actualNs;
        } else {
            framePaceWaitNs += actualNs;
        }
        if (errorCount++ == 0) {
            errorMinNs = errorMaxNs = errorNs;
        } else {
            errorMinNs = std::min(errorMinNs, errorNs);
            errorMaxNs = std::max(errorMaxNs, errorNs);
        }
        errorSumNs += errorNs;
        // Sixteen signed one-millisecond buckets: [-8,-7), ... [-1,0),
        // [0,1), ... [7,+inf). This keeps early and late errors visible without
        // allocating a histogram whose range depends on a frame.
        const int64_t errorMs = errorNs / 1'000'000;
        const int bucket = std::clamp(static_cast<int>(errorMs) + 8, 0,
                                      static_cast<int>(kErrorHistogramBuckets - 1));
        ++errorHistogram[static_cast<std::size_t>(bucket)];
    }

    [[nodiscard]] double ErrorP95Ms() const {
        if (!errorCount) return 0.0;
        std::array<int64_t, kMaxWaitsPerFrame> sorted{};
        const std::size_t count = std::min<std::size_t>(waitSampleCount, sorted.size());
        for (std::size_t i = 0; i < count; ++i) sorted[i] = waits[i].errorNs;
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count));
        const std::size_t index = std::min(count - 1,
            static_cast<std::size_t>(0.95 * static_cast<double>(count - 1) + 0.5));
        return static_cast<double>(sorted[index]) / 1'000'000.0;
    }

    [[nodiscard]] double AbsoluteErrorPercentileMs(double percentile) const {
        if (!waitSampleCount) return 0.0;
        std::array<uint64_t, kMaxWaitsPerFrame> sorted{};
        const std::size_t count = std::min<std::size_t>(waitSampleCount, sorted.size());
        for (std::size_t i = 0; i < count; ++i) {
            const int64_t error = waits[i].errorNs;
            sorted[i] = error < 0 ? static_cast<uint64_t>(-error) : static_cast<uint64_t>(error);
        }
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count));
        const std::size_t index = std::min(count - 1,
            static_cast<std::size_t>(percentile * static_cast<double>(count - 1) + 0.5));
        return static_cast<double>(sorted[index]) / 1'000'000.0;
    }

    [[nodiscard]] double RelativeErrorPercentile(double percentile) const {
        if (!waitSampleCount) return 0.0;
        std::array<double, kMaxWaitsPerFrame> sorted{};
        const std::size_t count = std::min<std::size_t>(waitSampleCount, sorted.size());
        for (std::size_t i = 0; i < count; ++i) sorted[i] = waits[i].relativeError;
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count));
        const std::size_t index = std::min(count - 1,
            static_cast<std::size_t>(percentile * static_cast<double>(count - 1) + 0.5));
        return sorted[index];
    }

    [[nodiscard]] double ErrorP50Ms() const { return AbsoluteErrorPercentileMs(0.50); }
    [[nodiscard]] double ErrorP95AbsoluteMs() const { return AbsoluteErrorPercentileMs(0.95); }
    [[nodiscard]] double ErrorP50Relative() const { return RelativeErrorPercentile(0.50); }
    [[nodiscard]] double ErrorP95Relative() const { return RelativeErrorPercentile(0.95); }
    /// Σused / Σactual wait over this frame's windows; the figure to maximise at zero violations.
    [[nodiscard]] double Utilisation() const {
        return windowActualNs ? static_cast<double>(usedNs) / static_cast<double>(windowActualNs) : 0.0;
    }
};

/// Fixed per-flight command samples. A producer clears the row when it successfully reads the
/// previous flight's timestamps, then records one sample per command. The next recording pass uses
/// the same command key, so a missing or reshaped command falls back to the model mean. This keeps
/// the steady-state prediction path allocation-free and rotates storage with the flight ring.
struct FrameCommandTimingLedger {
    static constexpr std::size_t kFlightCount = FrameWaitLedger::kFlightCount;
    static constexpr std::size_t kMaxCommands = 256;

    struct Sample {
        uint64_t commandKey = 0;
        uint64_t elapsedNs = 0;
    };

    std::array<std::array<Sample, kMaxCommands>, kFlightCount> rows{};
    std::array<uint32_t, kFlightCount> counts{};

    void Begin(uint32_t flight) {
        if (flight >= kFlightCount) return;
        rows[flight].fill(Sample{});
        counts[flight] = 0;
    }

    void Record(uint32_t flight, std::size_t command, uint64_t commandKey, uint64_t elapsedNs) {
        if (flight >= kFlightCount || command >= kMaxCommands || !commandKey || !elapsedNs) return;
        rows[flight][command] = Sample{commandKey, elapsedNs};
        counts[flight] = std::max(counts[flight], static_cast<uint32_t>(command + 1));
    }

    [[nodiscard]] uint64_t Last(uint32_t flight, std::size_t command, uint64_t commandKey) const {
        if (flight >= kFlightCount || command >= kMaxCommands || !commandKey ||
            rows[flight][command].commandKey != commandKey)
            return 0;
        return rows[flight][command].elapsedNs;
    }
};

/// R715 pattern 5: the retirement timeline of one VkQueue, owned by the graph's single submission
/// point (one owner, one thread). Every batch the owner issues signals the next value. R503 caps the
/// outstanding batches at SubmitRecord::kOutstandingBatchCap: when the cap is reached the owner first
/// polls the counter and only waits (bounded) for the OLDEST outstanding batch, never for the whole
/// queue. A release (a one-shot command buffer, a staging buffer) runs once its value is reached.
struct QueueTimeline {
    using Clock = std::chrono::steady_clock;
    using CounterFn = VkResult (*)(VkDevice, VkSemaphore, uint64_t*);
    using WaitFn = VkResult (*)(VkDevice, VkSemaphore, uint64_t value, uint64_t boundNs);

    struct IssuedBatch {
        uint64_t value = 0;
        Clock::time_point issuedAt{};
        uint64_t upperBoundNs = 0;
        uint64_t meanCostNs = 0;
        uint64_t commandKey = 0;  ///< 1-based issue ordinal within the frame that issued it
        bool cold = false;        ///< no last-frame timing: the mean fell back to the upper bound
    };

    VkDevice device = VK_NULL_HANDLE;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    uint64_t lastSignaled = 0;           ///< value of the newest issued batch
    uint64_t completed = 0;              ///< newest value observed reached
    std::deque<uint64_t> outstanding;    ///< issued values not yet observed reached, oldest first
    std::deque<IssuedBatch> issuedBatches; ///< prediction metadata aligned with outstanding
    std::deque<std::pair<uint64_t, std::function<void()>>> releases;  ///< ordered by value
    CounterFn counterFn = nullptr;       ///< vkGetSemaphoreCounterValue when null; injectable for tests
    WaitFn waitFn = nullptr;             ///< bounded vkWaitSemaphores when null; injectable for tests
    uint64_t blockingWaits = 0;          ///< cap waits that found the oldest batch still running
    uint64_t frameIndex = 0;             ///< graph frame associated with the active window
    IdleWindowFiller* idleWindowFiller = nullptr; ///< graph-owned, never copied on the frame path
    FrameWaitLedger* frameLedger = nullptr;       ///< graph-owned rotating row, if configured
    Clock::time_point lastReachedAt_{};   ///< observed reach time used to chain later predictions
    /// Window derivation (R613: last-frame data only; owner 11-10: the margin is derived, never
    /// tuned). A window is a command's remaining time to retire, bounded by quantities learned
    /// from that command's own recent windows (issue ordinal within the frame is the command key):
    ///   1. q = actual / raw per ring sample. The raw prediction is scaled by
    ///      min(1, minQ - kPredictionRangeFactor * (maxQ - minQ)): the window ends below the lowest
    ///      q seen by the range of q seen, so spread (late outliers included) widens the margin;
    ///   2. the last observed wait scaled by the worst consecutive shrink ratio seen, so a command
    ///      whose waits are falling is not predicted from a stale larger value.
    /// The window is the smaller bound, minus time already elapsed in it. History is the last
    /// kPredictionHistory non-cold windows of that command, so a bound decays once the large
    /// error leaves the ring. Warm-up (cold batches, fewer than kPredictionWarmup samples, a reset
    /// after a pipeline or resolution change) yields no window.
    /// Both lengths are multiples of the flight count. kPredictionHistory (3 flights) and
    /// kPredictionRangeFactor were chosen offline on 24 native runs (500 and 1080x1080; look-dev
    /// midday angles 0-2; two repeats; two predictor versions) as the highest-utilisation pair with
    /// zero violations there: the exact smallest factor was 2.33, rounded up to 2.4. The 12
    /// held-out 1920x1080 runs needed at most 2.24, so they also give zero (reports/sn-gpuwinpredict5.md).
    static constexpr std::size_t kPredictionWarmup = 2 * FrameWaitLedger::kFlightCount;
    static constexpr std::size_t kPredictionHistory = 3 * FrameWaitLedger::kFlightCount;
    static constexpr double kPredictionRangeFactor = 2.4;
    static constexpr std::size_t kMaxPredictionKeys = 64;
    struct PredictionHistory {
        struct Sample { uint64_t rawNs = 0; uint64_t actualNs = 0; };
        std::array<Sample, kPredictionHistory> samples{};  ///< ring, oldest at `next` once full
        uint32_t count = 0;
        uint32_t next = 0;
    };
    std::array<PredictionHistory, kMaxPredictionKeys> predictionHistory_{};
    uint64_t issueFrameIndex_ = ~uint64_t{0};
    uint64_t issueOrdinal_ = 0;

    [[nodiscard]] static std::size_t HistorySlot(uint64_t commandKey) {
        return static_cast<std::size_t>(commandKey % kMaxPredictionKeys);
    }
    /// Forget all learned errors (pipeline or resolution change): the next frames are warm-up.
    void ResetPredictionHistory() { predictionHistory_ = {}; }

    /// Refresh `completed` from the counter and drop reached values. Never blocks.
    VkResult Poll();
    /// Make room for one more batch under the R503 cap: poll, then wait for the oldest value only.
    VkResult ReserveBatch(uint32_t cap);
    /// Remaining time until `value` from the chained issue-time mean costs, before any bound.
    [[nodiscard]] uint64_t PredictRetireRawNs(uint64_t value, Clock::time_point now) const;
    /// The window to expose for `value`: 0 while warming up. `windowStarted` is when the window
    /// opened, so the last-wait bound shrinks as the window is consumed.
    [[nodiscard]] uint64_t PredictRetireNs(uint64_t value, Clock::time_point now,
                                           Clock::time_point windowStarted) const;
    [[nodiscard]] uint64_t PredictRetireNs(uint64_t value, Clock::time_point now) const {
        return PredictRetireNs(value, now, now);
    }
    /// Learn from a completed window. Cold batches, unkeyed batches and windows with no raw
    /// prediction are warm-up and ignored.
    void ObservePredictionError(uint64_t commandKey, bool cold, uint64_t rawPredictedNs,
                                uint64_t actualNs) {
        if (cold || !commandKey || !rawPredictedNs) return;
        PredictionHistory& history = predictionHistory_[HistorySlot(commandKey)];
        history.samples[history.next] = {rawPredictedNs, actualNs};
        history.next = (history.next + 1) % kPredictionHistory;
        history.count = std::min<uint32_t>(history.count + 1, kPredictionHistory);
    }
    /// The value the next issued batch signals (call once per issued batch, after ReserveBatch).
    uint64_t NextValue() { return ++lastSignaled; }
    /// Record a successfully issued batch value.
    void Issued(uint64_t value, uint64_t upperBoundNs = 0, uint64_t meanCostNs = 0,
                Clock::time_point issuedAt = Clock::now(), bool cold = false) {
        if (frameIndex != issueFrameIndex_) {
            issueFrameIndex_ = frameIndex;
            issueOrdinal_ = 0;
        }
        outstanding.push_back(value);
        issuedBatches.push_back(IssuedBatch{value, issuedAt, upperBoundNs,
                                            meanCostNs ? meanCostNs : upperBoundNs,
                                            ++issueOrdinal_, cold || !meanCostNs});
    }
    /// Run `release` once `value` is reached (immediately when it already is).
    void Defer(uint64_t value, std::function<void()> release);
    /// Run every release whose value is reached. Returns them collected so the caller may run them
    /// outside any lock it holds.
    [[nodiscard]] std::vector<std::function<void()>> TakeCompleted();
    /// Teardown/device loss only (the GPU is idle or gone): treat every value as reached.
    [[nodiscard]] std::vector<std::function<void()>> TakeAll();
};

/// One deferred queue submission (a graph node's VkSubmitInfo2, deep-copied so it outlives the
/// node's Execute()). The semaphore/command-buffer submit-infos are trivially-copyable PODs whose
/// only referents are Vulkan handles + values, so a value copy of the vectors is a complete,
/// self-owned payload — the drain rebuilds a VkSubmitInfo2 pointing at THIS record's storage.
struct SubmitRecord {
    std::vector<VkSemaphoreSubmitInfo> waits;
    std::vector<VkCommandBufferSubmitInfo> commandBuffers;
    // Calibrated upper bounds supplied by the existing producer's timestamp history.
    // Missing/zero entries reserve a whole submission; they can never enable batching.
    std::vector<uint64_t> commandCostUpperBoundsNs;
    // Producer expected-time estimates, normally the same command's last-frame timestamp and
    // otherwise the windowed model mean. Missing/zero entries fall back to the corresponding
    // upper bound at issue time. Safety margin is owned by QueueTimeline at window placement.
    std::vector<uint64_t> commandCostMeanNs;
    uint64_t submissionBudgetNs = 2'000'000;
    struct Batch { std::size_t first, count; };
    [[nodiscard]] std::vector<Batch> SubmissionBatches() const;
    std::vector<VkSemaphoreSubmitInfo> signals;
    VkFence fence = VK_NULL_HANDLE;   ///< Binary in-flight fence for the designated fence-owning submit, else VK_NULL_HANDLE.
    bool present = false;             ///< True for the present record (issued via presentFn, not vkQueueSubmit2).
    std::shared_ptr<Vixen::Vulkan::Resources::QueueSubmitCompletion> completion; ///< Off-graph completion published by the owner.

    /// The producing node's device (set by RenderGraph::SubmitNodeWork). The owner drains through this
    /// device's queue + fpQueueSubmit2 — so a multi-device graph stays correct without the owner
    /// knowing "the" device. Never dereferenced when null (a publish with no device is dropped).
    Vixen::Vulkan::Resources::VulkanDevice* device = nullptr;

    // Present payload (used only when present == true). Copied by value; pSwapchains/pImageIndices/
    // pWaitSemaphores below are re-pointed at this record's own storage by the drain.
    PFN_vkQueuePresentKHR presentFn = nullptr;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    uint32_t imageIndex = 0;
    VkSemaphore presentWait = VK_NULL_HANDLE;   ///< render-complete semaphore to wait before present; may be VK_NULL_HANDLE.
    VkFence presentFence = VK_NULL_HANDLE;       ///< VK_EXT_swapchain_maintenance1 present fence; may be VK_NULL_HANDLE.

    /// Deep-copy a node's VkSubmitInfo2 into a self-owned record. The submit-info's array pointers
    /// reference node-local storage that dies with Execute(); this copies the elements into the
    /// record so the deferred drain can rebuild a VkSubmitInfo2 pointing at THIS record's vectors.
    /// (pNext chains are NOT copied — the frame-path submits converted in P1 use none; a site that
    /// grows a pNext must extend this.)
    // Each primary command buffer is a separate queue submission. Consume waits
    // once, and publish signals/fence only after the final buffer. Queue order and
    // existing entry/exit barriers retain the original graph dependency contract.
    // R503: at most two submitted batches are outstanding on the queue, so one
    // batch executes while the next is queued and a pass boundary no longer costs
    // a host round trip. On native devices a batch is now a whole pass, so this cap
    // intentionally bounds whole-pass submissions rather than tile-sized work. The
    // final semaphore/fence remains owned by the final batch. The queue-wide count
    // lives in the owner's QueueTimeline, shared by both executors, so no record has
    // to assume a full queue; reaching the cap waits for the oldest outstanding batch
    // value, not for the queue to drain.
    static constexpr uint32_t kOutstandingBatchCap = 2;
    /// Issue every batch through `issue` (one VkSubmitInfo2 + fence per call), each signalling the
    /// queue's timeline; the final batch also carries the record's own signals and fence. The cap
    /// wait happens between calls, so a caller that guards `issue` never holds its guard while it
    /// waits. Owner-only (the graph's submission point).
    using IssueFn = std::function<VkResult(const VkSubmitInfo2&, VkFence)>;
    VkResult Submit(const IssueFn& issue, QueueTimeline& timeline) const;

    /// Released once the record's final batch completes on the GPU (a one-shot command buffer,
    /// its staging). Run immediately when the submit fails (nothing was left pending).
    std::function<void()> onRetired;

    /// Same for a legacy VkSubmitInfo (binary semaphores only; a VkTimelineSemaphoreSubmitInfo in
    /// its pNext supplies the values).
    static SubmitRecord FromSubmitInfo(const VkSubmitInfo& si, VkFence fence) {
        SubmitRecord r;
        r.fence = fence;
        const auto* values = static_cast<const VkTimelineSemaphoreSubmitInfo*>(si.pNext);
        if (values && values->sType != VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) values = nullptr;
        for (uint32_t i = 0; i < si.waitSemaphoreCount; ++i) {
            VkSemaphoreSubmitInfo wait{};
            wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            wait.semaphore = si.pWaitSemaphores[i];
            wait.value = values && i < values->waitSemaphoreValueCount ? values->pWaitSemaphoreValues[i] : 0;
            wait.stageMask = si.pWaitDstStageMask ? si.pWaitDstStageMask[i] : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            r.waits.push_back(wait);
        }
        for (uint32_t i = 0; i < si.commandBufferCount; ++i) {
            VkCommandBufferSubmitInfo command{};
            command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            command.commandBuffer = si.pCommandBuffers[i];
            r.commandBuffers.push_back(command);
        }
        for (uint32_t i = 0; i < si.signalSemaphoreCount; ++i) {
            VkSemaphoreSubmitInfo signal{};
            signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signal.semaphore = si.pSignalSemaphores[i];
            signal.value = values && i < values->signalSemaphoreValueCount ? values->pSignalSemaphoreValues[i] : 0;
            signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            r.signals.push_back(signal);
        }
        return r;
    }

    static SubmitRecord FromSubmitInfo2(const VkSubmitInfo2& si, VkFence fence) {
        SubmitRecord r;
        r.fence = fence;
        if (si.waitSemaphoreInfoCount)
            r.waits.assign(si.pWaitSemaphoreInfos, si.pWaitSemaphoreInfos + si.waitSemaphoreInfoCount);
        if (si.commandBufferInfoCount) r.commandBuffers.assign(si.pCommandBufferInfos,
                                si.pCommandBufferInfos + si.commandBufferInfoCount);
        if (si.signalSemaphoreInfoCount) r.signals.assign(si.pSignalSemaphoreInfos,
                         si.pSignalSemaphoreInfos + si.signalSemaphoreInfoCount);
        return r;
    }

    static SubmitRecord FromQueueSubmitRequest(
        const Vixen::Vulkan::Resources::QueueSubmitRequest& request) {
        SubmitRecord record;
        record.device = request.device;
        record.waits = request.waits;
        record.commandBuffers = request.commandBuffers;
        record.signals = request.signals;
        record.commandCostUpperBoundsNs = request.commandCostUpperBoundsNs;
        record.submissionBudgetNs = request.submissionBudgetNs;
        record.fence = request.fence;
        record.completion = request.completion;
        record.onRetired = request.onRetired;
        return record;
    }
};

/// Per-frame, per-producer submit slots. One slot per graph node (indexed by the node's position in
/// executionOrder, assigned at compile). A producing node writes ONLY its own slot; the owner drains
/// slots in index order after a wave barrier (happens-before from the barrier makes the read safe with
/// no lock). Reset per frame. Whole object is touched by the graph thread except the per-slot writes,
/// which each belong to exactly one node — so there is no shared mutable cell and no mutex.
class SubmitChannel {
public:
    /// Sized at compile to the node count; every slot starts empty. Called on the graph thread.
    void Configure(std::size_t nodeCount) {
        slots_.assign(nodeCount, Slot{});
        anyFault_ = false;
        faultMessage_.clear();
    }

    [[nodiscard]] bool IsConfigured() const { return !slots_.empty(); }

    /// A producing node publishes its submit into its own slot. `ordinal` is the node's stable
    /// executionOrder index (RenderGraph passes it). No lock: distinct nodes own distinct slots, and
    /// only the node's own Execute() appends (a one-shot then its frame submit, in that order).
    void Publish(std::size_t ordinal, SubmitRecord&& record) {
        if (ordinal >= slots_.size()) return;  // defensive: an unconfigured/late node is a no-op, never UB.
        slots_[ordinal].records.push_back(std::move(record));
    }

    /// Clear all slots for the next frame. Graph thread, at frame start.
    void BeginFrame() {
        for (Slot& s : slots_) s.records.clear();
        anyFault_ = false;
        faultMessage_.clear();
    }

    /// Number of node slots.
    [[nodiscard]] std::size_t Size() const { return slots_.size(); }

    /// Whether slot `ordinal` was published this frame, and access to its record (owner-only, post-barrier).
    [[nodiscard]] bool Filled(std::size_t ordinal) const {
        return ordinal < slots_.size() && !slots_[ordinal].records.empty();
    }
    [[nodiscard]] std::vector<SubmitRecord>& Records(std::size_t ordinal) { return slots_[ordinal].records; }

    /// The owner records a submit/present fault here; the frame driver surfaces it (device-loss path).
    void RecordFault(const char* where, VkResult result);
    [[nodiscard]] bool HasFault() const { return anyFault_; }
    [[nodiscard]] const std::string& FaultMessage() const { return faultMessage_; }

private:
    struct Slot {
        std::vector<SubmitRecord> records;
    };
    std::vector<Slot> slots_;
    bool anyFault_ = false;
    std::string faultMessage_;
};

}  // namespace Vixen::RenderGraph
