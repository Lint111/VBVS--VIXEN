#pragma once

namespace Vixen::RenderGraph {

/**
 * @brief Interface for nodes that need setup during graph compilation
 *
 * Nodes implementing this interface will have GraphCompileSetup() called
 * during RenderGraph::Prepare(), before deferred connections are processed.
 * This allows nodes to inspect their connected inputs and dynamically configure
 * their outputs or internal state before the graph is fully compiled.
 *
 * Use cases:
 * - Variadic nodes discovering dynamic slot counts from shader metadata
 * - Struct unpacker nodes discovering member outputs from input type
 * - Any node that needs to inspect connected inputs to configure outputs
 *
 * Execution order:
 * 1. Graph construction (AddNode, Connect)
 * 2. Prepare() → TopologicalSort()
 * 3. Prepare() → GraphCompileSetup() ← THIS INTERFACE
 * 4. Prepare() → ProcessDeferredConnections() (ConnectVariadic, etc.)
 * 5. Prepare() → CompileImpl() (validate, allocate resources)
 * 6. Execute loop → ExecuteImpl()
 *
 * Note: GraphCompileSetup is single-threaded and happens at graph-compile-time.
 * For multi-bundle parallel work, nodes call their own SetupImpl/CompileImpl
 * with Context per-bundle during the Compile phase.
 */
class IGraphCompilable {
public:
    virtual ~IGraphCompilable() = default;

    /**
     * @brief Called during graph compilation, before deferred connections
     *
     * This runs after basic connections are established but before
     * deferred connections (like ConnectVariadic, ConnectMember) are processed.
     *
     * Single-threaded execution - no Context needed since bundles aren't
     * being executed yet. Nodes can access their connected resources directly
     * via GetInput() or create contexts internally if needed for setup logic.
     *
     * Typical uses:
     * - Inspect input types and register dynamic output slots
     * - Read shader metadata and create variadic input slots
     * - Validate that required inputs are connected
     * - Prepare metadata for deferred connection resolution
     *
     * Example:
     * @code
     * void MyNode::GraphCompileSetup() {
     *     // Create context for bundle 0 to read inputs
     *     Context ctx{this, 0};
     *
     *     // Read shader metadata input
     *     auto shaderBundle = ctx.In(MyConfig::SHADER_BUNDLE);
     *
     *     // Discover and register dynamic slots
     *     for (const auto& descriptor : shaderBundle->descriptors) {
     *         RegisterVariadicSlot(descriptor.binding, descriptor.type);
     *     }
     * }
     * @endcode
     */
    virtual void GraphCompileSetup() = 0;
};

} // namespace Vixen::RenderGraph
