#pragma once

#include <functional>
#include <vector>
#include <string>
#include <iostream>

namespace Vixen::RenderGraph {

// Forward declarations
class RenderGraph;
class NodeInstance;

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
 * @brief Node-level lifecycle phases
 *
 * These hooks execute per-node during compilation. Use for node-specific
 * setup that depends on other nodes (e.g., variadic slot creation after
 * source node setup completes).
 */
enum class NodeLifecyclePhase {
    PreSetup,             // Before Setup() called on this node
    PostSetup,            // After Setup() completes successfully
    PreCompile,           // Before Compile() called on this node
    PostCompile,          // After Compile() completes successfully
    PreExecute,           // Before Execute() called on this node (per-frame)
    PostExecute,          // After Execute() completes successfully (per-frame)
    PreCleanup,           // Before Cleanup() called on this node
    PostCleanup           // After Cleanup() completes successfully
};

/**
 * @brief Type-safe hook callback types
 */
using GraphLifecycleCallback = std::function<void(RenderGraph*)>;
using NodeLifecycleCallback = std::function<void(NodeInstance*)>;

/**
 * @brief Hook registration with phase-specific callbacks
 *
 * Centralized hook management for render graph lifecycle events.
 * Callbacks are executed in registration order within each phase.
 */
class GraphLifecycleHooks {
public:
    GraphLifecycleHooks() = default;
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
     * @brief Register a node-level lifecycle callback
     *
     * @param phase When to execute the callback
     * @param callback Function to execute (receives node instance)
     * @param debugName Optional name for debugging/logging
     */
    void RegisterNodeHook(
        NodeLifecyclePhase phase,
        NodeLifecycleCallback callback,
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
     * @brief Execute all registered node hooks for a specific phase
     *
     * @param phase Current lifecycle phase
     * @param node NodeInstance being processed
     */
    void ExecuteNodeHooks(NodeLifecyclePhase phase, NodeInstance* node);

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
     * @brief Clear hooks for a specific node phase
     */
    void ClearNodeHooks(NodeLifecyclePhase phase);

    /**
     * @brief Get human-readable phase name for debugging
     */
    static const char* GetPhaseName(GraphLifecyclePhase phase);
    static const char* GetPhaseName(NodeLifecyclePhase phase);

private:
    struct GraphHookEntry {
        GraphLifecycleCallback callback;
        std::string debugName;
    };

    struct NodeHookEntry {
        NodeLifecycleCallback callback;
        std::string debugName;
    };

    // Storage for each phase (indexed by enum value)
    std::vector<GraphHookEntry> graphHooks_[6];  // 6 GraphLifecyclePhase values
    std::vector<NodeHookEntry> nodeHooks_[8];    // 8 NodeLifecyclePhase values
};

} // namespace Vixen::RenderGraph
