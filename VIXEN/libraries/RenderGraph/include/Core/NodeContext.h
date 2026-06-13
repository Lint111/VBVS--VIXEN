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
 * @brief Why a node is being cleaned up.
 *
 * Lets a node distinguish a transient **recompile** (e.g. swapchain recreation — release per-recompile
 * resources but KEEP persistent ones, like the OS window) from **final teardown** (release everything).
 * Most nodes ignore it; only nodes owning persistent-across-recompile resources need to branch on it.
 */
enum class CleanupReason {
    Recompile,      // RecompileDirtyNodes: this node is dirty and will be Setup+Compiled again
    DeviceLost,     // RecoverFromDeviceLoss: the GPU device was lost. Release ALL device-child resources
                    // (like Recompile — every node already does this); persistent NON-device resources
                    // (the OS window + surface) still survive (reason != FinalTeardown). Any vkDeviceWaitIdle
                    // / fence wait in CleanupImpl is harmless: on a lost device it returns immediately.
    FinalTeardown   // ExecuteCleanup: the whole graph is shutting down
};

/**
 * @brief Context for Cleanup phase
 *
 * Cleanup cannot access inputs/outputs - resources being destroyed.
 */
struct CleanupContext : public BaseContext {
    // Why this cleanup is happening. Set by NodeInstance::CleanupImpl() from the Cleanup(reason) call;
    // defaults to FinalTeardown so destructor/CleanupStack paths (which pass no reason) are safe.
    CleanupReason reason = CleanupReason::FinalTeardown;

    CleanupContext(NodeInstance* n, uint32_t taskIdx)
        : BaseContext(n, taskIdx) {}  // No tasks in Cleanup

public:
    // Constructor for derived typed contexts
    template<typename NodeT>
    CleanupContext(NodeT* n, uint32_t taskIdx)
        : BaseContext(n, taskIdx) {}
};

/**
 * @brief Macro to generate context bridge methods
 *
 * Generates bridge methods that override base class lifecycle methods
 * and forward calls to derived class typed methods.
 *
 * Usage: Place in derived class that introduces new context types
 *
 * @param BaseCtx Base context type (e.g., SetupContext, CompileContext)
 * @param DerivedCtx Derived context type (e.g., TypedSetupContext, VariadicCompileContext)
 * @param Phase Lifecycle phase name (Setup, Compile, Execute, Cleanup)
 */
#define GENERATE_CONTEXT_BRIDGE(BaseCtx, DerivedCtx, Phase) \
    void Phase##Impl(BaseCtx& ctx) override { \
        Phase##Impl(static_cast<DerivedCtx&>(ctx)); \
    }

/**
 * @brief Macro to generate all four context bridge methods
 *
 * @param SetupCtx Derived setup context type
 * @param CompileCtx Derived compile context type
 * @param ExecuteCtx Derived execute context type
 * @param CleanupCtx Derived cleanup context type
 */
#define GENERATE_ALL_CONTEXT_BRIDGES(SetupCtx, CompileCtx, ExecuteCtx, CleanupCtx) \
    GENERATE_CONTEXT_BRIDGE(Vixen::RenderGraph::SetupContext, SetupCtx, Setup) \
    GENERATE_CONTEXT_BRIDGE(Vixen::RenderGraph::CompileContext, CompileCtx, Compile) \
    GENERATE_CONTEXT_BRIDGE(Vixen::RenderGraph::ExecuteContext, ExecuteCtx, Execute) \
    GENERATE_CONTEXT_BRIDGE(Vixen::RenderGraph::CleanupContext, CleanupCtx, Cleanup)

} // namespace Vixen::RenderGraph
