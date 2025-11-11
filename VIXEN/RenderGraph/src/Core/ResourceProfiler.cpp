#include "Core/ResourceProfiler.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace Vixen::RenderGraph {

// =============================================================================
// Constructor / Destructor
// =============================================================================

ResourceProfiler::ResourceProfiler()
    : currentFrame_(0)
    , currentStackUsage_(0)
    , currentHeapUsage_(0)
    , currentVramUsage_(0)
    , peakStackUsage_(0)
    , peakHeapUsage_(0)
    , peakVramUsage_(0)
    , maxFrameHistory_(120)
    , detailedLogging_(false)
{
}

ResourceProfiler::~ResourceProfiler() = default;

// =============================================================================
// Frame Lifecycle
// =============================================================================

void ResourceProfiler::BeginFrame(uint64_t frameNumber) {
    currentFrame_ = frameNumber;
    frameStartTime_ = std::chrono::steady_clock::now();

    // Reset per-frame tracking
    currentFrameStats_.clear();
    currentStackUsage_ = 0;
    currentHeapUsage_ = 0;
    currentVramUsage_ = 0;
    peakStackUsage_ = 0;
    peakHeapUsage_ = 0;
    peakVramUsage_ = 0;

    if (detailedLogging_) {
        std::cout << "[ResourceProfiler] Frame " << frameNumber << " started\n";
    }
}

void ResourceProfiler::EndFrame() {
    auto frameEndTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        frameEndTime - frameStartTime_
    );
    double durationMs = duration.count() / 1000.0;

    // Build frame statistics
    FrameResourceStats frameStats;
    frameStats.frameNumber = currentFrame_;
    frameStats.frameDurationMs = durationMs;
    frameStats.peakStackUsage = peakStackUsage_;
    frameStats.peakHeapUsage = peakHeapUsage_;
    frameStats.peakVramUsage = peakVramUsage_;

    // Aggregate totals and collect per-node stats
    NodeResourceStats totals;
    totals.nodeName = "TOTAL";

    for (const auto& [nodeId, nodeStats] : currentFrameStats_) {
        frameStats.nodeStats.push_back(nodeStats);

        // Accumulate totals
        totals.stackAllocations += nodeStats.stackAllocations;
        totals.heapAllocations += nodeStats.heapAllocations;
        totals.vramAllocations += nodeStats.vramAllocations;
        totals.stackBytesUsed += nodeStats.stackBytesUsed;
        totals.heapBytesUsed += nodeStats.heapBytesUsed;
        totals.vramBytesUsed += nodeStats.vramBytesUsed;
        totals.aliasedAllocations += nodeStats.aliasedAllocations;
        totals.bytesSavedViaAliasing += nodeStats.bytesSavedViaAliasing;
        totals.allocationTimeMs += nodeStats.allocationTimeMs;
        totals.releaseTimeMs += nodeStats.releaseTimeMs;
    }

    frameStats.totals = totals;

    // Sort nodes by total bytes used (descending) for better reporting
    std::sort(frameStats.nodeStats.begin(), frameStats.nodeStats.end(),
        [](const NodeResourceStats& a, const NodeResourceStats& b) {
            return a.GetTotalBytes() > b.GetTotalBytes();
        });

    // Store in history
    frameHistory_[currentFrame_] = frameStats;

    // Prune old frames
    PruneOldFrames();

    if (detailedLogging_) {
        std::cout << "[ResourceProfiler] Frame " << currentFrame_ << " ended\n";
        std::cout << "  Duration: " << std::fixed << std::setprecision(2)
                  << durationMs << " ms\n";
        std::cout << "  Total allocations: " << totals.GetTotalAllocations() << "\n";
        std::cout << "  Total bytes: " << totals.GetTotalBytes() << "\n";
        std::cout << "  Peak VRAM: " << (peakVramUsage_ / 1024.0 / 1024.0)
                  << " MB\n";
        std::cout << "  Aliasing efficiency: " << std::fixed << std::setprecision(1)
                  << totals.GetAliasingEfficiency() << "%\n";
    }
}

// =============================================================================
// Recording
// =============================================================================

