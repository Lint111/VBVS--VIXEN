#include "Core/GraphTaskLowering.h"

#include <utility>

namespace Vixen::RenderGraph {

KernelDispatch::TaskId GraphTaskPlan::FindTaskId(const NodeInstance* node) const {
    if (!node) return {};
    const auto it = taskIdsByNode.find(const_cast<NodeInstance*>(node));
    return it == taskIdsByNode.end() ? KernelDispatch::TaskId{} : it->second;
}

NodeInstance* GraphTaskPlan::FindNode(const KernelDispatch::TaskId& taskId) const {
    const auto it = nodesByTask.find(taskId);
    return it == nodesByTask.end() ? nullptr : it->second;
}

KernelDispatch::VirtualTask* GraphTaskPlan::FindTask(const KernelDispatch::TaskId& taskId) {
    for (auto& task : tasks) {
        if (task.id == taskId) return &task;
    }
    return nullptr;
}

const KernelDispatch::VirtualTask* GraphTaskPlan::FindTask(
    const KernelDispatch::TaskId& taskId) const {
    for (const auto& task : tasks) {
        if (task.id == taskId) return &task;
    }
    return nullptr;
}

bool GraphTaskLowering::IsCpuEligible(const NodeInstance* node) {
    return node && node->GetNodeType() &&
           node->GetNodeType()->GetGraphExecutionKind() == GraphExecutionKind::Cpu;
}

GraphTaskPlan GraphTaskLowering::Build(
    const GraphTopology& topology,
    const std::vector<NodeInstance*>& executionOrder,
    const ResourceAccessTracker& accessTracker) {
    GraphTaskPlan plan;
    plan.tasks.reserve(executionOrder.size());

    // IDs are graph-local and stable: compiled topological order is the Tier-B identity that must
    // remain invariant across worker counts. A non-zero owner keeps TaskId::IsValid true.
    for (size_t index = 0; index < executionOrder.size(); ++index) {
        NodeInstance* const node = executionOrder[index];
        if (!node) continue;

        const KernelDispatch::TaskId taskId{static_cast<uint64_t>(index + 1), 0};
        plan.dependencyGraph.AddTask(taskId);
        plan.taskIdsByNode.emplace(node, taskId);
        plan.nodesByTask.emplace(taskId, node);

        KernelDispatch::VirtualTask task;
        task.id = taskId;
        task.execute = [node] { node->ExecuteAsGraphTask(); };
        plan.tasks.push_back(std::move(task));
    }

    const auto addEdge = [&plan](NodeInstance* source, NodeInstance* target) {
        const auto sourceIt = plan.taskIdsByNode.find(source);
        const auto targetIt = plan.taskIdsByNode.find(target);
        if (sourceIt == plan.taskIdsByNode.end() || targetIt == plan.taskIdsByNode.end() ||
            sourceIt->second == targetIt->second) {
            return;
        }
        if (!plan.dependencyGraph.HasDependency(sourceIt->second, targetIt->second)) {
            plan.dependencyGraph.AddEdge(sourceIt->second, targetIt->second);
        }
    };

    // Authored connection edges are the primary Tier-B dependency model.
    for (const GraphEdge& edge : topology.GetEdges()) {
        addEdge(edge.source, edge.target);
    }

    // ResourceAccessTracker expands array constituents and declared ReadWrite accesses. Add a
    // conservative edge for every conflicting pair in compiled order, covering hazards not
    // represented by a direct connection while retaining the sequential driver's order.
    for (size_t sourceIndex = 0; sourceIndex < executionOrder.size(); ++sourceIndex) {
        for (size_t targetIndex = sourceIndex + 1;
             targetIndex < executionOrder.size(); ++targetIndex) {
            NodeInstance* const source = executionOrder[sourceIndex];
            NodeInstance* const target = executionOrder[targetIndex];
            if (source && target && accessTracker.HasConflict(source, target)) {
                addEdge(source, target);
            }
        }
    }

    // Unknown/Vulkan/callback-sensitive nodes remain on one ordered lane. Explicit CPU nodes may
    // share a wave, but topology and hazard edges still constrain them wherever data requires it.
    NodeInstance* previousSerialNode = nullptr;
    for (NodeInstance* const node : executionOrder) {
        if (!node || IsCpuEligible(node)) continue;
        if (previousSerialNode) addEdge(previousSerialNode, node);
        previousSerialNode = node;
    }

    // Materialize dependency lists as a diagnostic mirror of the shared graph. The executor uses
    // `waves`, while callers/tests can inspect each task independently.
    for (auto& task : plan.tasks) {
        task.dependencies = plan.dependencyGraph.GetDependencies(task.id);
    }
    plan.waves = plan.dependencyGraph.GetParallelLevels();
    return plan;
}

} // namespace Vixen::RenderGraph
