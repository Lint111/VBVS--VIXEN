#include "Core/SubmitChannel.h"
#include "Core/PhaseTiming.h"
#include "Core/GpuFault.h"

#include <string>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <limits>

namespace Vixen::RenderGraph {

namespace {
uint64_t SaturatingAdd(uint64_t left, uint64_t right) {
    return right > std::numeric_limits<uint64_t>::max() - left
        ? std::numeric_limits<uint64_t>::max()
        : left + right;
}

uint64_t ElapsedNs(QueueTimeline::Clock::time_point start,
                   QueueTimeline::Clock::time_point end) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0;
}

/// What a window predicted when it opened. Captured once so the ledger and the margin learn from
/// the same numbers.
struct OpeningPrediction {
    uint64_t rawNs = 0;
    uint64_t windowNs = 0;
    uint64_t commandKey = 0;
    bool cold = false;
};

OpeningPrediction OpenPrediction(const QueueTimeline& timeline, uint64_t value,
                                 QueueTimeline::Clock::time_point now) {
    OpeningPrediction opening;
    opening.rawNs = timeline.PredictRetireRawNs(value, now);
    opening.windowNs = timeline.PredictRetireNs(value, now, now);
    const auto batch = std::find_if(timeline.issuedBatches.begin(), timeline.issuedBatches.end(),
        [value](const QueueTimeline::IssuedBatch& b) { return b.value == value; });
    if (batch != timeline.issuedBatches.end()) {
        opening.commandKey = batch->commandKey;
        opening.cold = batch->cold;
    }
    return opening;
}

void RecordRetireWait(QueueTimeline& timeline, const OpeningPrediction& opening,
                      QueueTimeline::Clock::time_point started) {
    const uint64_t actualNs = ElapsedNs(started, QueueTimeline::Clock::now());
    if (timeline.frameLedger)
        timeline.frameLedger->RecordWait(IdleWindow::Kind::GpuRetire, opening.windowNs, actualNs,
                                         opening.commandKey, opening.rawNs, opening.cold);
    timeline.ObservePredictionError(opening.commandKey, opening.cold, opening.rawNs, actualNs);
}
} // namespace

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

VkResult QueueTimeline::Poll() {
    if (outstanding.empty() || semaphore == VK_NULL_HANDLE) return VK_SUCCESS;
    uint64_t value = completed;
    const VkResult result = counterFn ? counterFn(device, semaphore, &value)
                                      : vkGetSemaphoreCounterValue(device, semaphore, &value);
    if (result != VK_SUCCESS) return result;
    completed = std::max(completed, value);
    const auto reachedAt = Clock::now();
    bool reachedAny = false;
    while (!outstanding.empty() && outstanding.front() <= completed) outstanding.pop_front();
    while (!issuedBatches.empty() && issuedBatches.front().value <= completed) {
        issuedBatches.pop_front();
        reachedAny = true;
    }
    if (reachedAny) lastReachedAt_ = reachedAt;
    return VK_SUCCESS;
}

VkResult QueueTimeline::ReserveBatch(uint32_t cap) {
    if (outstanding.size() < cap) return VK_SUCCESS;
    if (const VkResult polled = Poll(); polled != VK_SUCCESS) return polled;
    if (outstanding.size() < cap) return VK_SUCCESS;
    // The cap is reached and the oldest batch is still running: expose the predicted window,
    // then wait only for that batch. With no filler this remains today's single bounded wait.
#if UNDERSET_NATIVE_BENCHMARK
    const bool trace = std::getenv("VIXEN_GPUYIELD_TRACE") != nullptr;
#endif
    const uint64_t oldest = outstanding[outstanding.size() - cap];
    const auto windowStarted = Clock::now();
    const OpeningPrediction initialPrediction = OpenPrediction(*this, oldest, windowStarted);
    const bool hasFiller = idleWindowFiller && static_cast<bool>(*idleWindowFiller);
    if (!hasFiller) {
        const auto started = Clock::now();
        PhaseTiming::ScopedWait retireTimer("submit-retire-oldest-timeline");
        ++blockingWaits;
        const VkResult waited = waitFn ? waitFn(device, semaphore, oldest, GpuWaitBoundNs())
                                       : BoundedWaitSemaphore(device, semaphore, oldest).result;
#if UNDERSET_NATIVE_BENCHMARK
        if (trace) {
            const auto ns = [](Clock::time_point t) {
                return static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t.time_since_epoch()).count());
            };
            std::fprintf(stderr, "[GPUYIELD_HOST] operation=chunk-retire value=%llu start_ns=%lld end_ns=%lld result=%d\n",
                static_cast<unsigned long long>(oldest), ns(started), ns(Clock::now()),
                static_cast<int>(waited));
        }