void ResourceProfiler::RecordAllocation(
    uint32_t nodeId,
    const std::string& nodeName,
    ResourceLocation location,
    size_t bytes,
    bool wasAliased
) {
    auto& nodeStats = GetOrCreateNodeStats(nodeId, nodeName);

    // Update allocation counts
    switch (location) {
        case ResourceLocation::Stack:
            nodeStats.stackAllocations++;
            nodeStats.stackBytesUsed += bytes;
            currentStackUsage_ += bytes;
            break;
        case ResourceLocation::Heap:
            nodeStats.heapAllocations++;
            nodeStats.heapBytesUsed += bytes;
            currentHeapUsage_ += bytes;
            break;
        case ResourceLocation::VRAM:
            nodeStats.vramAllocations++;
            nodeStats.vramBytesUsed += bytes;
            currentVramUsage_ += bytes;
            break;
    }

    // Track aliasing
    if (wasAliased) {
        nodeStats.aliasedAllocations++;
        // Note: bytesSavedViaAliasing should be set externally based on
        // the actual memory saved (difference between allocation size and reused size)
        // For now, we track the count and let the caller provide saved bytes
    }

    // Update peak usage
    UpdatePeakUsage();

    // Detailed logging
    if (detailedLogging_) {
        LogAllocation(nodeId, nodeName, location, bytes, wasAliased);
    }
}

void ResourceProfiler::RecordRelease(
    uint32_t nodeId,
    const std::string& nodeName,
    Resource* resource,
    size_t bytes
) {
    auto& nodeStats = GetOrCreateNodeStats(nodeId, nodeName);

    // Update current usage based on resource location
    if (resource) {
        switch (resource->GetLocation()) {
            case ResourceLocation::Stack:
                if (currentStackUsage_ >= bytes) {
                    currentStackUsage_ -= bytes;
                }
                break;
            case ResourceLocation::Heap:
                if (currentHeapUsage_ >= bytes) {
                    currentHeapUsage_ -= bytes;
                }
                break;
            case ResourceLocation::VRAM:
                if (currentVramUsage_ >= bytes) {
                    currentVramUsage_ -= bytes;
                }
                break;
        }
    }

    // Detailed logging
    if (detailedLogging_) {
        LogRelease(nodeId, nodeName, bytes);
    }
}

// =============================================================================
// Statistics Queries
// =============================================================================

NodeResourceStats ResourceProfiler::GetNodeStats(uint32_t nodeId, uint64_t frameNumber) const {
    auto frameIt = frameHistory_.find(frameNumber);
    if (frameIt == frameHistory_.end()) {
        return NodeResourceStats{};
    }

    const auto& frameStats = frameIt->second;
    for (const auto& nodeStats : frameStats.nodeStats) {
        if (nodeStats.nodeId == nodeId) {
            return nodeStats;
        }
    }

    return NodeResourceStats{};
}

FrameResourceStats ResourceProfiler::GetFrameStats(uint64_t frameNumber) const {
    auto it = frameHistory_.find(frameNumber);
    if (it != frameHistory_.end()) {
        return it->second;
    }
    return FrameResourceStats{};
}

FrameResourceStats ResourceProfiler::GetCurrentFrameStats() const {
    // Build temporary stats from current frame data
    FrameResourceStats stats;
    stats.frameNumber = currentFrame_;
    stats.peakStackUsage = peakStackUsage_;
    stats.peakHeapUsage = peakHeapUsage_;
    stats.peakVramUsage = peakVramUsage_;

    NodeResourceStats totals;
    totals.nodeName = "TOTAL";

    for (const auto& [nodeId, nodeStats] : currentFrameStats_) {
        stats.nodeStats.push_back(nodeStats);

        // Accumulate totals
        totals.stackAllocations += nodeStats.stackAllocations;
        totals.heapAllocations += nodeStats.heapAllocations;
        totals.vramAllocations += nodeStats.vramAllocations;
        totals.stackBytesUsed += nodeStats.stackBytesUsed;
        totals.heapBytesUsed += nodeStats.heapBytesUsed;
        totals.vramBytesUsed += nodeStats.vramBytesUsed;
        totals.aliasedAllocations += nodeStats.aliasedAllocations;
        totals.bytesSavedViaAliasing += nodeStats.bytesSavedViaAliasing;
    }

    stats.totals = totals;

    return stats;
}

