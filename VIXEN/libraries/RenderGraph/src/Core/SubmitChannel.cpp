#include "Core/SubmitChannel.h"

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
                              PFN_vkQueueWaitIdle retireFn) const {
    const bool trace = std::getenv("VIXEN_GPUYIELD_TRACE") != nullptr;
    using Clock = std::chrono::steady_clock;
    const auto ticks = [](Clock::time_point time) {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            time.time_since_epoch()).count());
    };
    const auto batches = SubmissionBatches();
    const std::size_t count = batches.size();
    for (std::size_t i = 0; i < count; ++i) {
        const auto retireStarted = trace ? Clock::now() : Clock::time_point{};
        const VkResult retired = retireFn(queue);
        if (trace) {
            std::fprintf(stderr, "[GPUYIELD_HOST] operation=chunk-retire chunk=%zu chunks=%zu start_ns=%lld end_ns=%lld result=%d\n",
                i, count, ticks(retireStarted), ticks(Clock::now()), static_cast<int>(retired));
        }
        if (retired != VK_SUCCESS) return retired;

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
        if (last) {
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
