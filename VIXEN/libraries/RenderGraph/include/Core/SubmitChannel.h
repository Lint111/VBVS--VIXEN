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
        int64_t errorNs = 0;
    };

    uint64_t frameIndex = 0;
    uint64_t gpuRetireWaitCount = 0;
    uint64_t gpuRetireWaitNs = 0;
    uint64_t framePaceWaitNs = 0;
    uint64_t windowCount = 0;
    uint64_t windowPredictedNs = 0;
    uint64_t windowActualNs = 0;
    uint64_t filledNs = 0;
    uint64_t overrunNs = 0;
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

    void RecordWait(IdleWindow::Kind kind, uint64_t predictedNs, uint64_t actualNs) {
        const int64_t errorNs = actualNs >= predictedNs
            ? static_cast<int64_t>(actualNs - predictedNs)
            : -static_cast<int64_t>(predictedNs - actualNs);
        if (waitSampleCount < waits.size()) {
            waits[waitSampleCount++] = WaitSample{kind, predictedNs, actualNs, errorNs};
        }
        ++windowCount;
        windowPredictedNs += predictedNs;
        windowActualNs += actualNs;
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

    /// Refresh `completed` from the counter and drop reached values. Never blocks.
    VkResult Poll();
    /// Make room for one more batch under the R503 cap: poll, then wait for the oldest value only.
    VkResult ReserveBatch(uint32_t cap);
    /// Predict the remaining time until `value`, chaining the issue-time mean costs.
    [[nodiscard]] uint64_t PredictRetireNs(uint64_t value, Clock::time_point now) const;
    /// The value the next issued batch signals (call once per issued batch, after ReserveBatch).
    uint64_t NextValue() { return ++lastSignaled; }
    /// Record a successfully issued batch value.
    void Issued(uint64_t value, uint64_t upperBoundNs = 0, uint64_t meanCostNs = 0,
                Clock::time_point issuedAt = Clock::now()) {
        outstanding.push_back(value);
        issuedBatches.push_back(IssuedBatch{value, issuedAt, upperBoundNs,
                                            meanCostNs ? meanCostNs : upperBoundNs});
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
    // Producer running-mean estimates, parallel to commandCostUpperBoundsNs. Missing/zero
    // entries fall back to the corresponding upper bound at issue time.
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