#endif
        if (waited != VK_SUCCESS) return waited;
        completed = std::max(completed, oldest);
        while (!outstanding.empty() && outstanding.front() <= completed) outstanding.pop_front();
        while (!issuedBatches.empty() && issuedBatches.front().value <= completed) issuedBatches.pop_front();
        lastReachedAt_ = Clock::now();
        RecordRetireWait(*this, initialPrediction, windowStarted);
        return VK_SUCCESS;
    }

    for (;;) {
        const auto now = Clock::now();
        const uint64_t predicted = PredictRetireNs(oldest, now, windowStarted);
        std::stop_source closed;
        const IdleWindow window{IdleWindow::Kind::GpuRetire, frameIndex, predicted, oldest,
                                closed.get_token()};
        if (predicted > 0) (*idleWindowFiller)(window);

        if (const VkResult polled = Poll(); polled != VK_SUCCESS) return polled;
        if (completed >= oldest) {
            closed.request_stop();
            RecordRetireWait(*this, initialPrediction, windowStarted);
            return VK_SUCCESS;
        }

        const uint64_t boundNs = predicted > 0
            ? std::min(predicted, GpuWaitBoundNs())
            : GpuWaitBoundNs();
        const auto started = Clock::now();
        PhaseTiming::ScopedWait retireTimer("submit-retire-oldest-timeline");
        ++blockingWaits;
        const VkResult waited = waitFn ? waitFn(device, semaphore, oldest, boundNs)
                                       : BoundedWaitSemaphore(device, semaphore, oldest, boundNs).result;
#if UNDERSET_NATIVE_BENCHMARK
        if (trace) {
            const auto ns = [](Clock::time_point t) {
                return static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t.time_since_epoch()).count());
            };
            std::fprintf(stderr, "[GPUYIELD_HOST] operation=chunk-retire value=%llu start_ns=%lld end_ns=%lld result=%d\n",
                static_cast<unsigned long long>(oldest), ns(started), ns(Clock::now()),
                static_cast<int>(waited));
        }
#endif
        if (waited != VK_SUCCESS) return waited;
        if (const VkResult polled = Poll(); polled != VK_SUCCESS) return polled;
        if (completed >= oldest) {
            closed.request_stop();
            RecordRetireWait(*this, initialPrediction, windowStarted);
            return VK_SUCCESS;
        }
    }
}