FrameResourceStats ResourceProfiler::GetAverageStats(size_t frameCount) const {
    if (frameHistory_.empty()) {
        return FrameResourceStats{};
    }

    // Get the most recent frames
    std::vector<uint64_t> frameNumbers;
    frameNumbers.reserve(frameHistory_.size());
    for (const auto& [frameNum, _] : frameHistory_) {
        frameNumbers.push_back(frameNum);
    }
    std::sort(frameNumbers.begin(), frameNumbers.end(), std::greater<uint64_t>());

    // Limit to requested frame count
    size_t numFrames = std::min(frameCount, frameNumbers.size());
    frameNumbers.resize(numFrames);

    // Aggregate statistics
    FrameResourceStats avgStats;
    avgStats.frameNumber = frameNumbers.front(); // Most recent frame number

    // Accumulate all stats
    double totalDuration = 0.0;
    size_t totalPeakStack = 0;
    size_t totalPeakHeap = 0;
    size_t totalPeakVram = 0;

    // Map to accumulate per-node stats across frames
    std::unordered_map<uint32_t, NodeResourceStats> nodeAccumulators;

    for (uint64_t frameNum : frameNumbers) {
        const auto& frameStats = frameHistory_.at(frameNum);

        totalDuration += frameStats.frameDurationMs;
        totalPeakStack += frameStats.peakStackUsage;
        totalPeakHeap += frameStats.peakHeapUsage;
        totalPeakVram += frameStats.peakVramUsage;

        // Accumulate node stats
        for (const auto& nodeStats : frameStats.nodeStats) {
            auto& accum = nodeAccumulators[nodeStats.nodeId];
            if (accum.nodeName.empty()) {
                accum.nodeId = nodeStats.nodeId;
                accum.nodeName = nodeStats.nodeName;
            }

            accum.stackAllocations += nodeStats.stackAllocations;
            accum.heapAllocations += nodeStats.heapAllocations;
            accum.vramAllocations += nodeStats.vramAllocations;
            accum.stackBytesUsed += nodeStats.stackBytesUsed;
            accum.heapBytesUsed += nodeStats.heapBytesUsed;
            accum.vramBytesUsed += nodeStats.vramBytesUsed;
            accum.aliasedAllocations += nodeStats.aliasedAllocations;
            accum.bytesSavedViaAliasing += nodeStats.bytesSavedViaAliasing;
            accum.allocationTimeMs += nodeStats.allocationTimeMs;
            accum.releaseTimeMs += nodeStats.releaseTimeMs;
        }
    }

    // Compute averages
    avgStats.frameDurationMs = totalDuration / numFrames;
    avgStats.peakStackUsage = totalPeakStack / numFrames;
    avgStats.peakHeapUsage = totalPeakHeap / numFrames;
    avgStats.peakVramUsage = totalPeakVram / numFrames;

    // Average node stats
    for (auto& [nodeId, accum] : nodeAccumulators) {
        NodeResourceStats avgNode = accum;
        avgNode.stackAllocations /= numFrames;
        avgNode.heapAllocations /= numFrames;
        avgNode.vramAllocations /= numFrames;
        avgNode.stackBytesUsed /= numFrames;
        avgNode.heapBytesUsed /= numFrames;
        avgNode.vramBytesUsed /= numFrames;
        avgNode.aliasedAllocations /= numFrames;
        avgNode.bytesSavedViaAliasing /= numFrames;
        avgNode.allocationTimeMs /= numFrames;
        avgNode.releaseTimeMs /= numFrames;

        avgStats.nodeStats.push_back(avgNode);
    }

    // Compute totals
    NodeResourceStats totals;
    totals.nodeName = "TOTAL (AVG)";
    for (const auto& nodeStats : avgStats.nodeStats) {
        totals.stackAllocations += nodeStats.stackAllocations;
        totals.heapAllocations += nodeStats.heapAllocations;
        totals.vramAllocations += nodeStats.vramAllocations;
        totals.stackBytesUsed += nodeStats.stackBytesUsed;
        totals.heapBytesUsed += nodeStats.heapBytesUsed;
        totals.vramBytesUsed += nodeStats.vramBytesUsed;
        totals.aliasedAllocations += nodeStats.aliasedAllocations;
        totals.bytesSavedViaAliasing += nodeStats.bytesSavedViaAliasing;
        totals.allocationTimeMs += nodeStats.allocationTimeMs;
        totals.releaseTimeMs += nodeStats.releaseTimeMs;
    }
    avgStats.totals = totals;

    // Sort by total bytes
    std::sort(avgStats.nodeStats.begin(), avgStats.nodeStats.end(),
        [](const NodeResourceStats& a, const NodeResourceStats& b) {
            return a.GetTotalBytes() > b.GetTotalBytes();
        });

    return avgStats;
}

