// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

#include "Core/WaveScheduler.h"
#include <algorithm>
#include <limits>
#include <sstream>

namespace Vixen::RenderGraph {

bool WaveScheduler::ComputeWaves(
    const GraphTopology& topology,
    const ResourceAccessTracker& accessTracker
) {
    Clear();

    // Cache for Recompute()
    cachedTopology_ = &topology;
    cachedAccessTracker_ = &accessTracker;

    // Get topological order - this respects dependencies
    auto topoOrder = topology.TopologicalSort();
    if (topoOrder.empty() && topology.GetNodeCount() > 0) {
        // Topological sort failed (cycle detected)
        return false;
    }

    totalNodes_ = topoOrder.size();
    conflictCount_ = 0;

    // Process each node in topological order
    for (NodeInstance* node : topoOrder) {
        if (!node) continue;

        // Step 1: Find earliest wave based on dependencies
        uint32_t earliestWave = FindEarliestWaveByDependencies(node);

        // Step 2: Find actual wave (may be later due to conflicts)
        uint32_t assignedWave = earliestWave;

        // Check for resource conflicts, moving to later waves if needed
        while (HasConflictInWave(node, assignedWave, accessTracker)) {
            ++assignedWave;
            ++conflictCount_;
        }

        // Step 3: Assign node to wave
        EnsureWaveExists(assignedWave);
        waves_[assignedWave].nodes.push_back(node);
        nodeToWave_[node] = assignedWave;
    }

    computed_ = true;
    return true;
}

void WaveScheduler::Recompute() {
    if (cachedTopology_ && cachedAccessTracker_) {
        ComputeWaves(*cachedTopology_, *cachedAccessTracker_);
    }
}

void WaveScheduler::Clear() {
    waves_.clear();
    nodeToWave_.clear();
    totalNodes_ = 0;
    conflictCount_ = 0;
    computed_ = false;
}

uint32_t WaveScheduler::GetNodeWave(NodeInstance* node) const {
    auto it = nodeToWave_.find(node);
    return it != nodeToWave_.end() ? it->second : UINT32_MAX;
}

WaveSchedulerStats WaveScheduler::GetStats() const {
    WaveSchedulerStats stats;
    stats.totalNodes = totalNodes_;
    stats.waveCount = waves_.size();
    stats.conflictCount = conflictCount_;

    if (waves_.empty()) {
        return stats;
    }

    stats.minWaveSize = std::numeric_limits<size_t>::max();
    stats.maxWaveSize = 0;
    size_t totalSize = 0;

    for (const auto& wave : waves_) {
        size_t size = wave.Size();
        stats.minWaveSize = std::min(stats.minWaveSize, size);
        stats.maxWaveSize = std::max(stats.maxWaveSize, size);
        totalSize += size;
    }

    stats.avgWaveSize = static_cast<float>(totalSize) / static_cast<float>(waves_.size());
    stats.parallelismFactor = stats.avgWaveSize;

    return stats;
}

float WaveScheduler::GetParallelismFactor() const {
    if (waves_.empty()) return 0.0f;
    return static_cast<float>(totalNodes_) / static_cast<float>(waves_.size());
}

float WaveScheduler::GetTheoreticalSpeedup() const {
    if (waves_.empty()) return 1.0f;
    return static_cast<float>(totalNodes_) / static_cast<float>(waves_.size());
}

bool WaveScheduler::Validate(
    const GraphTopology& topology,
    const ResourceAccessTracker& accessTracker,
    std::string& errorMessage
) const {
    if (!computed_) {
        errorMessage = "Waves not computed";
        return false;
    }

    // Check 1: All nodes scheduled
    size_t scheduledCount = 0;
    for (const auto& wave : waves_) {
        scheduledCount += wave.Size();
    }

    if (scheduledCount != topology.GetNodeCount()) {
        std::ostringstream oss;
        oss << "Node count mismatch: scheduled " << scheduledCount
            << " but topology has " << topology.GetNodeCount();
        errorMessage = oss.str();
        return false;
    }

    // Check 2: No dependency violations
    for (const auto& wave : waves_) {
        for (NodeInstance* node : wave.nodes) {
            auto deps = topology.GetDirectDependencies(node);
            for (NodeInstance* dep : deps) {
                uint32_t depWave = GetNodeWave(dep);
                if (depWave >= wave.waveIndex) {
                    std::ostringstream oss;
                    oss << "Dependency violation: " << node->GetInstanceName()
                        << " (wave " << wave.waveIndex << ") depends on "
                        << dep->GetInstanceName() << " (wave " << depWave << ")";
                    errorMessage = oss.str();
                    return false;
                }
            }
        }
    }

    // Check 3: No resource conflicts within waves
    for (const auto& wave : waves_) {
        for (size_t i = 0; i < wave.nodes.size(); ++i) {
            for (size_t j = i + 1; j < wave.nodes.size(); ++j) {
                if (accessTracker.HasConflict(wave.nodes[i], wave.nodes[j])) {
                    std::ostringstream oss;
                    oss << "Resource conflict in wave " << wave.waveIndex << ": "
                        << wave.nodes[i]->GetInstanceName() << " and "
                        << wave.nodes[j]->GetInstanceName();
                    errorMessage = oss.str();
                    return false;
                }
            }
        }
    }

    return true;
}

uint32_t WaveScheduler::FindEarliestWaveByDependencies(NodeInstance* node) const {
    uint32_t earliestWave = 0;

    // Check all dependencies - node must be in wave AFTER all dependencies
    auto deps = node->GetDependencies();
    for (NodeInstance* dep : deps) {
        auto it = nodeToWave_.find(dep);
        if (it != nodeToWave_.end()) {
            // Node must be in wave > dependency's wave
            earliestWave = std::max(earliestWave, it->second + 1);
        }
    }

    return earliestWave;
}

bool WaveScheduler::HasConflictInWave(
    NodeInstance* node,
    uint32_t waveIndex,
    const ResourceAccessTracker& accessTracker
) const {
    if (waveIndex >= waves_.size()) {
        // Wave doesn't exist yet, so no conflicts
        return false;
    }

    const auto& wave = waves_[waveIndex];
    for (NodeInstance* existingNode : wave.nodes) {
        if (accessTracker.HasConflict(node, existingNode)) {
            return true;
        }
    }

    return false;
}

void WaveScheduler::EnsureWaveExists(uint32_t waveIndex) {
    while (waves_.size() <= waveIndex) {
        ExecutionWave wave;
        wave.waveIndex = static_cast<uint32_t>(waves_.size());
        waves_.push_back(std::move(wave));
    }
}

} // namespace Vixen::RenderGraph
