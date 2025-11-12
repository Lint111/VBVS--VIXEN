#include "Core/StackResourceTracker.h"
#include <algorithm>
#include <numeric>
#include <iostream>

namespace VIXEN {

void StackResourceTracker::BeginFrame(uint64_t frameNumber) {
    // Save previous frame to history
    if (currentFrame_.frameNumber > 0) {
        history_.push_back(currentFrame_);
        if (history_.size() > MAX_HISTORY_FRAMES) {
            history_.erase(history_.begin());
        }
    }

    // Reset current frame
    currentFrame_ = FrameStackUsage{};
    currentFrame_.frameNumber = frameNumber;
}

void StackResourceTracker::EndFrame() {
    CheckThresholds();

    // Update peak usage
    if (currentFrame_.totalStackUsed > currentFrame_.peakStackUsed) {
        currentFrame_.peakStackUsed = currentFrame_.totalStackUsed;
    }
}

void StackResourceTracker::TrackAllocation(
    uint64_t resourceHash,
    uint64_t scopeHash,
    const void* stackAddress,
    size_t sizeBytes,
    uint32_t nodeId,
    bool isTemporary
) {
    StackAllocation alloc{
        .resourceHash = resourceHash,
        .scopeHash = scopeHash,
        .sizeBytes = sizeBytes,
        .stackAddress = stackAddress,
        .nodeId = nodeId,
        .frameNumber = currentFrame_.frameNumber,
        .isTemporary = isTemporary
    };

    currentFrame_.allocations.push_back(alloc);
    currentFrame_.totalStackUsed += sizeBytes;
    currentFrame_.allocationCount++;

    if (currentFrame_.totalStackUsed > currentFrame_.peakStackUsed) {
        currentFrame_.peakStackUsed = currentFrame_.totalStackUsed;
    }

    // Immediate threshold check for critical situations
    if (IsOverCriticalThreshold()) {
        LogWarning("CRITICAL: Stack usage exceeds safe threshold!");
    }
}

size_t StackResourceTracker::ReleaseTemporaryResources(uint64_t scopeHash) {
    size_t releasedCount = 0;
    size_t releasedBytes = 0;

    // Remove all temporary allocations matching the scope hash
    auto it = currentFrame_.allocations.begin();
    while (it != currentFrame_.allocations.end()) {
        if (it->isTemporary && it->scopeHash == scopeHash) {
            releasedBytes += it->sizeBytes;
            releasedCount++;
            it = currentFrame_.allocations.erase(it);
        } else {
            ++it;
        }
    }

    // Update totals
    currentFrame_.totalStackUsed -= releasedBytes;
    currentFrame_.allocationCount -= releasedCount;

    return releasedCount;
}

bool StackResourceTracker::ReleaseResource(uint64_t resourceHash) {
    // Find and remove specific resource
    auto it = std::find_if(
        currentFrame_.allocations.begin(),
        currentFrame_.allocations.end(),
        [resourceHash](const StackAllocation& alloc) {
            return alloc.resourceHash == resourceHash;
        }
    );

    if (it != currentFrame_.allocations.end()) {
        size_t sizeBytes = it->sizeBytes;
        currentFrame_.allocations.erase(it);
        currentFrame_.totalStackUsed -= sizeBytes;
        currentFrame_.allocationCount--;
        return true;
    }

    return false;
}

void StackResourceTracker::CheckThresholds() {
    if (IsOverCriticalThreshold()) {
        LogWarning("Frame ended with critical stack usage");
    } else if (IsOverWarningThreshold()) {
        LogWarning("Frame ended with elevated stack usage");
    }
}

void StackResourceTracker::LogWarning(const char* message) const {
    std::cerr << "[StackResourceTracker] WARNING: " << message
              << " (Frame " << currentFrame_.frameNumber
              << ", Used: " << currentFrame_.totalStackUsed << "/"
              << MAX_STACK_PER_FRAME << " bytes)" << std::endl;
}

StackResourceTracker::UsageStats StackResourceTracker::GetStats() const {
    if (history_.empty()) {
        return UsageStats{
            .averageStackPerFrame = currentFrame_.totalStackUsed,
            .peakStackUsage = currentFrame_.peakStackUsed,
            .minStackUsage = currentFrame_.totalStackUsed,
            .framesTracked = 1,
            .warningFrames = IsOverWarningThreshold() ? 1u : 0u,
            .criticalFrames = IsOverCriticalThreshold() ? 1u : 0u
        };
    }

    size_t totalStack = 0;
    size_t peakStack = 0;
    size_t minStack = SIZE_MAX;
    uint32_t warningCount = 0;
    uint32_t criticalCount = 0;

    for (const auto& frame : history_) {
        totalStack += frame.totalStackUsed;
        peakStack = std::max(peakStack, frame.peakStackUsed);
        minStack = std::min(minStack, frame.totalStackUsed);

        if (frame.totalStackUsed > CRITICAL_THRESHOLD) {
            criticalCount++;
        } else if (frame.totalStackUsed > WARNING_THRESHOLD) {
            warningCount++;
        }
    }

    return UsageStats{
        .averageStackPerFrame = totalStack / history_.size(),
        .peakStackUsage = peakStack,
        .minStackUsage = minStack,
        .framesTracked = static_cast<uint32_t>(history_.size()),
        .warningFrames = warningCount,
        .criticalFrames = criticalCount
    };
}

void StackResourceTracker::ClearHistory() {
    history_.clear();
}

}  // namespace VIXEN
