#pragma once

#include "ILoggable.h"
#include <functional>
#include <vector>
#include <string>

namespace Vixen::RenderGraph {

// Forward declarations
class RenderGraph;

/**
 * @brief Graph-level lifecycle phases
 *
 * These hooks execute once per graph compilation at specific points in the
 * render graph build/compile pipeline. Use for graph-wide setup that needs
 * to happen at specific times (e.g., connection finalization, resource allocation).
 */
enum class GraphLifecyclePhase {
    PreTopologyBuild,      // Before dependency analysis and topological sort
    PostTopologyBuild,     // After dependency graph built and validated
    PreExecutionOrder,     // Before execution order calculation
    PostExecutionOrder,    // After execution order determined
    PreCompilation,        // Before any node Setup/Compile called
    PostCompilation        // After all nodes compiled successfully
};

/**
 * @brief Type-safe hook callback types
 */
using GraphLifecycleCallback = std::function<void(RenderGraph*)>;

/**
 * @brief Graph-wide lifecycle hooks
 *
 * Callbacks for phases that belong to the whole graph (topology built, compilation finished),
 * executed in registration order within each phase. A node's own lifecycle is not here: it reacts
 * through its own typed events (Setup/Compile/Execute, CleanupImpl/DeviceReleaseImpl/ShutdownImpl,
 * RecompileImpl with its RecompileCause; see NodeInstance), and a variadic node refreshes its
 * declared slots itself (VariadicTypedNode).
 */
class GraphLifecycleHooks : public ILoggable {
public:
    GraphLifecycleHooks();
    ~GraphLifecycleHooks() = default;

    // Prevent copying (hooks are tied to specific graph instance)
    GraphLifecycleHooks(const GraphLifecycleHooks&) = delete;
    GraphLifecycleHooks& operator=(const GraphLifecycleHooks&) = delete;

    /**
     * @brief Register a graph-level lifecycle callback
     *
     * @param phase When to execute the callback
     * @param callback Function to execute
     * @param debugName Optional name for debugging/logging
     */
    void RegisterGraphHook(
        GraphLifecyclePhase phase,
        GraphLifecycleCallback callback,
        const std::string& debugName = ""
    );

    /**
     * @brief Execute all registered graph hooks for a specific phase
     *
     * @param phase Current lifecycle phase
     * @param graph RenderGraph instance
     */
    void ExecuteGraphHooks(GraphLifecyclePhase phase, RenderGraph* graph);

    /**
     * @brief Clear all registered hooks
     *
     * Useful for graph reset/recompilation scenarios
     */
    void ClearAll();

    /**
     * @brief Clear hooks for a specific graph phase
     */
    void ClearGraphHooks(GraphLifecyclePhase phase);

    /**
     * @brief Get human-readable phase name for debugging
     */
    static const char* GetPhaseName(GraphLifecyclePhase phase);

private:
    struct GraphHookEntry {
        GraphLifecycleCallback callback;
        std::string debugName;
    };

    // Storage for each phase (indexed by enum value)
    std::vector<GraphHookEntry> graphHooks_[6];  // 6 GraphLifecyclePhase values

};

} // namespace Vixen::RenderGraph
