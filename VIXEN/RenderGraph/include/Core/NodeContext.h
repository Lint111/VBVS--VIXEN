#pragma once
#include <cstdint>

namespace Vixen::RenderGraph {

class NodeInstance;

/**
 * @brief Base context for all lifecycle phases
 *
 * Provides common functionality across Setup/Compile/Execute/Cleanup.
 * Derived context classes add phase-specific capabilities.
 */
struct BaseContext {
    NodeInstance* node;
    uint32_t taskIndex;

    BaseContext(NodeInstance* n, uint32_t idx)
        : node(n), taskIndex(idx) {}

    virtual ~BaseContext() = default;

public:
    // Constructor for derived classes that need to override node pointer type
    template<typename NodeT>
    BaseContext(NodeT* n, uint32_t idx)
        : node(static_cast<NodeInstance*>(n)), taskIndex(idx) {}
};

/**
 * @brief Context for Setup phase
 *
 * Setup cannot access inputs or outputs - graph topology not finalized.
 * Use for graph-scope initialization only.
 */
struct SetupContext : public BaseContext {
    SetupContext(NodeInstance* n, uint32_t taskIdx)
        : BaseContext(n, taskIdx) {}

public:
    // Constructor for derived typed contexts
    template<typename NodeT>
    SetupContext(NodeT* n, uint32_t taskIdx)
        : BaseContext(n, taskIdx) {}
};

/**
 * @brief Context for Compile phase
 *
 * Compile can read inputs and write outputs.
 * Graph topology is finalized, resources can be allocated.
 */
struct CompileContext : public BaseContext {
    CompileContext(NodeInstance* n, uint32_t taskIdx)
        : BaseContext(n, taskIdx) {}

public:
    // Constructor for derived typed contexts
    template<typename NodeT>
    CompileContext(NodeT* n , uint32_t taskIdx)
        : BaseContext(n, taskIdx) {}
};

/**
 * @brief Context for Execute phase
 *
 * Execute runs per-task with task-bound input/output access.
 * Multiple tasks may execute for nodes with TaskLevel slots.
 */
struct ExecuteContext : public BaseContext {
    ExecuteContext(NodeInstance* n, uint32_t taskIdx)
        : BaseContext(n, taskIdx) {}

public:
    // Constructor for derived typed contexts
    template<typename NodeT>
    ExecuteContext(NodeT* n, uint32_t taskIdx)
        : BaseContext(n, taskIdx) {}
};

/**
 * @brief Context for Cleanup phase
 *
 * Cleanup cannot access inputs/outputs - resources being destroyed.
 */
struct CleanupContext : public BaseContext {
    CleanupContext(NodeInstance* n, uint32_t taskIdx)
        : BaseContext(n, taskIdx) {}  // No tasks in Cleanup

public:
    // Constructor for derived typed contexts
    template<typename NodeT>
    CleanupContext(NodeT* n, uint32_t taskIdx)
        : BaseContext(n, taskIdx) {}
};

} // namespace Vixen::RenderGraph
