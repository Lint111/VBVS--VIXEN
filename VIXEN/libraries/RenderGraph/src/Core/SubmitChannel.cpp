#include "Core/SubmitChannel.h"
#include "Core/PhaseTiming.h"
#include "Core/GpuFault.h"

#include <string>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace Vixen::RenderGraph {

std::vector<SubmitRecord::Batch> SubmitRecord::SubmissionBatches() const {
    if (commandBuffers.empty()) return {{0, 0}};
    if (!tiled) return {{0, commandBuffers.size()}};
    std::vector<Batch> batches;
    const uint64_t budget = std::max(uint64_t{1}, submissionBudgetNs);
    uint64_t used = 0;
    for (std::size_t i = 0; i < commandBuffers.size(); ++i) {
        const uint64_t cost = i < commandCostUpperBoundsNs.size() && commandCostUpperBoundsNs[i]
            ? commandCostUpperBoundsNs[i] : budget;
        if (batches.empty() || cost > budget || used > budget - std::min(cost, budget)) {
            batches.push_back({i, 0});
            used = 0;
        }
        ++batches.back().count;
        used = std::min(budget, used + std::min(cost, budget));
    }
    return batches;
}

VkResult QueueTimeline::Poll() {
    if (completed >= lastSignaled || semaphore == VK_NULL_HANDLE) return VK_SUCCESS;
    uint64_t value = completed;
    const VkResult result = counterFn ? counterFn(device, semaphore, &value)
                                      : vkGetSemaphoreCounterValue(device, semaphore, &value);
    if (result != VK_SUCCESS) return result;
    completed = std::max(completed, value);
    while (!outstanding.empty() && outstanding.front() <= completed) outstanding.pop_front();
    return VK_SUCCESS;
}

VkResult QueueTimeline::ReserveBatch(uint32_t cap) {
    if (outstanding.size() < cap) return VK_SUCCESS;
    if (const VkResult polled = Poll(); polled != VK_SUCCESS) return polled;
    if (outstanding.size() < cap) return VK_SUCCESS;
    // The cap is reached and the oldest batch is still running: wait for that batch only.
    const bool trace = std::getenv("VIXEN_GPUYIELD_TRACE") != nullptr;
    const auto started = std::chrono::steady_clock::now();
    PhaseTiming::ScopedWait retireTimer("submit-retire-oldest-timeline");
    const uint64_t oldest = outstanding[outstanding.size() - cap];
    ++blockingWaits;
    const VkResult waited = waitFn ? waitFn(device, semaphore, oldest, GpuWaitBoundNs())
                                   : BoundedWaitSemaphore(device, semaphore, oldest).result;
    if (trace) {
        const auto ns = [](std::chrono::steady_clock::time_point t) {
            return static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                t.time_since_epoch()).count());
        };
        std::fprintf(stderr, "[GPUYIELD_HOST] operation=chunk-retire value=%llu start_ns=%lld end_ns=%lld result=%d\n",
            static_cast<unsigned long long>(oldest), ns(started), ns(std::chrono::steady_clock::now()),
            static_cast<int>(waited));
    }
    if (waited != VK_SUCCESS) return waited;
    completed = std::max(completed, oldest);
    while (!outstanding.empty() && outstanding.front() <= completed) outstanding.pop_front();
    return VK_SUCCESS;
}

void QueueTimeline::Defer(uint64_t value, std::function<void()> release) {
    if (!release) return;
    if (value <= completed) {
        release();
        return;
    }
    releases.emplace_back(value, std::move(release));
}

std::vector<std::function<void()>> QueueTimeline::TakeCompleted() {
    std::vector<std::function<void()>> ready;
    while (!releases.empty() && releases.front().first <= completed) {
        ready.push_back(std::move(releases.front().second));
        releases.pop_front();
    }
    return ready;
}

std::vector<std::function<void()>> QueueTimeline::TakeAll() {
    completed = lastSignaled;
    outstanding.clear();
    std::vector<std::function<void()>> all;
    all.reserve(releases.size());
    for (auto& entry : releases) all.push_back(std::move(entry.second));
    releases.clear();
    return all;
}

VkResult SubmitRecord::Submit(const IssueFn& issue, QueueTimeline& timeline) const {
    const bool trace = std::getenv("VIXEN_GPUYIELD_TRACE") != nullptr;
    using Clock = std::chrono::steady_clock;
    const auto ticks = [](Clock::time_point time) {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            time.time_since_epoch()).count());
    };
    const auto batches = SubmissionBatches();
    const std::size_t count = batches.size();
    VkSemaphoreSubmitInfo timelineSignal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    timelineSignal.semaphore = timeline.semaphore;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    std::vector<VkSemaphoreSubmitInfo> finalSignals(signals);
    finalSignals.push_back(timelineSignal);
    for (std::size_t i = 0; i < count; ++i) {
        if (tiled) {
            if (const VkResult reserved = timeline.ReserveBatch(kOutstandingBatchCap); reserved != VK_SUCCESS)
                return reserved;
        }

        const bool last = i + 1 == count;
        const uint64_t value = timeline.lastSignaled + 1;
        VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        if (i == 0) {
            si.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
            si.pWaitSemaphoreInfos = waits.empty() ? nullptr : waits.data();
        }
        if (!commandBuffers.empty()) {
            si.commandBufferInfoCount = static_cast<uint32_t>(batches[i].count);
            si.pCommandBufferInfos = &commandBuffers[batches[i].first];
        }
        timelineSignal.value = value;
        finalSignals.back().value = value;
        // A null timeline (no semaphore: CPU contract tests) signals only the record's own semaphores.
        const bool withTimeline = timeline.semaphore != VK_NULL_HANDLE;
        if (last) {
            si.signalSemaphoreInfoCount = static_cast<uint32_t>(finalSignals.size() - (withTimeline ? 0 : 1));
            si.pSignalSemaphoreInfos = si.signalSemaphoreInfoCount ? finalSignals.data() : nullptr;
        } else if (withTimeline) {
            si.signalSemaphoreInfoCount = 1;
            si.pSignalSemaphoreInfos = &timelineSignal;
        }
        const auto started = trace ? Clock::now() : Clock::time_point{};
        const VkResult result = issue(si, last ? fence : VK_NULL_HANDLE);
        if (trace) {
            std::fprintf(stderr, "[GPUYIELD_HOST] operation=submit chunk=%zu chunks=%zu start_ns=%lld end_ns=%lld result=%d\n",
                i, count, ticks(started), ticks(Clock::now()), static_cast<int>(result));
        }
        if (result != VK_SUCCESS) return result;
        timeline.Issued(timeline.NextValue(), tiled);
    }
    return VK_SUCCESS;
}

void SubmitChannel::RecordFault(const char* where, VkResult result) {
    // First fault wins the message (the drain aborts the frame on it); later faults in the same
    // frame are still flagged but do not overwrite the primary cause.
    if (!anyFault_) {
        faultMessage_ = std::string(where ? where : "SubmitChannel") +
                        " failed (VkResult " + std::to_string(static_cast<int>(result)) + ")";
    }
    anyFault_ = true;
}

}  // namespace Vixen::RenderGraph
