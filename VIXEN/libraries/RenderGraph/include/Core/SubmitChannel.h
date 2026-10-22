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

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

namespace Vixen::Vulkan::Resources { class VulkanDevice; }

namespace Vixen::RenderGraph {

/// R715 pattern 5: the retirement timeline of one VkQueue, owned by the graph's single submission
/// point (one owner, one thread). Every batch the owner issues signals the next value. R503 caps the
/// outstanding batches at SubmitRecord::kOutstandingBatchCap: when the cap is reached the owner first
/// polls the counter and only waits (bounded) for the OLDEST outstanding batch, never for the whole
/// queue. A release (a one-shot command buffer, a staging buffer) runs once its value is reached.
struct QueueTimeline {
    using CounterFn = VkResult (*)(VkDevice, VkSemaphore, uint64_t*);
    using WaitFn = VkResult (*)(VkDevice, VkSemaphore, uint64_t value, uint64_t boundNs);

    VkDevice device = VK_NULL_HANDLE;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    uint64_t lastSignaled = 0;           ///< value of the newest issued batch
    uint64_t completed = 0;              ///< newest value observed reached
    std::deque<uint64_t> outstanding;    ///< issued values not yet observed reached, oldest first
    std::deque<std::pair<uint64_t, std::function<void()>>> releases;  ///< ordered by value
    CounterFn counterFn = nullptr;       ///< vkGetSemaphoreCounterValue when null; injectable for tests
    WaitFn waitFn = nullptr;             ///< bounded vkWaitSemaphores when null; injectable for tests
    uint64_t blockingWaits = 0;          ///< cap waits that found the oldest batch still running

    /// Refresh `completed` from the counter (whenever anything issued is unreached) and drop reached
    /// values. Never blocks.
    VkResult Poll();
    /// Make room for one more batch under the R503 cap: poll, then wait for the oldest value only.
    VkResult ReserveBatch(uint32_t cap);
    /// The value the next issued batch signals (call once per issued batch, after ReserveBatch).
    uint64_t NextValue() { return ++lastSignaled; }
    /// Record a successfully issued batch value; `reservesSlot` is true for a tiled batch.
    void Issued(uint64_t value, bool reservesSlot = true) { if (reservesSlot) outstanding.push_back(value); }
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
    uint64_t submissionBudgetNs = 2'000'000;
    /// A tiled producer (ComputeDispatch, TraceRays, MultiDispatch) splits its command buffers into
    /// budgeted batches, each reserving one of R503's outstanding slots. An untiled record is one
    /// submission that reserves no slot (as on the sequential executor before the owner existed);
    /// it still signals the queue timeline, so its releases retire on time.
    bool tiled = false;
    struct Batch { std::size_t first, count; };
    [[nodiscard]] std::vector<Batch> SubmissionBatches() const;
    std::vector<VkSemaphoreSubmitInfo> signals;
    VkFence fence = VK_NULL_HANDLE;   ///< Binary in-flight fence for the designated fence-owning submit, else VK_NULL_HANDLE.
    bool present = false;             ///< True for the present record (issued via presentFn, not vkQueueSubmit2).

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
    // a host round trip. Worst queued work is two submission budgets (4 ms), inside
    // R494's 4.17 ms desktop reserve. The final semaphore/fence remains owned by the
    // final batch. The queue-wide count lives in the owner's QueueTimeline, shared by
    // both executors, so no record has to assume a full queue; reaching the cap waits
    // for the oldest outstanding batch value, not for the queue to drain.
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
            VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
            wait.semaphore = si.pWaitSemaphores[i];
            wait.value = values && i < values->waitSemaphoreValueCount ? values->pWaitSemaphoreValues[i] : 0;
            wait.stageMask = si.pWaitDstStageMask ? si.pWaitDstStageMask[i] : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            r.waits.push_back(wait);
        }
        for (uint32_t i = 0; i < si.commandBufferCount; ++i) {
            VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
            command.commandBuffer = si.pCommandBuffers[i];
            r.commandBuffers.push_back(command);
        }
        for (uint32_t i = 0; i < si.signalSemaphoreCount; ++i) {
            VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
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
