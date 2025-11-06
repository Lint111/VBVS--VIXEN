#include "Core/GraphLifecycleHooks.h"
#include "Core/RenderGraph.h"
#include "Core/NodeInstance.h"

namespace Vixen::RenderGraph {

void GraphLifecycleHooks::RegisterGraphHook(
    GraphLifecyclePhase phase,
    GraphLifecycleCallback callback,
    const std::string& debugName
) {
    size_t index = static_cast<size_t>(phase);
    graphHooks_[index].push_back({callback, debugName});

    if (!debugName.empty()) {
        std::cout << "[GraphLifecycleHooks] Registered graph hook '" << debugName
                  << "' for phase: " << GetPhaseName(phase) << std::endl;
    }
}

void GraphLifecycleHooks::RegisterNodeHook(
    NodeLifecyclePhase phase,
    NodeLifecycleCallback callback,
    const std::string& debugName
) {
    size_t index = static_cast<size_t>(phase);
    nodeHooks_[index].push_back({callback, debugName});

    if (!debugName.empty()) {
        std::cout << "[GraphLifecycleHooks] Registered node hook '" << debugName
                  << "' for phase: " << GetPhaseName(phase) << std::endl;
    }
}

void GraphLifecycleHooks::ExecuteGraphHooks(GraphLifecyclePhase phase, RenderGraph* graph) {
    size_t index = static_cast<size_t>(phase);
    const auto& hooks = graphHooks_[index];

    if (hooks.empty()) {
        return;
    }

    std::cout << "[GraphLifecycleHooks] Executing " << hooks.size()
              << " graph hooks for phase: " << GetPhaseName(phase) << std::endl;

    for (const auto& entry : hooks) {
        if (!entry.debugName.empty()) {
            std::cout << "[GraphLifecycleHooks]   Executing: " << entry.debugName << std::endl;
        }

        try {
            entry.callback(graph);
        } catch (const std::exception& e) {
            std::cerr << "[GraphLifecycleHooks] ERROR in hook '" << entry.debugName
                      << "': " << e.what() << std::endl;
            throw;
        }
    }
}

void GraphLifecycleHooks::ExecuteNodeHooks(NodeLifecyclePhase phase, NodeInstance* node) {
    size_t index = static_cast<size_t>(phase);
    const auto& hooks = nodeHooks_[index];

    if (hooks.empty()) {
        return;
    }

    std::cout << "[GraphLifecycleHooks] Executing " << hooks.size()
              << " node hooks for phase: " << GetPhaseName(phase)
              << " on node: " << node->GetInstanceName() << std::endl;

    for (const auto& entry : hooks) {
        if (!entry.debugName.empty()) {
            std::cout << "[GraphLifecycleHooks]   Executing: " << entry.debugName << std::endl;
        }

        try {
            entry.callback(node);
        } catch (const std::exception& e) {
            std::cerr << "[GraphLifecycleHooks] ERROR in node hook '" << entry.debugName
                      << "' for node '" << node->GetInstanceName() << "': " << e.what() << std::endl;
            throw;
        }
    }
}

void GraphLifecycleHooks::ClearAll() {
    for (auto& hooks : graphHooks_) {
        hooks.clear();
    }
    for (auto& hooks : nodeHooks_) {
        hooks.clear();
    }
}

void GraphLifecycleHooks::ClearGraphHooks(GraphLifecyclePhase phase) {
    size_t index = static_cast<size_t>(phase);
    graphHooks_[index].clear();
}

void GraphLifecycleHooks::ClearNodeHooks(NodeLifecyclePhase phase) {
    size_t index = static_cast<size_t>(phase);
    nodeHooks_[index].clear();
}

const char* GraphLifecycleHooks::GetPhaseName(GraphLifecyclePhase phase) {
    switch (phase) {
        case GraphLifecyclePhase::PreTopologyBuild:   return "PreTopologyBuild";
        case GraphLifecyclePhase::PostTopologyBuild:  return "PostTopologyBuild";
        case GraphLifecyclePhase::PreExecutionOrder:  return "PreExecutionOrder";
        case GraphLifecyclePhase::PostExecutionOrder: return "PostExecutionOrder";
        case GraphLifecyclePhase::PreCompilation:     return "PreCompilation";
        case GraphLifecyclePhase::PostCompilation:    return "PostCompilation";
        default: return "Unknown";
    }
}

const char* GraphLifecycleHooks::GetPhaseName(NodeLifecyclePhase phase) {
    switch (phase) {
        case NodeLifecyclePhase::PreSetup:    return "PreSetup";
        case NodeLifecyclePhase::PostSetup:   return "PostSetup";
        case NodeLifecyclePhase::PreCompile:  return "PreCompile";
        case NodeLifecyclePhase::PostCompile: return "PostCompile";
        case NodeLifecyclePhase::PreExecute:  return "PreExecute";
        case NodeLifecyclePhase::PostExecute: return "PostExecute";
        case NodeLifecyclePhase::PreCleanup:  return "PreCleanup";
        case NodeLifecyclePhase::PostCleanup: return "PostCleanup";
        default: return "Unknown";
    }
}

} // namespace Vixen::RenderGraph
