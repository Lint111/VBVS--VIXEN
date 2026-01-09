// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

#include "Core/VirtualResourceAccessTracker.h"
#include "Data/Core/CompileTimeResourceSystem.h"  // Resource class
#include <algorithm>

namespace Vixen::RenderGraph {

// ============================================================================
// VirtualResourceAccessInfo Implementation
// ============================================================================

std::vector<VirtualTaskId> VirtualResourceAccessInfo::GetWriters() const {
    std::vector<VirtualTaskId> writers;
    for (const auto& access : accesses) {
        if (access.accessType == ResourceAccessType::Write ||
            access.accessType == ResourceAccessType::ReadWrite) {
            writers.push_back(access.task);
        }
    }
    return writers;
}

std::vector<VirtualTaskId> VirtualResourceAccessInfo::GetReaders() const {
    std::vector<VirtualTaskId> readers;
    for (const auto& access : accesses) {
        if (access.accessType == ResourceAccessType::Read ||
            access.accessType == ResourceAccessType::ReadWrite) {
            readers.push_back(access.task);
        }
    }
    return readers;
}

bool VirtualResourceAccessInfo::HasWriter() const {
    return std::any_of(accesses.begin(), accesses.end(), [](const VirtualResourceAccess& a) {
        return a.accessType == ResourceAccessType::Write ||
               a.accessType == ResourceAccessType::ReadWrite;
    });
}

bool VirtualResourceAccessInfo::HasMultipleWriters() const {
    return GetWriterCount() > 1;
}

size_t VirtualResourceAccessInfo::GetWriterCount() const {
    size_t count = 0;
    for (const auto& access : accesses) {
        if (access.accessType == ResourceAccessType::Write ||
            access.accessType == ResourceAccessType::ReadWrite) {
            ++count;
        }
    }
    return count;
}

size_t VirtualResourceAccessInfo::GetReaderCount() const {
    size_t count = 0;
    for (const auto& access : accesses) {
        if (access.accessType == ResourceAccessType::Read ||
            access.accessType == ResourceAccessType::ReadWrite) {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// VirtualResourceAccessTracker Implementation
// ============================================================================

void VirtualResourceAccessTracker::BuildFromTopology(const GraphTopology& topology) {
    Clear();

    for (NodeInstance* node : topology.GetNodes()) {
        if (node) {
            AddNode(node);
        }
    }
}

void VirtualResourceAccessTracker::AddNode(NodeInstance* node) {
    if (!node) return;

    const auto& bundles = node->GetBundles();

    // Create VirtualTaskId for each bundle
    // If no bundles, create a single task with index 0
    uint32_t taskCount = bundles.empty() ? 1 : static_cast<uint32_t>(bundles.size());

    for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
        VirtualTaskId taskId{node, taskIndex};

        // Track this task for the node
        nodeTasks_[node].push_back(taskId);

        // If no bundles, nothing to track
        if (bundles.empty()) continue;

        const auto& bundle = bundles[taskIndex];

        // Outputs are WRITES for this specific task
        for (size_t slotIndex = 0; slotIndex < bundle.outputs.size(); ++slotIndex) {
            Resource* resource = bundle.outputs[slotIndex];
            if (resource) {
                RecordAccess(
                    resource,
                    taskId,
                    ResourceAccessType::Write,
                    static_cast<uint32_t>(slotIndex),
                    true  // isOutput
                );
            }
        }

        // Inputs are READS for this specific task
        // TODO: Check SlotMutability for ReadWrite inputs
        for (size_t slotIndex = 0; slotIndex < bundle.inputs.size(); ++slotIndex) {
            Resource* resource = bundle.inputs[slotIndex];
            if (resource) {
                RecordAccess(
                    resource,
                    taskId,
                    ResourceAccessType::Read,
                    static_cast<uint32_t>(slotIndex),
                    false  // isOutput
                );
            }
        }
    }
}

void VirtualResourceAccessTracker::Clear() {
    resourceAccesses_.clear();
    taskResources_.clear();
    taskWrites_.clear();
    taskReads_.clear();
    nodeTasks_.clear();
}

bool VirtualResourceAccessTracker::HasConflict(
    const VirtualTaskId& taskA,
    const VirtualTaskId& taskB
) const {
    // Same task never conflicts with itself
    if (taskA == taskB) return false;

    // Invalid tasks don't conflict
    if (!taskA.IsValid() || !taskB.IsValid()) return false;

    // Get writes from both tasks
    auto itWritesA = taskWrites_.find(taskA);
    auto itWritesB = taskWrites_.find(taskB);
    auto itReadsA = taskReads_.find(taskA);
    auto itReadsB = taskReads_.find(taskB);

    // Check: A writes to something B writes to (Write-Write conflict)
    if (itWritesA != taskWrites_.end() && itWritesB != taskWrites_.end()) {
        for (Resource* resA : itWritesA->second) {
            if (itWritesB->second.count(resA) > 0) {
                return true;  // Both write to same resource
            }
        }
    }

    // Check: A writes to something B reads (Write-Read conflict)
    if (itWritesA != taskWrites_.end() && itReadsB != taskReads_.end()) {
        for (Resource* resA : itWritesA->second) {
            if (itReadsB->second.count(resA) > 0) {
                return true;  // A writes what B reads
            }
        }
    }

    // Check: B writes to something A reads (Read-Write conflict)
    if (itWritesB != taskWrites_.end() && itReadsA != taskReads_.end()) {
        for (Resource* resB : itWritesB->second) {
            if (itReadsA->second.count(resB) > 0) {
                return true;  // B writes what A reads
            }
        }
    }

    // No conflict: either no shared resources, or both only read shared resources
    return false;
}

std::unordered_set<VirtualTaskId, VirtualTaskIdHash>
VirtualResourceAccessTracker::GetConflictingTasks(const VirtualTaskId& task) const {
    std::unordered_set<VirtualTaskId, VirtualTaskIdHash> conflicting;
    if (!task.IsValid()) return conflicting;

    // Get all resources this task accesses
    auto itResources = taskResources_.find(task);
    if (itResources == taskResources_.end()) return conflicting;

    // Check each resource for conflicts
    for (Resource* resource : itResources->second) {
        auto itAccess = resourceAccesses_.find(resource);
        if (itAccess == resourceAccesses_.end()) continue;

        const auto& info = itAccess->second;

        // Check if this task writes to the resource
        auto itWrites = taskWrites_.find(task);
        bool taskWritesToResource = itWrites != taskWrites_.end() &&
                                    itWrites->second.count(resource) > 0;

        for (const auto& access : info.accesses) {
            if (access.task == task) continue;

            if (taskWritesToResource) {
                // Task writes → conflict with all other accessors
                conflicting.insert(access.task);
            } else if (access.accessType == ResourceAccessType::Write ||
                       access.accessType == ResourceAccessType::ReadWrite) {
                // Other task writes → conflict
                conflicting.insert(access.task);
            }
            // Both read → no conflict
        }
    }

    return conflicting;
}

std::vector<Resource*> VirtualResourceAccessTracker::GetSharedResources(
    const VirtualTaskId& taskA,
    const VirtualTaskId& taskB
) const {
    std::vector<Resource*> shared;
    if (!taskA.IsValid() || !taskB.IsValid()) return shared;

    auto itA = taskResources_.find(taskA);
    auto itB = taskResources_.find(taskB);
    if (itA == taskResources_.end() || itB == taskResources_.end()) return shared;

    // Convert B's resources to set for O(1) lookup
    std::unordered_set<Resource*> resourcesB(itB->second.begin(), itB->second.end());

    for (Resource* res : itA->second) {
        if (resourcesB.count(res) > 0) {
            shared.push_back(res);
        }
    }

    return shared;
}

bool VirtualResourceAccessTracker::HasIntraNodeConflict(
    NodeInstance* node,
    uint32_t taskIndexA,
    uint32_t taskIndexB
) const {
    if (!node || taskIndexA == taskIndexB) return false;

    VirtualTaskId taskA{node, taskIndexA};
    VirtualTaskId taskB{node, taskIndexB};

    return HasConflict(taskA, taskB);
}

const VirtualResourceAccessInfo* VirtualResourceAccessTracker::GetAccessInfo(Resource* resource) const {
    auto it = resourceAccesses_.find(resource);
    return it != resourceAccesses_.end() ? &it->second : nullptr;
}

std::vector<Resource*> VirtualResourceAccessTracker::GetTaskResources(const VirtualTaskId& task) const {
    auto it = taskResources_.find(task);
    return it != taskResources_.end() ? it->second : std::vector<Resource*>{};
}

std::vector<Resource*> VirtualResourceAccessTracker::GetTaskWrites(const VirtualTaskId& task) const {
    auto it = taskWrites_.find(task);
    if (it == taskWrites_.end()) return {};
    return std::vector<Resource*>(it->second.begin(), it->second.end());
}

std::vector<Resource*> VirtualResourceAccessTracker::GetTaskReads(const VirtualTaskId& task) const {
    auto it = taskReads_.find(task);
    if (it == taskReads_.end()) return {};
    return std::vector<Resource*>(it->second.begin(), it->second.end());
}

bool VirtualResourceAccessTracker::IsWriter(const VirtualTaskId& task) const {
    auto it = taskWrites_.find(task);
    return it != taskWrites_.end() && !it->second.empty();
}

std::vector<VirtualTaskId> VirtualResourceAccessTracker::GetNodeTasks(NodeInstance* node) const {
    auto it = nodeTasks_.find(node);
    return it != nodeTasks_.end() ? it->second : std::vector<VirtualTaskId>{};
}

uint32_t VirtualResourceAccessTracker::GetNodeTaskCount(NodeInstance* node) const {
    auto it = nodeTasks_.find(node);
    return it != nodeTasks_.end() ? static_cast<uint32_t>(it->second.size()) : 0;
}

size_t VirtualResourceAccessTracker::GetConflictingResourceCount() const {
    size_t count = 0;
    for (const auto& [resource, info] : resourceAccesses_) {
        if (info.HasWriter() && info.accesses.size() > 1) {
            // Resource has a writer and multiple accessors = potential conflict
            ++count;
        }
    }
    return count;
}

size_t VirtualResourceAccessTracker::GetMaxWritersPerResource() const {
    size_t maxWriters = 0;
    for (const auto& [resource, info] : resourceAccesses_) {
        size_t writers = info.GetWriterCount();
        maxWriters = std::max(maxWriters, writers);
    }
    return maxWriters;
}

float VirtualResourceAccessTracker::GetParallelismPotential() const {
    if (taskResources_.empty()) return 1.0f;  // Empty = fully parallel

    // Count non-conflicting task pairs
    size_t totalPairs = 0;
    size_t parallelPairs = 0;

    std::vector<VirtualTaskId> tasks;
    tasks.reserve(taskResources_.size());
    for (const auto& [task, _] : taskResources_) {
        tasks.push_back(task);
    }

    for (size_t i = 0; i < tasks.size(); ++i) {
        for (size_t j = i + 1; j < tasks.size(); ++j) {
            ++totalPairs;
            if (!HasConflict(tasks[i], tasks[j])) {
                ++parallelPairs;
            }
        }
    }

    return totalPairs > 0 ? static_cast<float>(parallelPairs) / totalPairs : 1.0f;
}

void VirtualResourceAccessTracker::RecordAccess(
    Resource* resource,
    const VirtualTaskId& task,
    ResourceAccessType accessType,
    uint32_t slotIndex,
    bool isOutput
) {
    if (!resource || !task.IsValid()) return;

    // Record in resource -> accesses map
    auto& info = resourceAccesses_[resource];
    info.resource = resource;

    // Check if we already have an access record for this task+slot
    auto existingIt = std::find_if(info.accesses.begin(), info.accesses.end(),
        [&task, slotIndex, isOutput](const VirtualResourceAccess& a) {
            return a.task == task && a.slotIndex == slotIndex && a.isOutput == isOutput;
        });

    if (existingIt == info.accesses.end()) {
        info.accesses.push_back({task, accessType, slotIndex, isOutput});
    } else {
        // Upgrade to ReadWrite if needed
        if (accessType != existingIt->accessType) {
            existingIt->accessType = ResourceAccessType::ReadWrite;
        }
    }

    // Record in task -> resources map
    auto& resources = taskResources_[task];
    if (std::find(resources.begin(), resources.end(), resource) == resources.end()) {
        resources.push_back(resource);
    }

    // Record in task -> writes/reads maps
    if (accessType == ResourceAccessType::Write || accessType == ResourceAccessType::ReadWrite) {
        taskWrites_[task].insert(resource);
    }
    if (accessType == ResourceAccessType::Read || accessType == ResourceAccessType::ReadWrite) {
        taskReads_[task].insert(resource);
    }
}

} // namespace Vixen::RenderGraph
