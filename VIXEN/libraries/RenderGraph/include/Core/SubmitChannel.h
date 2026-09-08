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
// SCOPE (P1, controller ruling Option A): the ASYNC frame-path submits only (fence/semaphore signal,
// no inline WaitIdle). The synchronous submit+WaitIdle sites (CommandBufferUtility, TextureLoader,
// CashSystem AS build, RenderTargetReadback, the one-shot transition/clear nodes) and BatchedUploader
// keep VK2 as a documented residual; converting them to two-phase observe is a separate P1b lane.
//
// The one behaviour change beyond lock removal: a deferred submit's result is observed by the OWNER,
// not by the producing node inline. A failed submit records a fault the drain surfaces (device-loss
// notification fires from the owner), replacing each node's inline throw. This is design §2.5's
// "idle handling becomes a quiescent row" seen from the submit side.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace Vixen::Vulkan::Resources { class VulkanDevice; }

namespace Vixen::RenderGraph {

/// One deferred queue submission (a graph node's VkSubmitInfo2, deep-copied so it outlives the
/// node's Execute()). The semaphore/command-buffer submit-infos are trivially-copyable PODs whose
/// only referents are Vulkan handles + values, so a value copy of the vectors is a complete,
/// self-owned payload — the drain rebuilds a VkSubmitInfo2 pointing at THIS record's storage.
struct SubmitRecord {
    std::vector<VkSemaphoreSubmitInfo> waits;
    std::vector<VkCommandBufferSubmitInfo> commandBuffers;
    std::vector<VkSemaphoreSubmitInfo> signals;
    VkFence fence = VK_NULL_HANDLE;   ///< The frame in-flight fence for the frame-final submit; else VK_NULL_HANDLE.
    bool present = false;             ///< True for the present record (issued via presentFn, not vkQueueSubmit2).

    /// The producing node's device (set by RenderGraph::PublishSubmit). The owner drains through this
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
    static SubmitRecord FromSubmitInfo2(const VkSubmitInfo2& si, VkFence fence) {
        SubmitRecord r;
        r.fence = fence;
        r.waits.assign(si.pWaitSemaphoreInfos, si.pWaitSemaphoreInfos + si.waitSemaphoreInfoCount);
        r.commandBuffers.assign(si.pCommandBufferInfos,
                                si.pCommandBufferInfos + si.commandBufferInfoCount);
        r.signals.assign(si.pSignalSemaphoreInfos,
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
    /// executionOrder index (RenderGraph passes it). No lock: distinct nodes own distinct slots, and a
    /// node publishes at most once per frame from its single Execute() call.
    void Publish(std::size_t ordinal, SubmitRecord&& record) {
        if (ordinal >= slots_.size()) return;  // defensive: an unconfigured/late node is a no-op, never UB.
        slots_[ordinal].record = std::move(record);
        slots_[ordinal].filled = true;
    }

    /// Clear all slots for the next frame. Graph thread, at frame start.
    void BeginFrame() {
        for (Slot& s : slots_) s.filled = false;
        anyFault_ = false;
        faultMessage_.clear();
    }

    /// Number of node slots.
    [[nodiscard]] std::size_t Size() const { return slots_.size(); }

    /// Whether slot `ordinal` was published this frame, and access to its record (owner-only, post-barrier).
    [[nodiscard]] bool Filled(std::size_t ordinal) const {
        return ordinal < slots_.size() && slots_[ordinal].filled;
    }
    [[nodiscard]] SubmitRecord& Record(std::size_t ordinal) { return slots_[ordinal].record; }

    /// The owner records a submit/present fault here; the frame driver surfaces it (device-loss path).
    void RecordFault(const char* where, VkResult result);
    [[nodiscard]] bool HasFault() const { return anyFault_; }
    [[nodiscard]] const std::string& FaultMessage() const { return faultMessage_; }

private:
    struct Slot {
        SubmitRecord record;
        bool filled = false;
    };
    std::vector<Slot> slots_;
    bool anyFault_ = false;
    std::string faultMessage_;
};

}  // namespace Vixen::RenderGraph
