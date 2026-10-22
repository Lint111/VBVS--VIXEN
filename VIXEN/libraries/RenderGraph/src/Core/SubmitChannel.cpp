#include "Core/SubmitChannel.h"
#include "Core/PhaseTiming.h"

#include <string>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace Vixen::RenderGraph {

std::vector<SubmitRecord::Batch> SubmitRecord::SubmissionBatches() const {
    if (commandBuffers.empty()) return {{0, 0}};
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

VkResult SubmitRecord::Submit(PFN_vkQueueSubmit2 submitFn, VkQueue queue,
                              PFN_vkQueueWaitIdle retireFn,
                              uint32_t* outstandingBatches) const {
    return SubmitBatches(submitFn, queue, retireFn, outstandingBatches, nullptr);
}

VkResult SubmitRecord::Submit(PFN_vkQueueSubmit2 submitFn, VkQueue queue,
                              const TimelineRetirement& timeline) const {
    return SubmitBatches(submitFn, queue, nullptr, nullptr, &timeline);
}

VkResult SubmitRecord::SubmitBatches(PFN_vkQueueSubmit2 submitFn, VkQueue queue,
                                     PFN_vkQueueWaitIdle retireFn, uint32_t* outstandingBatches,
                                     const TimelineRetirement* timeline) const {
    const bool trace = std::getenv("VIXEN_GPUYIELD_TRACE") != nullptr;
    using Clock = std::chrono::steady_clock;
    const auto ticks = [](Clock::time_point time) {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            time.time_since_epoch()).count());
    };
    const auto batches = SubmissionBatches();
    const std::size_t count = batches.size();
    uint32_t assumedFull = kOutstandingBatchCap;
    uint32_t& outstanding = outstandingBatches ? *outstandingBatches : assumedFull;
    // With a timeline every batch signals its own value; slot 0 is that signal, followed by the
    // record's own signals, which only the final batch carries.
    std::vector<VkSemaphoreSubmitInfo> timelineSignals;
    if (timeline) {
        timelineSignals.reserve(signals.size() + 1);
        VkSemaphoreSubmitInfo batchSignal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        batchSignal.semaphore = timeline->semaphore;
        batchSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        timelineSignals.push_back(batchSignal);
        timelineSignals.insert(timelineSignals.end(), signals.begin(), signals.end());
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (timeline && *timeline->submitted - *timeline->retired >= kOutstandingBatchCap) {
            const uint64_t oldest = *timeline->submitted - kOutstandingBatchCap + 1;
            const auto retireStarted = trace ? Clock::now() : Clock::time_point{};
            PhaseTiming::ScopedWait retireTimer("submit-retire-oldest-batch");
            VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &timeline->semaphore;
            waitInfo.pValues = &oldest;
            const VkResult retired = timeline->waitFn(timeline->device, &waitInfo, UINT64_MAX);
            if (trace) {
                std::fprintf(stderr, "[GPUYIELD_HOST] operation=chunk-retire chunk=%zu chunks=%zu start_ns=%lld end_ns=%lld result=%d\n",
                    i, count, ticks(retireStarted), ticks(Clock::now()), static_cast<int>(retired));
            }
            if (retired != VK_SUCCESS) return retired;
            *timeline->retired = oldest;
        } else if (!timeline && outstanding >= kOutstandingBatchCap) {
            const auto retireStarted = trace ? Clock::now() : Clock::time_point{};
            PhaseTiming::ScopedWait retireTimer("submit-retire-queue-wait-idle");
            const VkResult retired = retireFn(queue);
            if (trace) {
                std::fprintf(stderr, "[GPUYIELD_HOST] operation=chunk-retire chunk=%zu chunks=%zu start_ns=%lld end_ns=%lld result=%d\n",
                    i, count, ticks(retireStarted), ticks(Clock::now()), static_cast<int>(retired));
            }
            if (retired != VK_SUCCESS) return retired;
            outstanding = 0;
        }

        VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        if (i == 0) {
            si.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
            si.pWaitSemaphoreInfos = waits.empty() ? nullptr : waits.data();
        }
        if (!commandBuffers.empty()) {
            si.commandBufferInfoCount = static_cast<uint32_t>(batches[i].count);
            si.pCommandBufferInfos = &commandBuffers[batches[i].first];
        }
        const bool last = i + 1 == count;
        if (timeline) {
            timelineSignals[0].value = *timeline->submitted + 1;
            si.signalSemaphoreInfoCount = last ? static_cast<uint32_t>(timelineSignals.size()) : 1u;
            si.pSignalSemaphoreInfos = timelineSignals.data();
        } else if (last) {
            si.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
            si.pSignalSemaphoreInfos = signals.empty() ? nullptr : signals.data();
        }
        const auto started = trace ? Clock::now() : Clock::time_point{};
        const VkResult result = submitFn(queue, 1, &si, last ? fence : VK_NULL_HANDLE);
        if (trace) {
            std::fprintf(stderr, "[GPUYIELD_HOST] operation=submit chunk=%zu chunks=%zu start_ns=%lld end_ns=%lld result=%d\n",
                i, count, ticks(started), ticks(Clock::now()), static_cast<int>(result));
        }
        if (result != VK_SUCCESS) return result;
        if (timeline) ++*timeline->submitted;
        else ++outstanding;
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