// =============================================================================
// Export
// =============================================================================

std::string ResourceProfiler::ExportAsText(uint64_t frameNumber) const {
    auto frameStats = GetFrameStats(frameNumber);
    if (frameStats.frameNumber == 0 && frameNumber != 0) {
        return "Frame " + std::to_string(frameNumber) + " not found in history\n";
    }

    std::ostringstream oss;

    // Header
    oss << "================================================================================\n";
    oss << "Resource Profiler Report - Frame " << frameNumber << "\n";
    oss << "================================================================================\n";
    oss << "Frame Duration: " << std::fixed << std::setprecision(2)
        << frameStats.frameDurationMs << " ms\n";
    oss << "\n";

    // Peak usage
    oss << "Peak Memory Usage:\n";
    oss << "  Stack: " << std::setw(12) << frameStats.peakStackUsage
        << " bytes (" << std::fixed << std::setprecision(2)
        << (frameStats.peakStackUsage / 1024.0 / 1024.0) << " MB)\n";
    oss << "  Heap:  " << std::setw(12) << frameStats.peakHeapUsage
        << " bytes (" << std::fixed << std::setprecision(2)
        << (frameStats.peakHeapUsage / 1024.0 / 1024.0) << " MB)\n";
    oss << "  VRAM:  " << std::setw(12) << frameStats.peakVramUsage
        << " bytes (" << std::fixed << std::setprecision(2)
        << (frameStats.peakVramUsage / 1024.0 / 1024.0) << " MB)\n";
    oss << "\n";

    // Totals
    oss << "Frame Totals:\n";
    oss << "  Total Allocations: " << frameStats.totals.GetTotalAllocations() << "\n";
    oss << "    Stack: " << frameStats.totals.stackAllocations << "\n";
    oss << "    Heap:  " << frameStats.totals.heapAllocations << "\n";
    oss << "    VRAM:  " << frameStats.totals.vramAllocations << "\n";
    oss << "  Total Bytes: " << frameStats.totals.GetTotalBytes()
        << " (" << std::fixed << std::setprecision(2)
        << (frameStats.totals.GetTotalBytes() / 1024.0 / 1024.0) << " MB)\n";
    oss << "  Aliased Allocations: " << frameStats.totals.aliasedAllocations << "\n";
    oss << "  Bytes Saved via Aliasing: " << frameStats.totals.bytesSavedViaAliasing
        << " (" << std::fixed << std::setprecision(2)
        << (frameStats.totals.bytesSavedViaAliasing / 1024.0 / 1024.0) << " MB)\n";
    oss << "  Aliasing Efficiency: " << std::fixed << std::setprecision(1)
        << frameStats.totals.GetAliasingEfficiency() << "%\n";
    oss << "\n";

    // Per-node breakdown
    if (!frameStats.nodeStats.empty()) {
        oss << "Per-Node Statistics:\n";
        oss << "--------------------------------------------------------------------------------\n";
        oss << std::left << std::setw(30) << "Node Name"
            << std::right << std::setw(12) << "Allocs"
            << std::setw(15) << "Total Bytes"
            << std::setw(12) << "Stack MB"
            << std::setw(12) << "Heap MB"
            << std::setw(12) << "VRAM MB"
            << "\n";
        oss << "--------------------------------------------------------------------------------\n";

        for (const auto& nodeStats : frameStats.nodeStats) {
            oss << std::left << std::setw(30) << nodeStats.nodeName
                << std::right << std::setw(12) << nodeStats.GetTotalAllocations()
                << std::setw(15) << nodeStats.GetTotalBytes()
                << std::fixed << std::setprecision(2)
                << std::setw(12) << (nodeStats.stackBytesUsed / 1024.0 / 1024.0)
                << std::setw(12) << (nodeStats.heapBytesUsed / 1024.0 / 1024.0)
                << std::setw(12) << (nodeStats.vramBytesUsed / 1024.0 / 1024.0)
                << "\n";

            // Show aliasing info if present
            if (nodeStats.aliasedAllocations > 0) {
                oss << "  └─ Aliased: " << nodeStats.aliasedAllocations
                    << " allocations, saved "
                    << std::fixed << std::setprecision(2)
                    << (nodeStats.bytesSavedViaAliasing / 1024.0 / 1024.0)
                    << " MB (" << std::fixed << std::setprecision(1)
                    << nodeStats.GetAliasingEfficiency() << "% efficiency)\n";
            }
        }
    }

    oss << "================================================================================\n";

    return oss.str();
}

