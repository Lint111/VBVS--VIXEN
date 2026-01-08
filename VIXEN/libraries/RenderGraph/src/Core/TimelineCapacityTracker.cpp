// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#include "Core/TimelineCapacityTracker.h"
#include "Message.h"  // For budget events
#include <algorithm>
#include <cmath>

namespace Vixen::RenderGraph {

// =============================================================================
// TimelineCapacityTracker Implementation
// =============================================================================

TimelineCapacityTracker::TimelineCapacityTracker(const Config& config)
    : config_(config)
    , frameCounter_(0)
{
    // Validate configuration
    if (config_.historyDepth > config_.maxHistoryDepth) {
        config_.historyDepth = config_.maxHistoryDepth;
    }

    // Ensure at least one GPU queue and one CPU thread
    if (config_.numGPUQueues == 0) {
        config_.numGPUQueues = 1;
    }
    if (config_.numCPUThreads == 0) {
        config_.numCPUThreads = 1;
    }

    // Initialize GPU queues with budgets
    currentFrame_.gpuQueues.resize(config_.numGPUQueues);
    for (auto& gpu : currentFrame_.gpuQueues) {
        gpu.budgetNs = config_.gpuTimeBudgetNs;
    }

    // Initialize CPU threads with budgets
    currentFrame_.cpuThreads.resize(config_.numCPUThreads);
    for (auto& cpu : currentFrame_.cpuThreads) {
        cpu.budgetNs = config_.cpuTimeBudgetNs;
    }

    // Reset measurements
    currentFrame_.Reset();
}

// =============================================================================
// Frame Lifecycle
// =============================================================================

void TimelineCapacityTracker::BeginFrame() {
    // Increment frame counter
    ++frameCounter_;

    // Reset current frame measurements (preserve budgets)
    currentFrame_.Reset();
    currentFrame_.frameNumber = frameCounter_;

    // Delegate to GPUPerformanceLogger if available
    if (gpuPerfLogger_) {
        // GPUPerformanceLogger handles its own frame lifecycle
        // No explicit BeginFrame() call needed (handled by nodes)
    }
}

void TimelineCapacityTracker::EndFrame() {
    // Compute final utilizations for current frame
    currentFrame_.ComputeUtilizations();

    // Store in history
    history_.push_back(currentFrame_);

    // Trim history if exceeds max depth
    if (history_.size() > config_.historyDepth) {
        history_.pop_front();
    }

    // Sprint 6.3: Publish budget events for decoupled pressure adjustment
    PublishBudgetEvents();
}

// =============================================================================
// Measurement Recording
// =============================================================================

void TimelineCapacityTracker::RecordGPUTime(uint32_t queueIndex, uint64_t nanoseconds) {
    // Validate queue index
    if (queueIndex >= currentFrame_.gpuQueues.size()) {
        return;  // Invalid index, ignore
    }

    // Accumulate GPU time for this queue
    auto& gpu = currentFrame_.gpuQueues[queueIndex];
    gpu.measuredNs += nanoseconds;
    ++gpu.taskCount;

    // Update computed values in real-time
    gpu.ComputeUtilization();

    // Delegate to GPUPerformanceLogger for detailed tracking
    if (gpuPerfLogger_) {
        // GPUPerformanceLogger records timing via its own mechanisms
        // (timestamp queries in ExecuteWithMetadata)
    }
}

void TimelineCapacityTracker::RecordCPUTime(uint32_t threadIndex, uint64_t nanoseconds) {
    // Validate thread index
    if (threadIndex >= currentFrame_.cpuThreads.size()) {
        return;  // Invalid index, ignore
    }

    // Accumulate CPU time for this thread
    auto& cpu = currentFrame_.cpuThreads[threadIndex];
    cpu.measuredNs += nanoseconds;
    ++cpu.taskCount;

    // Update computed values in real-time
    cpu.ComputeUtilization();
}

// =============================================================================
// Adaptive Scheduling (Phase 1.4: Damped Hysteresis)
// =============================================================================

uint32_t TimelineCapacityTracker::SuggestAdditionalTasks(
    uint64_t estimatedCostPerTaskNs
) const {
    // Check if adaptive scheduling enabled
    if (!config_.enableAdaptiveScheduling) {
        return 0;
    }

    // Check if we can schedule more work
    if (!CanScheduleMoreWork()) {
        return 0;
    }

    // Compute remaining budget
    uint64_t remaining = GetGPURemainingBudget();
    if (remaining == 0 || estimatedCostPerTaskNs == 0) {
        return 0;
    }

    // Compute how many tasks fit in remaining budget
    uint32_t additionalTasks = static_cast<uint32_t>(
        remaining / estimatedCostPerTaskNs
    );

    return additionalTasks;
}

float TimelineCapacityTracker::ComputeTaskCountScale() const {
    // Check if adaptive scheduling enabled
    if (!config_.enableAdaptiveScheduling) {
        return 1.0f;  // No scaling
    }

    // Get maximum GPU utilization across all queues (most constrained)
    float utilization = currentFrame_.GetMaxGPUUtilization();

    // Target utilization (aim for slightly below adaptiveThreshold)
    constexpr float TARGET_UTILIZATION = 0.92f;

    // Compute utilization delta from target
    float delta = utilization - TARGET_UTILIZATION;

    // Deadband: No change if within ±deadband
    if (std::abs(delta) < config_.hysteresisDeadband) {
        return 1.0f;  // No change
    }

    // Proportional control: scale inversely to delta
    // delta > 0 (over target) → scale < 1.0 (reduce)
    // delta < 0 (under target) → scale > 1.0 (increase)
    float scale = 1.0f - (delta * 0.5f);  // 0.5 = proportional gain

    // Clamp to damping limits (±10% max change per frame)
    float minScale = 1.0f - config_.hysteresisDamping;
    float maxScale = 1.0f + config_.hysteresisDamping;
    scale = std::clamp(scale, minScale, maxScale);

    return scale;
}

// =============================================================================
// Historical Statistics
// =============================================================================

float TimelineCapacityTracker::GetAverageGPUUtilization(uint32_t frameCount) const {
    return ComputeAverage(history_, frameCount, true);
}

float TimelineCapacityTracker::GetAverageCPUUtilization(uint32_t frameCount) const {
    return ComputeAverage(history_, frameCount, false);
}

float TimelineCapacityTracker::ComputeAverage(
    const std::deque<SystemTimeline>& data,
    uint32_t count,
    bool useGPU
) const {
    if (data.empty()) {
        return 0.0f;
    }

    // Limit count to available data
    uint32_t framesToAverage = std::min(count, static_cast<uint32_t>(data.size()));
    if (framesToAverage == 0) {
        return 0.0f;
    }

    // Compute average over recent frames (from back)
    // Use maximum utilization across all queues/threads per frame
    float sum = 0.0f;
    auto it = data.rbegin();
    for (uint32_t i = 0; i < framesToAverage; ++i, ++it) {
        sum += useGPU ? it->GetMaxGPUUtilization() : it->GetMaxCPUUtilization();
    }

    return sum / static_cast<float>(framesToAverage);
}

// =============================================================================
// Event-Driven Architecture (Sprint 6.3)
// =============================================================================

void TimelineCapacityTracker::SubscribeToFrameEvents(EventBus::MessageBus* messageBus) {
    if (!messageBus) {
        return;
    }

    // ScopedSubscriptions handles unsubscribe automatically
    subscriptions_.SetBus(messageBus);

    // Subscribe to FrameStartEvent (type-safe, clean syntax)
    subscriptions_.Subscribe<EventBus::FrameStartEvent>(
        [this](const EventBus::FrameStartEvent& e) {
            // Note: Frame counter comes from FrameManager, not from us
            // We just reset our measurements
            BeginFrame();
        }
    );

    // Subscribe to FrameEndEvent
    subscriptions_.Subscribe<EventBus::FrameEndEvent>(
        [this](const EventBus::FrameEndEvent& e) {
            EndFrame();  // This will compute utilization and publish budget events
        }
    );
}

// UnsubscribeFromFrameEvents() is now inline in header using subscriptions_.UnsubscribeAll()

void TimelineCapacityTracker::PublishBudgetEvents() {
    auto* messageBus = subscriptions_.GetBus();
    if (!messageBus) {
        return;  // No MessageBus, skip budget event publishing
    }

    // Get GPU utilization (use max across all queues)
    float utilization = currentFrame_.GetMaxGPUUtilization();

    // Get budget and actual for event payload
    uint64_t budgetNs = 0;
    uint64_t actualNs = 0;
    if (!currentFrame_.gpuQueues.empty()) {
        budgetNs = currentFrame_.gpuQueues[0].budgetNs;
        actualNs = currentFrame_.gpuQueues[0].measuredNs;
    }

    // Decide which event to publish based on utilization
    if (IsOverBudget()) {
        // Over budget: publish overrun event
        auto event = std::make_unique<EventBus::BudgetOverrunEvent>(
            0,  // System sender
            frameCounter_,
            utilization,
            budgetNs,
            actualNs
        );
        messageBus->Publish(std::move(event));
    } else if (CanScheduleMoreWork()) {
        // Under threshold: publish available event
        uint64_t remainingNs = GetMinGPURemainingBudget();
        auto event = std::make_unique<EventBus::BudgetAvailableEvent>(
            0,  // System sender
            frameCounter_,
            utilization,
            config_.adaptiveThreshold,
            remainingNs
        );
        messageBus->Publish(std::move(event));
    }
    // If exactly at threshold (90-100%), no event - within deadband
}

} // namespace Vixen::RenderGraph