uint64_t QueueTimeline::PredictRetireRawNs(uint64_t value, Clock::time_point now) const {
    if (issuedBatches.empty()) return 0;
    const auto target = std::find_if(issuedBatches.begin(), issuedBatches.end(),
        [value](const IssuedBatch& batch) { return batch.value == value; });
    if (target == issuedBatches.end()) return 0;

    Clock::time_point retireAt = lastReachedAt_;
    bool havePrevious = lastReachedAt_ != Clock::time_point{};
    for (auto it = issuedBatches.begin(); it != std::next(target); ++it) {
        const auto base = havePrevious ? std::max(it->issuedAt, retireAt) : it->issuedAt;
        retireAt = base + std::chrono::nanoseconds(it->meanCostNs);
        havePrevious = true;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(retireAt - now).count();
    return remaining > 0 ? static_cast<uint64_t>(remaining) : 0;
}

uint64_t QueueTimeline::PredictRetireNs(uint64_t value, Clock::time_point now,
                                        Clock::time_point windowStarted) const {
    const auto target = std::find_if(issuedBatches.begin(), issuedBatches.end(),
        [value](const IssuedBatch& batch) { return batch.value == value; });
    if (target == issuedBatches.end() || target->cold || !target->commandKey) return 0;
    const PredictionHistory& history = predictionHistory_[HistorySlot(target->commandKey)];
    if (history.count < kPredictionWarmup) return 0;
    const uint64_t raw = PredictRetireRawNs(value, now);
    if (!raw) return 0;

    // Walk the ring oldest to newest. q = actual / raw is the factor by which this command's wait
    // came in against the raw prediction.
    const uint32_t first = history.count < kPredictionHistory ? 0 : history.next;
    double minQ = std::numeric_limits<double>::max();
    double maxQ = 0.0;
    double worstShrink = 1.0;  // smallest actual / previous actual
    uint64_t previousActual = 0;
    for (uint32_t i = 0; i < history.count; ++i) {
        const auto& sample = history.samples[(first + i) % kPredictionHistory];
        const double q = static_cast<double>(sample.actualNs) / static_cast<double>(sample.rawNs);
        minQ = std::min(minQ, q);
        maxQ = std::max(maxQ, q);
        if (previousActual)
            worstShrink = std::min(worstShrink,
                static_cast<double>(sample.actualNs) / static_cast<double>(previousActual));
        previousActual = sample.actualNs;
    }
    // The next q is expected no lower than kPredictionRangeFactor observed ranges below the ring minimum.
    const double marginBound = static_cast<double>(raw) *
        std::min(1.0, minQ - kPredictionRangeFactor * (maxQ - minQ));
    const double elapsed = static_cast<double>(ElapsedNs(windowStarted, now));
    const double shrinkBound = static_cast<double>(previousActual) * worstShrink - elapsed;
    const double window = std::min(marginBound, shrinkBound);
    return window > 0.0 ? static_cast<uint64_t>(window) : 0;
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
    issuedBatches.clear();
    lastReachedAt_ = Clock::now();
    std::vector<std::function<void()>> all;
    all.reserve(releases.size());
    for (auto& entry : releases) all.push_back(std::move(entry.second));
    releases.clear();
    return all;
}

VkResult SubmitRecord::Submit(const IssueFn& issue, QueueTimeline& timeline) const {
#if UNDERSET_NATIVE_BENCHMARK
    const bool trace = std::getenv("VIXEN_GPUYIELD_TRACE") != nullptr;
    using Clock = std::chrono::steady_clock;
    const auto ticks = [](Clock::time_point time) {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            time.time_since_epoch()).count());
    };
#endif
    const auto batches = SubmissionBatches();
    const std::size_t count = batches.size();
    const auto batchCosts = [this](const Batch& batch) {
        uint64_t upper = 0;
        uint64_t mean = 0;
        for (std::size_t command = batch.first; command < batch.first + batch.count; ++command) {
            const uint64_t upperCost = command < commandCostUpperBoundsNs.size()
                ? commandCostUpperBoundsNs[command] : 0;
            const uint64_t meanCost = command < commandCostMeanNs.size() && commandCostMeanNs[command]
                ? commandCostMeanNs[command] : upperCost;
            upper = SaturatingAdd(upper, upperCost);
            mean = SaturatingAdd(mean, meanCost);
        }
        return std::pair<uint64_t, uint64_t>{upper, mean};
    };
    VkSemaphoreSubmitInfo timelineSignal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    timelineSignal.semaphore = timeline.semaphore;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    std::vector<VkSemaphoreSubmitInfo> finalSignals(signals);
    finalSignals.push_back(timelineSignal);
    for (std::size_t i = 0; i < count; ++i) {
        if (const VkResult reserved = timeline.ReserveBatch(kOutstandingBatchCap); reserved != VK_SUCCESS)
            return reserved;

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
#if UNDERSET_NATIVE_BENCHMARK
        const auto started = trace ? Clock::now() : Clock::time_point{};
#endif
        const VkResult result = issue(si, last ? fence : VK_NULL_HANDLE);
#if UNDERSET_NATIVE_BENCHMARK
        if (trace) {
            std::fprintf(stderr, "[GPUYIELD_HOST] operation=submit chunk=%zu chunks=%zu start_ns=%lld end_ns=%lld result=%d\n",
                i, count, ticks(started), ticks(Clock::now()), static_cast<int>(result));
        }
#endif
        if (result != VK_SUCCESS) return result;
        const auto [upperCost, meanCost] = batchCosts(batches[i]);
        bool cold = false;
        for (std::size_t command = batches[i].first; command < batches[i].first + batches[i].count; ++command)
            cold = cold || command >= commandCostMeanNs.size() || !commandCostMeanNs[command];
        timeline.Issued(timeline.NextValue(), upperCost, meanCost, QueueTimeline::Clock::now(), cold);
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