std::string ResourceProfiler::ExportAsJSON(uint64_t frameNumber) const {
    auto frameStats = GetFrameStats(frameNumber);
    if (frameStats.frameNumber == 0 && frameNumber != 0) {
        return "{\"error\": \"Frame not found\"}";
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    oss << "{\n";
    oss << "  \"frameNumber\": " << frameNumber << ",\n";
    oss << "  \"frameDurationMs\": " << frameStats.frameDurationMs << ",\n";

    // Peak usage
    oss << "  \"peakUsage\": {\n";
    oss << "    \"stack\": " << frameStats.peakStackUsage << ",\n";
    oss << "    \"heap\": " << frameStats.peakHeapUsage << ",\n";
    oss << "    \"vram\": " << frameStats.peakVramUsage << "\n";
    oss << "  },\n";

    // Totals
    oss << "  \"totals\": {\n";
    oss << "    \"allocations\": {\n";
    oss << "      \"stack\": " << frameStats.totals.stackAllocations << ",\n";
    oss << "      \"heap\": " << frameStats.totals.heapAllocations << ",\n";
    oss << "      \"vram\": " << frameStats.totals.vramAllocations << ",\n";
    oss << "      \"total\": " << frameStats.totals.GetTotalAllocations() << "\n";
    oss << "    },\n";
    oss << "    \"bytesUsed\": {\n";
    oss << "      \"stack\": " << frameStats.totals.stackBytesUsed << ",\n";
    oss << "      \"heap\": " << frameStats.totals.heapBytesUsed << ",\n";
    oss << "      \"vram\": " << frameStats.totals.vramBytesUsed << ",\n";
    oss << "      \"total\": " << frameStats.totals.GetTotalBytes() << "\n";
    oss << "    },\n";
    oss << "    \"aliasing\": {\n";
    oss << "      \"aliasedAllocations\": " << frameStats.totals.aliasedAllocations << ",\n";
    oss << "      \"bytesSaved\": " << frameStats.totals.bytesSavedViaAliasing << ",\n";
    oss << "      \"efficiencyPercent\": " << std::fixed << std::setprecision(1)
        << frameStats.totals.GetAliasingEfficiency() << "\n";
    oss << "    }\n";
    oss << "  },\n";

    // Per-node stats
    oss << "  \"nodes\": [\n";
    for (size_t i = 0; i < frameStats.nodeStats.size(); ++i) {
        const auto& nodeStats = frameStats.nodeStats[i];

        oss << "    {\n";
        oss << "      \"nodeId\": " << nodeStats.nodeId << ",\n";
        oss << "      \"nodeName\": \"" << nodeStats.nodeName << "\",\n";
        oss << "      \"allocations\": {\n";
        oss << "        \"stack\": " << nodeStats.stackAllocations << ",\n";
        oss << "        \"heap\": " << nodeStats.heapAllocations << ",\n";
        oss << "        \"vram\": " << nodeStats.vramAllocations << "\n";
        oss << "      },\n";
        oss << "      \"bytesUsed\": {\n";
        oss << "        \"stack\": " << nodeStats.stackBytesUsed << ",\n";
        oss << "        \"heap\": " << nodeStats.heapBytesUsed << ",\n";
        oss << "        \"vram\": " << nodeStats.vramBytesUsed << "\n";
        oss << "      },\n";
        oss << "      \"aliasing\": {\n";
        oss << "        \"aliasedAllocations\": " << nodeStats.aliasedAllocations << ",\n";
        oss << "        \"bytesSaved\": " << nodeStats.bytesSavedViaAliasing << ",\n";
        oss << "        \"efficiencyPercent\": " << std::fixed << std::setprecision(1)
            << nodeStats.GetAliasingEfficiency() << "\n";
        oss << "      }\n";
        oss << "    }";

        if (i < frameStats.nodeStats.size() - 1) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "  ]\n";

    oss << "}\n";

    return oss.str();
}

// =============================================================================
// Helper Methods
// =============================================================================

void ResourceProfiler::PruneOldFrames() {
    if (frameHistory_.size() <= maxFrameHistory_) {
        return;
    }

    // Find oldest frames to remove
    std::vector<uint64_t> frameNumbers;
    frameNumbers.reserve(frameHistory_.size());
    for (const auto& [frameNum, _] : frameHistory_) {
        frameNumbers.push_back(frameNum);
    }
    std::sort(frameNumbers.begin(), frameNumbers.end());

    // Remove oldest frames
    size_t numToRemove = frameHistory_.size() - maxFrameHistory_;
    for (size_t i = 0; i < numToRemove; ++i) {
        frameHistory_.erase(frameNumbers[i]);
    }
}

NodeResourceStats& ResourceProfiler::GetOrCreateNodeStats(
    uint32_t nodeId,
    const std::string& nodeName
) {
    auto it = currentFrameStats_.find(nodeId);
    if (it == currentFrameStats_.end()) {
        NodeResourceStats stats;
        stats.nodeId = nodeId;
        stats.nodeName = nodeName;
        currentFrameStats_[nodeId] = stats;
        return currentFrameStats_[nodeId];
    }
    return it->second;
}

void ResourceProfiler::UpdatePeakUsage() {
    peakStackUsage_ = std::max(peakStackUsage_, currentStackUsage_);
    peakHeapUsage_ = std::max(peakHeapUsage_, currentHeapUsage_);
    peakVramUsage_ = std::max(peakVramUsage_, currentVramUsage_);
}

void ResourceProfiler::LogAllocation(
    uint32_t nodeId,
    const std::string& nodeName,
    ResourceLocation location,
    size_t bytes,
    bool wasAliased
) {
    const char* locationStr = "Unknown";
    switch (location) {
        case ResourceLocation::Stack: locationStr = "Stack"; break;
        case ResourceLocation::Heap:  locationStr = "Heap";  break;
        case ResourceLocation::VRAM:  locationStr = "VRAM";  break;
    }

    std::cout << "[ResourceProfiler] Allocation - Node: " << nodeName
              << " (ID: " << nodeId << ")"
              << ", Location: " << locationStr
              << ", Bytes: " << bytes
              << " (" << std::fixed << std::setprecision(2)
              << (bytes / 1024.0 / 1024.0) << " MB)";

    if (wasAliased) {
        std::cout << " [ALIASED]";
    }

    std::cout << "\n";
}

void ResourceProfiler::LogRelease(
    uint32_t nodeId,
    const std::string& nodeName,
    size_t bytes
) {
    std::cout << "[ResourceProfiler] Release - Node: " << nodeName
              << " (ID: " << nodeId << ")"
              << ", Bytes: " << bytes
              << " (" << std::fixed << std::setprecision(2)
              << (bytes / 1024.0 / 1024.0) << " MB)\n";
}

} // namespace Vixen::RenderGraph
