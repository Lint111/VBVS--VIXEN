/**
 * @file TaskQueue.cpp
 * @brief Explicit template instantiations for TaskQueue
 *
 * Sprint 6.2: TaskQueue System - Task #339
 * Sprint 6.3: Capacity Tracker Integration - Task #344 (Phase 2.1)
 * Design Element: #37 TaskQueue System
 *
 * This file provides explicit template instantiations to:
 * 1. Reduce compile times (template code compiled once)
 * 2. Verify template compiles with primary use case (DispatchPass)
 * 3. Ensure linker can find symbols when used across translation units
 *
 * Additional instantiations can be added as new TTaskData types are needed.
 */

#include "Core/TaskQueue.h"
#include "Core/TimelineCapacityTracker.h"
#include "Data/DispatchPass.h"

namespace Vixen::RenderGraph {

// ============================================================================
// Sprint 6.3: Capacity Tracker Integration (Phase 2.1)
// ============================================================================

template<typename TTaskData>
void TaskQueue<TTaskData>::RecordActualCost(uint32_t slotIndex, uint64_t actualNs) {
    // Validate slot index
    if (slotIndex >= slots_.size()) {
        return;  // Invalid index, ignore
    }

    // If no capacity tracker linked, nothing to record
    if (!capacityTracker_) {
        return;
    }

    // Record GPU time to capacity tracker
    // (Uses default queue 0 - multi-queue support in Phase 2.2)
    capacityTracker_->RecordGPUTime(actualNs);

    // Sprint 6.3 Phase 3.1: Track prediction error for adaptive scheduling
    // This enables the feedback loop where estimates are corrected based on actual measurements
    const uint64_t estimatedNs = slots_[slotIndex].estimatedCostNs;
    if (estimatedNs > 0) {
        // Use slot index as taskId for now - nodes can provide semantic IDs via profiles
        std::string taskId = "slot_" + std::to_string(slotIndex);
        capacityTracker_->RecordPrediction(taskId, estimatedNs, actualNs);
    }
}

template<typename TTaskData>
bool TaskQueue<TTaskData>::CanEnqueueWithMeasuredBudget(const TaskSlot& slot) const {
    // If no capacity tracker, fall back to estimate-based check
    if (!capacityTracker_) {
        // Simulate TryEnqueue logic without actually enqueueing
        const uint64_t budgetNs = budget_.gpuTimeBudgetNs;
        const uint64_t taskCost = slot.estimatedCostNs;

        if (budgetNs == 0) {
            return !budget_.IsStrict();  // Lenient accepts, strict rejects
        }

        // Overflow check
        if (taskCost > std::numeric_limits<uint64_t>::max() - totalEstimatedCostNs_) {
            return !budget_.IsStrict();
        }

        const uint64_t newTotal = totalEstimatedCostNs_ + taskCost;
        return newTotal <= budgetNs || !budget_.IsStrict();
    }

    // Use measured remaining capacity from tracker
    const uint64_t remainingNs = capacityTracker_->GetGPURemainingBudget();
    const uint64_t taskCost = slot.estimatedCostNs;

    // If tracker reports no capacity and we're in strict mode, reject
    if (remainingNs == 0 && budget_.IsStrict()) {
        return false;
    }

    // Check if task fits in measured capacity
    return taskCost <= remainingNs || !budget_.IsStrict();
}

// ============================================================================
// Explicit Template Instantiations
// ============================================================================

// Primary use case: DispatchPass for MultiDispatchNode
template class TaskQueue<DispatchPass>;

// Future instantiations (Task #341, #342 may add more):
// template class TaskQueue<ComputeTask>;
// template class TaskQueue<TransferTask>;

}  // namespace Vixen::RenderGraph
