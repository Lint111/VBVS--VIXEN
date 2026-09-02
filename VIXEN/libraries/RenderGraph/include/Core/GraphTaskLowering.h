#pragma once

#include "Core/GraphTopology.h"
#include "Core/ResourceAccessTracker.h"
#include "KernelDispatch/TaskDependencyGraph.h"
#include "KernelDispatch/VirtualTask.h"
#include <unordered_map>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief The Tier-B compiler output consumed by the shared Tier-A executor.
 *
 * One task represents one graph node. `dependencyGraph` contains authored connection edges and
 * conservative access-hazard edges, while serial-only nodes are chained in compiled graph order.
 * The plan owns no nodes; RenderGraph owns the node instances and keeps them alive until execution
 * returns.
 */
struct GraphTaskPlan {
    std::vector<KernelDispatch::VirtualTask> tasks;
    KernelDispatch::TaskDependencyGraph dependencyGraph;
    std::vector<std::vector<KernelDispatch::TaskId>> waves;
    std::unordered_map<NodeInstance*, KernelDispatch::TaskId> taskIdsByNode;
    std::unordered_map<KernelDispatch::TaskId, NodeInstance*, KernelDispatch::TaskIdHash> nodesByTask;

    [[nodiscard]] KernelDispatch::TaskId FindTaskId(const NodeInstance* node) const;
    [[nodiscard]] NodeInstance* FindNode(const KernelDispatch::TaskId& taskId) const;
    [[nodiscard]] KernelDispatch::VirtualTask* FindTask(const KernelDispatch::TaskId& taskId);
    [[nodiscard]] const KernelDispatch::VirtualTask* FindTask(
        const KernelDispatch::TaskId& taskId) const;
};

/** @brief Lowers RenderGraph's Tier-B topology/access model to the shared Tier-A task DAG. */
class GraphTaskLowering {
public:
    [[nodiscard]] static GraphTaskPlan Build(
        const GraphTopology& topology,
        const std::vector<NodeInstance*>& executionOrder,
        const ResourceAccessTracker& accessTracker);

private:
    [[nodiscard]] static bool IsCpuEligible(const NodeInstance* node);
};

} // namespace Vixen::RenderGraph
