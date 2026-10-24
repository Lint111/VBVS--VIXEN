#include "Core/GraphLifecycleHooks.h"
#include "Core/RenderGraph.h"

namespace Vixen::RenderGraph {

GraphLifecycleHooks::GraphLifecycleHooks() {
    InitializeLogger("LifecycleHooks");
}

void GraphLifecycleHooks::RegisterGraphHook(
    GraphLifecyclePhase phase,
    GraphLifecycleCallback callback,
    const std::string& debugName
) {
    size_t index = static_cast<size_t>(phase);
    graphHooks_[index].push_back({callback, debugName});

    if (!debugName.empty()) {
        LOG_DEBUG("Registered graph hook '" + debugName +
                 "' for phase: " + std::string(GetPhaseName(phase)));
    }
}

void GraphLifecycleHooks::ExecuteGraphHooks(GraphLifecyclePhase phase, RenderGraph* graph) {
    size_t index = static_cast<size_t>(phase);
    const auto& hooks = graphHooks_[index];

    if (hooks.empty()) {
        return;
    }

    LOG_DEBUG("Executing " + std::to_string(hooks.size()) +
             " graph hooks for phase: " + std::string(GetPhaseName(phase)));

    for (const auto& entry : hooks) {
        if (!entry.debugName.empty()) {
            LOG_DEBUG("  Executing: " + entry.debugName);
        }

        try {
            entry.callback(graph);
        } catch (const std::exception& e) {
            LOG_ERROR("ERROR in hook '" + entry.debugName +
                     "': " + std::string(e.what()));
            throw;
        }
    }
}

void GraphLifecycleHooks::ClearAll() {
    for (auto& hooks : graphHooks_) {
        hooks.clear();
    }
}

void GraphLifecycleHooks::ClearGraphHooks(GraphLifecyclePhase phase) {
    size_t index = static_cast<size_t>(phase);
    graphHooks_[index].clear();
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

} // namespace Vixen::RenderGraph
