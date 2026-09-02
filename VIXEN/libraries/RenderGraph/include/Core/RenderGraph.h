#pragma once

#include "Headers.h"

#include <cassert>

#include "Core/NodeInstance.h"
#include "Core/NodeTypeRegistry.h"
#include "Core/GraphTopology.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "CleanupStack.h"
#include "Core/ResourceDependencyTracker.h"
#include "Lifetime/DeferredDestruction.h"
#include "Lifetime/LifetimeScope.h"
#include "Memory/DeviceBudgetManager.h"
#include "EventTypes/RenderGraphEvents.h"
#include "MessageBus.h"
#include "Message.h"
#include "Time/EngineTime.h"
#include "MainCacher.h"
#include "Core/LoopManager.h"
#include "Core/GraphLifecycleHooks.h"
#include "Core/TaskProfileRegistry.h"
#include "Core/CalibrationStore.h"
#include "Core/TimelineCapacityTracker.h"
#include "Core/ResourceAccessTracker.h"  // Sprint 6.4: Conflict detection
#include "Core/FrameSyncScheduler.h"     // Auto-sync P2: frame sync schedule
#include "Core/GraphTaskLowering.h"      // Tier-B graph -> shared Tier-A task DAG
#include "Core/FailScenario.h"                  // Inc 1: self-neutralizing when VIXEN_FAIL_SCENARIOS is off
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <stop_token>
#include <typeindex>

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

class GraphScope;
struct SubGraphHandle;
struct SubGraphExpansionContext;
template<typename Derived>
class SubGraphType;

// Import types from ResourceManagement namespace
using ResourceManagement::DeferredDestructionQueue;
using ResourceManagement::LifetimeScopeManager;
using ResourceManagement::ResourceBudgetManager;
using ResourceManagement::DeviceBudgetManager;

// NodeHandle defined in CleanupStack.h (included transitively)

/**
 * @brief Main Render Graph class
 *
 * Orchestrates the entire render graph system:
 * - Graph construction
 * - Compilation and optimization
 * - Resource management
 * - Execution
 *
 * **THREAD SAFETY**: RenderGraph is **NOT thread-safe**.
 *
 * **Threading Model**:
 * - All RenderGraph methods must be called from the **same thread** (main thread)
 * - Graph construction (AddNode, ConnectNodes) must complete before execution begins
 * - Execution (RenderFrame, Execute) must not be called concurrently with graph modification
 * - LoopManager loops execute **sequentially**, not in parallel (single-threaded execution)
 *
 * **Rationale**:
 * - Vulkan command buffer recording is single-threaded per command buffer
 * - Node state transitions (Compile → Execute → Cleanup) are not atomic
 * - Resource lifetime management assumes single-threaded ownership
 * - EventBus message processing occurs sequentially during RenderFrame()
 *
 * **Future Work**:
 * - Multi-threaded execution could be added via wave-based parallel dispatch (Phase D)
 * - Requires dependency-based scheduling and per-node synchronization
 * - Current design prioritizes simplicity and correctness over parallelism
 *
 * **Best Practices**:
 * 1. Construct graph during initialization (single-threaded)
 * 2. Call RenderFrame() from main thread only
 * 3. Do NOT modify graph structure during execution (AddNode/ConnectNodes forbidden after first RenderFrame)
 * 4. Event handlers triggered during execution run synchronously on main thread
 */
class RenderGraph {
public:
    /**
     * @brief Construct a new Render Graph
     * @param registry The node type registry
     * @param messageBus Event bus for graph events (optional)
     * @param mainLogger Optional logger for debug output (in debug builds)
     * @param mainCacher Main cache system (required; injected by EngineContext — AR#8 removed the
     *        process-wide MainCacher::Instance() fallback so multiple engines don't share caches)
     */
    explicit RenderGraph(
        NodeTypeRegistry* registry,
        EventBus::MessageBus* messageBus = nullptr,
        Logger* mainLogger = nullptr,
        CashSystem::MainCacher* mainCacher = nullptr  // de facto required; see GetMainCacher()
    );

    ~RenderGraph();

    // Prevent copying
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // ====== Graph Building ======

    /**
     * @brief Add a node to the graph using C++ type (preferred - zero strings)
     * @tparam TNodeType The NodeType-derived class (e.g., WindowNodeType)
     * @param instanceName Unique name for this instance
     * @return Handle to the created node
     */
    template<typename TNodeType>
    NodeHandle AddNode(const std::string& instanceName) {
        static_assert(std::is_base_of_v<NodeType, TNodeType>, "TNodeType must derive from NodeType");
        TNodeType* nodeType = typeRegistry->Get<TNodeType>();
        if (!nodeType) {
            throw std::runtime_error("Node type not registered: " + std::string(typeid(TNodeType).name()));
        }
        return AddNodeImpl(nodeType, instanceName);
    }

    /**
     * @brief Expand a C++-declared sub-graph into this graph immediately.
     *
     * The returned handle is a boundary view over the already-expanded member nodes;
     * it is not a runtime node or an inner graph.
     */
    template<typename TSubGraph, typename Params>
    SubGraphHandle Instantiate(const std::string& instanceName, const Params& params);

    template<typename TSubGraph>
    SubGraphHandle Instantiate(const std::string& instanceName);

    /** @brief Connect a node output to every member slot bound to a group input port. */
    template<typename SourceSlot, typename Port>
    void Connect(NodeHandle source, SourceSlot sourceSlot,
                 const SubGraphHandle& target, Port targetPort);

    /** @brief Connect a group output port to a node input. */
    template<typename Port, typename TargetSlot>
    void Connect(const SubGraphHandle& source, Port sourcePort,
                 NodeHandle target, TargetSlot targetSlot);

    /** @brief Connect a group output port directly to another group input port. */
    template<typename SourcePort, typename TargetPort>
    void Connect(const SubGraphHandle& source, SourcePort sourcePort,
                 const SubGraphHandle& target, TargetPort targetPort);

    /**
     * @brief Add a node to the graph (legacy string-based API)
     * @param typeName The name of the node type
     * @param instanceName Unique name for this instance
     * @return Handle to the created node
     */
    NodeHandle AddNode(const std::string& typeName, const std::string& instanceName);

    /**
     * @brief Add a node using type ID (legacy ID-based API)
     */
    NodeHandle AddNode(NodeTypeId typeId, const std::string& instanceName);

    /**
     * @brief Connect two nodes (resource dependency)
     * @param from Source node handle
     * @param outputIdx Output index from source
     * @param to Target node handle
     * @param inputIdx Input index to target
     */
    void ConnectNodes(
        NodeHandle from, 
        uint32_t outputIdx,
        NodeHandle to, 
        uint32_t inputIdx
    );

    /**
     * @brief Remove a node from the graph
     */
    void RemoveNode(NodeHandle handle);

    /**
     * @brief Clear the entire graph
     */
    void Clear();

    // ====== Compilation ======

    /**
     * @brief Compile the graph
     *
     * Performs:
     * - Dependency analysis
     * - Resource allocation
     * - Pipeline creation
     * - Command buffer generation
     */
    void Compile();

    /**
     * @brief Register a callback to be executed after each node compiles
     *
     * Callbacks are invoked during Compile() after each node's Compile() method succeeds.
     * Use this for field extraction or other operations that need compiled node outputs.
     *
     * @param callback Function taking the just-compiled NodeInstance
     */
    using PostNodeCompileCallback = std::function<void(NodeInstance*)>;
    void RegisterPostNodeCompileCallback(PostNodeCompileCallback callback);

    /**
     * @brief Register a callback to be invoked after a specific node executes, EVERY frame
     *
     * Fires in RenderFrame()'s sequential execution loop, immediately after the named node's
     * Execute() returns and before the NEXT node in topological order runs. Use this to read/copy
     * a resource a node produced while it's still in the state that node left it in -- e.g.
     * capturing the swapchain image between a compute/render node writing to it and PresentNode
     * releasing it back to the swapchain (vkQueuePresentKHR makes it "not acquired"; reading it
     * after RenderFrame() returns touches an unowned presentable image, which is invalid per spec
     * regardless of synchronization -- see FrameCapture's registration in BenchmarkRunner for the
     * motivating case).
     *
     * Unlike RegisterPostNodeCompileCallback (fires once per node, at Compile() time), this fires
     * every frame the named node executes -- keep registered callbacks cheap or internally gated
     * (a "should I actually capture this frame" check), since they run on the render hot path.
     * No-op (never invoked) if no node with the given name exists at fire time.
     *
     * @param nodeInstanceName Exact NodeInstance::GetInstanceName() to fire after
     * @param callback Function taking the just-executed NodeInstance
     */
    using PostNodeExecuteCallback = std::function<void(NodeInstance*)>;
    void RegisterPostNodeExecuteCallback(const std::string& nodeInstanceName, PostNodeExecuteCallback callback);

    /**
     * @brief Check if graph is compiled
     */
    bool IsCompiled() const { return isCompiled; }

    // ====== Execution ======

    /**
     * @brief Render a complete frame
     *
     * High-level method that handles the full render loop:
     * - Acquires swapchain image
     * - Allocates/records command buffer
     * - Submits with semaphores
     * - Presents to swapchain
     *
     * @return VkResult from presentation (VK_SUCCESS, VK_ERROR_OUT_OF_DATE_KHR, etc.)
     */
    VkResult RenderFrame();

    // ====== Query ======

    /**
     * @brief Get a node instance by handle
     */
    NodeInstance* GetInstance(NodeHandle handle);
    const NodeInstance* GetInstance(NodeHandle handle) const;

    /**
     * @brief Get a node instance by name
     */
    NodeInstance* GetInstanceByName(const std::string& name);
    const NodeInstance* GetInstanceByName(const std::string& name) const;

    /**
     * @brief Get all instances of a specific type
     */
    std::vector<NodeInstance*> GetInstancesOfType(NodeTypeId typeId) const;

    /**
     * @brief Get all instances whose node type is the given C++ NodeType class.
     *
     * Type-safe discovery that avoids hard-coding instance names (FR-6): resolves the
     * type id from the registry, then returns the matching instances. Returns an empty
     * vector if the type was never registered with this graph.
     *
     * @tparam TNodeType A NodeType-derived class (e.g. WindowNodeType)
     */
    template<typename TNodeType>
    std::vector<NodeInstance*> GetInstancesOfType() const {
        static_assert(std::is_base_of_v<NodeType, TNodeType>, "TNodeType must derive from NodeType");
        TNodeType* nodeType = typeRegistry->Get<TNodeType>();
        if (!nodeType) {
            return {};
        }
        return GetInstancesOfType(nodeType->GetTypeId());
    }

    /**
     * @brief Get instance count of a specific type
     */
    uint32_t GetInstanceCount(NodeTypeId typeId) const;

    /**
     * @brief Get total node count
     */
    size_t GetNodeCount() const { return instances.size(); }

    /**
     * @brief Get node by name (for logger configuration)
     */
    NodeInstance* GetNodeByName(const std::string& name) const;

    /**
     * @brief Get execution order (after compilation)
     */
    const std::vector<NodeInstance*>& GetExecutionOrder() const { return executionOrder; }

    /**
     * @brief Get the graph topology
     */
    GraphTopology& GetTopology() { return topology; }
    const GraphTopology& GetTopology() const { return topology; }

    // ====== Cleanup Management ======

    /**
     * @brief Get the cleanup stack for registering cleanup callbacks
     * Nodes should register their cleanup during Compile()
     */
    CleanupStack& GetCleanupStack() { return cleanupStack; }

    /**
     * @brief Get the resource dependency tracker
     * Used internally to build automatic cleanup dependencies
     */
    ResourceDependencyTracker& GetDependencyTracker() { return dependencyTracker; }

    /**
     * @brief Helper: Returns the cleanup node name for the Device node (if present)
     *
     * Nodes that need to ensure they are cleaned before the logical device can
     * call this to obtain the correct dependency name instead of hard-coding
     * "DeviceNode_Cleanup". Falls back to the legacy name if no device node
     * instance is found.
     */
    std::string GetDeviceCleanupNodeName() const;

    /**
     * @brief Register an external cleanup callback with dependency on a graph node
     *
     * Allows external systems (e.g., BenchmarkRunner, FrameCapture) to register
     * cleanup callbacks that execute in dependency order with graph nodes.
     *
     * The callback will be executed BEFORE the dependency node's cleanup,
     * ensuring correct resource destruction order.
     *
     * @param dependencyNodeName Name of the node this cleanup depends on (e.g., "benchmark_device")
     * @param cleanupCallback Function to execute during graph cleanup
     * @param externalSystemName Identifier for debugging (e.g., "FrameCapture")
     *
     * @example
     * // In BenchmarkRunner: cleanup FrameCapture before DeviceNode
     * renderGraph->RegisterExternalCleanup(
     *     "benchmark_device",
     *     [this]() { frameCapture_->Cleanup(); },
     *     "FrameCapture"
     * );
     */
    void RegisterExternalCleanup(
        const std::string& dependencyNodeName,
        std::function<void()> cleanupCallback,
        const std::string& externalSystemName
    );

    // ====== Time Management ======

    /**
     * @brief Get the engine time
     * Provides delta time and elapsed time for frame-rate independent animations
     */
    Vixen::Core::EngineTime& GetTime() { return time; }
    const Vixen::Core::EngineTime& GetTime() const { return time; }

    /**
     * @brief Update the engine time
     * Should be called once per frame to maintain time-based animations
     */
    void UpdateTime() { time.Update(); }

    // ====== Loop Management (Phase 0.4) ======

    /**
     * @brief Register a new loop with the graph
     *
     * Creates a loop with the specified configuration. Returns a unique loop ID
     * that should be passed to LoopBridgeNode via LOOP_ID parameter.
     *
     * @param config Loop configuration (timestep, name, catch-up mode)
     * @return Unique loop ID for use with LoopBridgeNode
     */
    uint32_t RegisterLoop(const LoopConfig& config) {
        return loopManager.RegisterLoop(config);
    }

    /**
     * @brief Get the loop manager (for LoopBridgeNode access)
     *
     * LoopBridgeNodes access this directly via GetGraph()->GetLoopManager()
     * to publish loop state into the graph.
     *
     * @return Reference to graph-owned LoopManager
     */
    LoopManager& GetLoopManager() { return loopManager; }
    const LoopManager& GetLoopManager() const { return loopManager; }

    /**
     * @brief Get resource budget manager for task execution
     *
     * Returns nullptr if no budget manager has been configured.
     * Nodes use this via ExecuteTasks() for budget-aware parallelism.
     *
     * @return Pointer to ResourceBudgetManager, or nullptr
     */
    ResourceBudgetManager* GetBudgetManager() { return budgetManager.get(); }
    const ResourceBudgetManager* GetBudgetManager() const { return budgetManager.get(); }

    /**
     * @brief Set the device budget manager for GPU allocation tracking
     *
     * Application creates and configures DeviceBudgetManager, graph owns shared_ptr.
     * When set, budget manager is wired to MainCacher for tracked allocations.
     *
     * @param manager Shared pointer to DeviceBudgetManager
     */
    void SetDeviceBudgetManager(std::shared_ptr<DeviceBudgetManager> manager);

    /**
     * @brief Get device budget manager for GPU allocation tracking
     *
     * @return Pointer to DeviceBudgetManager, or nullptr if not configured
     */
    DeviceBudgetManager* GetDeviceBudgetManager() { return deviceBudgetManager_.get(); }
    const DeviceBudgetManager* GetDeviceBudgetManager() const { return deviceBudgetManager_.get(); }

    /**
     * @brief Process pending events from the message bus
     * 
     * Should be called once per frame, typically before RenderFrame().
     * Processes events that may mark nodes as needing recompilation.
     */
    void ProcessEvents();

    /**
     * @brief Recompile nodes that have been marked as dirty
     * 
     * Called after ProcessEvents() to handle cascade recompilation.
     * Only recompiles nodes that actually need it.
     */
    void RecompileDirtyNodes();

    // ====== Device-loss recovery (AR#1 Error-Model Phase 3) ======

    /**
     * @brief Latch that the GPU device was lost (VK_ERROR_DEVICE_LOST).
     *
     * Called by any node that observes VK_ERROR_DEVICE_LOST from a GPU call (submit / present /
     * acquire / fence wait). Idempotent — the first detection wins; subsequent calls only log.
     * Once latched, RenderFrame() short-circuits and returns VK_ERROR_DEVICE_LOST until recovery
     * clears the flag (Increment 2: RecoverFromDeviceLoss). The device and all its child objects
     * are invalid after this point; the graph must rebuild on a fresh device before rendering again.
     *
     * @param site Human-readable origin of the detection, e.g. "FrameSyncNode::vkWaitForFences".
     */
    void NotifyDeviceLost(const std::string& site);

    /** @brief True once a VK_ERROR_DEVICE_LOST has been latched and not yet recovered. */
    bool IsDeviceLost() const { return deviceLost_; }

    /**
     * @brief Abort the remainder of the current frame's node execution.
     *
     * Called mid-frame by a node that discovers the frame cannot proceed — canonically
     * SwapChainNode when acquire returns OUT_OF_DATE (window resize/maximize) and publishes the
     * IMAGE_INDEX = UINT32_MAX skip sentinel. The sequential execute loop stops before the next
     * node, so downstream consumers of per-image state (semaphore/command-buffer/descriptor
     * arrays indexed by image) are skipped wholesale instead of each needing its own sentinel
     * guard — six of ten consumers historically got that guard wrong (missing or after first
     * use). Per-node guards remain as second-layer defense. Cleared at the top of RenderFrame().
     */
    void AbortCurrentFrame();

    /** @brief True while the current frame's execution has been aborted (see AbortCurrentFrame). */
    bool IsFrameAborted() const { return frameAborted_; }

    /** @brief Cancellation token for the graph-owned current execution epoch. */
    std::stop_token GetExecutionStopToken() const { return executionStopSource_.get_token(); }

    /** @brief Monotonic identity of the current graph execution epoch. */
    uint64_t GetExecutionEpoch() const {
        return executionEpoch_.load(std::memory_order_acquire);
    }

    /**
     * @brief Rebuild the whole graph on a fresh GPU device after a latched device loss.
     *
     * Ordering-correct full rebuild — the dirty-recompile cascade canNOT be reused for this: it
     * processes nodes in execution order, so it would recreate the device (DeviceNode is first) BEFORE
     * the downstream nodes tear down their old buffers/images, destroying children of a dead device.
     * Instead this does a strict two-pass:
     *   1. Drain (WaitForGraphDevicesIdle — returns immediately on a truly-lost device) then TEARDOWN
     *      every node in REVERSE execution order with reason = DeviceLost (children before the device;
     *      WindowNode keeps the window+surface; DeviceNode, last, destroys the old VkDevice).
     *   2. REBUILD (Setup + Compile) every node in FORWARD execution order — DeviceNode first creates the
     *      new VulkanDevice and republishes it; every downstream node re-reads the new VulkanDevice* from
     *      its input (verified: all nodes acquire the device via ctx.In on Setup/Compile) and recreates.
     * Single attempt: on success the device-lost latch clears and rendering resumes; if the rebuild
     * throws (the device is genuinely gone), latches an unrecoverable terminal state and returns false so
     * the host aborts gracefully — no infinite recovery spin.
     *
     * Lives in the graph (the host just drives the tick, as it does RecompileDirtyNodes); the host can
     * call this when RenderFrame() returns VK_ERROR_DEVICE_LOST.
     *
     * @return true if the graph was rebuilt and rendering can resume; false if the loss is unrecoverable.
     */
    bool RecoverFromDeviceLoss();

    /**
     * @brief Get the message bus (for nodes to publish events)
     */
    EventBus::MessageBus* GetMessageBus() const { return messageBus; }

    /**
     * @brief Pre-allocate EventBus queue based on graph complexity
     *
     * Called automatically during Compile() using heuristic: nodeCount × 3.
     * Can also be called manually after adding nodes for explicit control.
     *
     * @param eventsPerNode Heuristic multiplier (default: 3 events per node)
     */
    void PreAllocateEventBus(size_t eventsPerNode = 3);

    /**
     * @brief Get the main cacher instance (for nodes to register and access caches)
     *
     * Nodes can use this to register cachers during Setup/Compile and access them.
     * Registration is idempotent - multiple nodes can call RegisterCacher for the same type.
     */
    CashSystem::MainCacher& GetMainCacher() {
        // AR#8: no MainCacher::Instance() fallback. The cacher is injected by EngineContext (which
        // owns one when the host supplies none), so two engines never share process-wide caches.
        assert(mainCacher && "RenderGraph has no MainCacher — construct it via EngineContext or pass one to the ctor");
        return *mainCacher;
    }

    /**
     * @brief Get the deferred destruction queue
     *
     * For zero-stutter hot-reload: instead of blocking with vkDeviceWaitIdle(),
     * nodes can queue resources for destruction after N frames have passed.
     *
     * Example (in PipelineNode::HandleCompilationResult):
     * ```cpp
     * auto* queue = renderGraph->GetDeferredDestructionQueue();
     * queue->Add(device, oldPipeline, currentFrame, vkDestroyPipeline);
     * ```
     */
    DeferredDestructionQueue* GetDeferredDestructionQueue() { return &deferredDestruction; }
    const DeferredDestructionQueue* GetDeferredDestructionQueue() const { return &deferredDestruction; }

    // ====== Lifetime Scope Management (Sprint 4 Phase B) ======

    /**
     * @brief Set the lifetime scope manager for per-frame resource management
     *
     * When set, the RenderGraph will call BeginFrame()/EndFrame() on the manager
     * during RenderFrame(), enabling automatic per-frame resource cleanup.
     *
     * @param manager Pointer to LifetimeScopeManager (must outlive RenderGraph)
     *
     * Example:
     * ```cpp
     * // Application setup
     * SharedResourceFactory factory(&allocator, &queue, &frameCounter);
     * LifetimeScopeManager scopeManager(&factory);
     * renderGraph->SetLifetimeScopeManager(&scopeManager);
     *
     * // In render loop - automatic BeginFrame/EndFrame
     * renderGraph->RenderFrame();  // Scope management happens internally
     * ```
     */
    void SetLifetimeScopeManager(LifetimeScopeManager* manager) { scopeManager_ = manager; }

    /**
     * @brief Get the current lifetime scope manager
     * @return Pointer to LifetimeScopeManager, or nullptr if not set
     */
    LifetimeScopeManager* GetLifetimeScopeManager() { return scopeManager_; }
    const LifetimeScopeManager* GetLifetimeScopeManager() const { return scopeManager_; }

    /**
     * @brief Get the current frame index
     *
     * Useful for frame-based resource tracking and deferred destruction.
     */
    uint64_t GetCurrentFrameIndex() const { return globalFrameIndex; }

    /**
     * @brief Mark a node as needing recompilation
     * 
     * Called by NodeInstance when it receives an invalidation event.
     * The node will be recompiled during the next RecompileDirtyNodes() call.
     */
    void MarkNodeNeedsRecompile(NodeHandle nodeHandle);

    /**
     * @brief Execute all cleanup callbacks in dependency order
     * Called during graph destruction or manual cleanup
     */
    void ExecuteCleanup();

    /**
     * @brief Execute partial cleanup starting from a specific node
     * 
     * Recursively cleans the node and its dependencies (moving backwards
     * toward producers). Only cleans dependencies if no other nodes use them
     * (reference count becomes zero).
     * 
     * @param rootNodeName Name of the node to start cleanup from
     * @return Number of nodes cleaned
     */
    size_t CleanupSubgraph(const std::string& rootNodeName);

    /**
     * @brief Cleanup nodes matching a tag
     * @param tag Tag to match (e.g., "shadow-maps")
     * @return Number of nodes cleaned
     */
    size_t CleanupByTag(const std::string& tag);

    /**
     * @brief Cleanup all nodes of a specific type
     * @param typeName Node type name (e.g., "GeometryPass")
     * @return Number of nodes cleaned
     */
    size_t CleanupByType(const std::string& typeName);

    /**
     * @brief Preview which nodes would be cleaned (dry-run)
     * 
     * @param rootNodeName Name of the node to analyze
     * @return Vector of node names that would be cleaned
     */
    std::vector<std::string> GetCleanupScope(const std::string& rootNodeName) const;

    // ====== Validation ======

    /**
     * @brief Validate the graph
     * @param errorMessage Output error message if validation fails
     * @return true if valid, false otherwise
     */
    bool Validate(std::string& errorMessage) const;

    // ====== Lifecycle Hooks ======

    /**
     * @brief Get the lifecycle hooks manager
     */
    GraphLifecycleHooks& GetLifecycleHooks() { return lifecycleHooks; }
    const GraphLifecycleHooks& GetLifecycleHooks() const { return lifecycleHooks; }

    // ====== Task Profile System (Sprint 6.3) ======

    /**
     * @brief Get the task profile registry
     *
     * Nodes use this to register profiles and get cost estimates.
     * The registry persists calibration data across sessions.
     *
     * @code
     * // In node Setup:
     * auto& registry = GetOwningGraph()->GetTaskProfileRegistry();
     * auto profile = std::make_unique<SimpleTaskProfile>("myTask", "compute");
     * registry.RegisterTask(std::move(profile));
     *
     * // In node Execute:
     * auto* profile = registry.GetProfile("myTask");
     * uint64_t estimatedCost = profile->GetEstimatedCostNs();
     * @endcode
     */
    TaskProfileRegistry& GetTaskProfileRegistry() { return taskProfileRegistry_; }
    const TaskProfileRegistry& GetTaskProfileRegistry() const { return taskProfileRegistry_; }

    /**
     * @brief Register a task profile factory
     *
     * Convenience wrapper - factories must be registered before LoadCalibration().
     *
     * @param typeName Profile type name (e.g., "SimpleTaskProfile")
     * @param factory Factory function
     */
    void RegisterTaskProfileFactory(const std::string& typeName, TaskProfileFactory factory) {
        taskProfileRegistry_.RegisterFactory(typeName, std::move(factory));
    }

    /**
     * @brief Load calibration data from file
     *
     * Call after registering factories but before first RenderFrame().
     *
     * @param baseDir Directory containing calibration files
     * @param gpu GPU identifier for file selection
     * @return Number of profiles loaded
     */
    size_t LoadCalibration(const std::filesystem::path& baseDir, const GPUIdentifier& gpu) {
        calibrationStore_ = std::make_unique<CalibrationStore>(baseDir);
        calibrationStore_->SetGPU(gpu);
        auto result = calibrationStore_->Load(taskProfileRegistry_);
        return result.profileCount;
    }

    /**
     * @brief Save calibration data to file
     *
     * Call periodically or at application shutdown.
     *
     * @return true if save succeeded
     */
    bool SaveCalibration() {
        if (!calibrationStore_) return false;
        auto result = calibrationStore_->Save(taskProfileRegistry_);
        return result.success;
    }

    // ====== Capacity Tracking System (Sprint 6.3 Phase 4) ======

    /**
     * @brief Get the capacity tracker
     *
     * Provides real-time frame budget tracking and utilization metrics.
     * Nodes record measurements; the system adjusts task profiles automatically.
     *
     * @return Reference to TimelineCapacityTracker
     */
    TimelineCapacityTracker& GetCapacityTracker() { return capacityTracker_; }
    const TimelineCapacityTracker& GetCapacityTracker() const { return capacityTracker_; }

    /**
     * @brief Configure capacity tracking
     *
     * @param config Tracker configuration (budgets, thresholds)
     */
    void ConfigureCapacityTracking(const TimelineCapacityTracker::Config& config) {
        capacityTracker_ = TimelineCapacityTracker(config);
    }

    /**
     * @brief Enable automatic pressure adjustment (event-driven)
     *
     * When enabled, the system automatically adjusts TaskProfile workUnits
     * based on capacity utilization after each frame via events:
     * - TimelineCapacityTracker publishes BudgetOverrun/AvailableEvent
     * - TaskProfileRegistry subscribes and adjusts pressure autonomously
     *
     * This is the event-driven implementation (Sprint 6.3 Option A).
     * RenderGraph no longer mediates between these systems.
     *
     * @param enable true to enable automatic adjustment
     */
    void SetAutoPressureAdjustment(bool enable);

    /**
     * @brief Check if auto pressure adjustment is enabled
     */
    [[nodiscard]] bool IsAutoPressureAdjustmentEnabled() const {
        return autoPressureAdjustment_;
    }

    /**
     * @brief Wire up event-driven subsystem subscriptions
     *
     * Called automatically when SetAutoPressureAdjustment(true) is called.
     * Can also be called manually after MessageBus is set.
     *
     * Sets up:
     * - TimelineCapacityTracker: subscribes to FrameStart/End, publishes Budget events
     * - TaskProfileRegistry: subscribes to Budget events for pressure adjustment
     */
    void InitializeEventDrivenSystems();

    /**
     * @brief Get the resource access tracker (for debugging/analysis)
     */
    [[nodiscard]] const ResourceAccessTracker& GetResourceAccessTracker() const {
        return resourceAccessTracker_;
    }

    /**
     * @brief Get the frame sync schedule (auto-sync P2)
     *
     * Returns the baked FrameSyncSchedule produced during Compile().
     * Not yet consumed at Execute-time (P3); exposed for inspection and tests.
     */
    [[nodiscard]] const FrameSyncSchedule& GetFrameSyncSchedule() const {
        return frameSyncScheduler_.GetSchedule();
    }

    /** @brief Compiled node-level task DAG and waves emitted during Compile(). */
    [[nodiscard]] const GraphTaskPlan& GetExecutionTaskPlan() const {
        return executionTaskPlan_;
    }

    // ====== Resource Dependency Tracking ======

    /**
     * @brief Register a resource producer for recompile dependency tracking
     *
     * This is used by variadic connections with field extraction to register
     * dynamically-populated resources after PostSetup hooks execute.
     *
     * @param resource The resource being produced
     * @param producer The node that produces this resource
     * @param outputIndex The output slot index on the producer node
     */
    void RegisterResourceProducer(Resource* resource, NodeInstance* producer, size_t outputIndex);


private:
    friend class GraphScope;

    template<typename TSubGraph, typename Params>
    SubGraphHandle InstantiateImpl(
        const std::string& instanceName,
        const Params& params,
        const std::shared_ptr<struct SubGraphExpansionContext>& expansion);

    // Internal implementation for AddNode (used by both template and non-template versions)
    NodeHandle AddNodeImpl(NodeType* nodeType, const std::string& instanceName);

    // Core components
    NodeTypeRegistry* typeRegistry;
    EventBus::MessageBus* messageBus = nullptr;  // Non-owning pointer
    CashSystem::MainCacher* mainCacher = nullptr;  // Non-owning pointer
    EventBus::ScopedSubscriptions subscriptions_;  // RAII subscriptions (auto-unsubscribe on destruction)
    // Vixen::Vulkan::Resources::VulkanDevice* primaryDevice;  // Removed - nodes access device directly

    // Logger (non-owning pointer — application owns the logger)
    Logger* mainLogger = nullptr;

    // Graph data
    std::vector<std::unique_ptr<NodeInstance>> instances;
    std::map<std::string, NodeHandle> nameToHandle;
    std::vector<PostNodeCompileCallback> postNodeCompileCallbacks;  // Callbacks executed after each node compiles
    // Callbacks executed every frame, right after the named node's Execute() returns (see
    // RegisterPostNodeExecuteCallback). Keyed by node instance name, not NodeHandle, so callers
    // can register before AddNode() runs (matches RegisterPostNodeCompileCallback's no-ordering-
    // requirement contract) -- looked up by name in the hot RenderFrame() execution loop.
    std::unordered_multimap<std::string, PostNodeExecuteCallback> postNodeExecuteCallbacks;
    std::map<NodeTypeId, std::vector<NodeInstance*>> instancesByType;
    
    // Resources (lifetime management only - nodes are the logical containers)
    // This vector owns all Resource objects created by the graph. Nodes hold raw
    // pointers to these resources via their inputs/outputs vectors. This centralized
    // ownership enables future optimizations like memory aliasing and resource pooling.
    std::vector<std::unique_ptr<Resource>> resources;

    // Topology
    GraphTopology topology;

    // Execution
    std::vector<NodeInstance*> executionOrder;
    GraphTaskPlan executionTaskPlan_;
    bool isCompiled = false;
    // AR#16: set by ExecuteCleanup (shutdown). RenderFrame() checks this so it never executes a node
    // against destroyed resources (the render loop can iterate once more after WindowCloseEvent).
    bool isCleanedUp = false;

    // Frame abort (see AbortCurrentFrame): set mid-frame by SwapChainNode's out-of-date skip path,
    // checked by the sequential execute loop, cleared at the top of every RenderFrame().
    bool frameAborted_ = false;
    std::stop_source executionStopSource_;
    std::atomic<uint64_t> executionEpoch_{0};

    void BeginExecutionEpoch();
    void InvalidateExecutionEpoch();

    // Device-loss recovery (AR#1 Error-Model Phase 3). Latched by NotifyDeviceLost() when a node sees
    // VK_ERROR_DEVICE_LOST; checked by RenderFrame() which then returns VK_ERROR_DEVICE_LOST distinctly
    // (vs Phase 2a's generic VK_ERROR_UNKNOWN). RecoverFromDeviceLoss() rebuilds + clears it.
    bool deviceLost_ = false;
    // Set when a device-loss rebuild attempt fails (the GPU is genuinely gone). Terminal: RenderFrame()
    // keeps returning VK_ERROR_DEVICE_LOST and RecoverFromDeviceLoss() refuses to retry, so the host
    // aborts instead of spinning on an unrecoverable device.
    bool deviceLostUnrecoverable_ = false;
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
    // Fault-injection test hook (VIXEN_SIMULATE_DEVICE_LOSS=<render-frame>). -2 = env not yet parsed,
    // -1 = disabled, >=0 = latch a synthetic device loss once at that globalFrameIndex. The teardown +
    // rebuild are valid on a healthy device too (vkDestroy*/recreate don't require a truly-lost device),
    // so this faithfully exercises RecoverFromDeviceLoss() in a live run. Scenario-build-only state.
    int simulateDeviceLossFrame_ = -2;
    bool deviceLossSimulated_ = false;
#endif

#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
public:
    // Fail-scenario fault injection (test builds only): dormant unless a scenario arms it.
    FailScenario::FaultInjector* GetFaultInjector() {
        if (!faultInjector_) faultInjector_ = std::make_unique<FailScenario::FaultInjector>();
        return faultInjector_.get();
    }
private:
    std::unique_ptr<FailScenario::FaultInjector> faultInjector_;
#endif

    // Event-driven recompilation
    std::set<NodeHandle> dirtyNodes;
    bool renderPaused = false;
    // True when the current pause is a SwapChainRecreation pause. In that case the recompile is the
    // recreation, so RecompileDirtyNodes must NOT defer on renderPaused (deferring would leave the
    // swapchain un-recreated and the matching PAUSE_END never sent — a permanent pause on resize).
    bool pausedForRecreation_ = false;

    // Cleanup management
    CleanupStack cleanupStack;
    ResourceDependencyTracker dependencyTracker;
    std::unordered_map<NodeInstance*, size_t> dependentCounts;  // Reference counting for partial cleanup
    DeferredDestructionQueue deferredDestruction;  // Zero-stutter hot-reload

    // Handle-index generator for RegisterExternalCleanup(). Per-graph (was a function-local
    // `static uint32_t`, which was shared process-wide across every RenderGraph instance and
    // unguarded against concurrent callers — audit V-N10). Starts at max/2 to avoid colliding
    // with real graph-node handle indices.
    std::atomic<uint32_t> externalCleanupCounter_{0x80000000};

    // Time management
    Vixen::Core::EngineTime time;

    // Phase 0.4: Loop management
    LoopManager loopManager;
    Timer frameTimer;
    uint64_t globalFrameIndex = 0;

    // Phase F: Resource budget manager (optional)
    std::unique_ptr<ResourceBudgetManager> budgetManager;

    // Sprint 4 Phase D: Device budget manager for GPU allocations (optional, externally provided)
    std::shared_ptr<DeviceBudgetManager> deviceBudgetManager_;

    // Lifecycle hook system
    GraphLifecycleHooks lifecycleHooks;

    // Sprint 6.3: Task profile system for calibrated cost estimation
    TaskProfileRegistry taskProfileRegistry_;
    std::unique_ptr<CalibrationStore> calibrationStore_;

    // Sprint 6.3 Phase 4: Capacity tracking with automatic pressure adjustment
    TimelineCapacityTracker capacityTracker_;
    bool autoPressureAdjustment_ = false;

    // Sprint 6.4: Node-level resource conflict analysis and frame synchronization
    ResourceAccessTracker resourceAccessTracker_;  // Node-level conflict detection
    FrameSyncScheduler frameSyncScheduler_;         // Auto-sync P2: per-frame sync schedule

    // Sprint 4 Phase B: Lifetime scope management (optional, externally provided)
    LifetimeScopeManager* scopeManager_ = nullptr;

    // Compilation phases
    void AnalyzeDependencies();
    void AllocateResources();
    void GeneratePipelines();
    void BuildExecutionOrder();
    void BuildExecutionTaskPlan();
    VkResult ExecuteLoweredFrame();
    [[nodiscard]] bool UseLoweredGraph() const;
    [[nodiscard]] int GraphWorkerCount() const;
    void ComputeDependentCounts();
    void RecursiveCleanup(NodeInstance* node, std::set<NodeInstance*>& cleaned);

    // Pre-allocation (Sprint 5.5)
    void PreAllocateResources();

    // Event handling
    void HandleRenderPause(const EventTypes::RenderPauseEvent& msg);
    void HandleWindowResize(const EventTypes::WindowResizedMessage& msg);
    void HandleWindowStateChange(const EventBus::WindowStateChangeEvent& msg);
    void HandleWindowClose();
    void HandleCleanupRequest(const EventTypes::CleanupRequestedMessage& msg);
    void HandleDeviceSyncRequest(const EventTypes::DeviceSyncRequestedMessage& msg);

    // Helpers
    NodeHandle CreateHandle(uint32_t index) const;
    NodeInstance* GetInstanceInternal(NodeHandle handle);
    Resource* CreateResourceForOutput(NodeInstance* node, uint32_t outputIndex);
    // Wait for devices referenced by graph instances to be idle.
    // If `instancesToCheck` is empty, waits for all devices referenced by the graph.
    // Otherwise waits only for devices referenced by the provided instances.
    void WaitForGraphDevicesIdle(const std::vector<NodeInstance*>& instancesToCheck = {});
    // Wait for the provided set of VkDevice handles to be idle
    void WaitForDevicesIdle(const std::unordered_set<VkDevice>& devices);
};

// ============================================================================
// Sub-graph composition (I1/I2)
// ============================================================================

/** @internal Expansion state shared by nested sub-graph instantiations. */
struct SubGraphExpansionContext {
    std::vector<std::type_index> typeStack;
    std::vector<std::string> nameStack;
};

struct SubGraphBinding {
    uint32_t port = 0;
    NodeHandle member;
    uint32_t memberSlot = 0;
};

struct SubGraphState {
    RenderGraph* graph = nullptr;
    std::string name;
    std::vector<NodeHandle> members;
    std::vector<SubGraphBinding> inputBindings;
    std::vector<SubGraphBinding> outputBindings;
    size_t inputCount = 0;
    size_t outputCount = 0;
};

/**
 * @brief Handle to an eagerly-expanded sub-graph boundary.
 *
 * This handle owns no executable graph. It keeps the expanded member set and the
 * resolved boundary aliases so later connections can be lowered to direct edges.
 */
struct SubGraphHandle {
    bool IsValid() const { return state_ != nullptr; }
    const std::string& GetName() const {
        static const std::string empty;
        return state_ ? state_->name : empty;
    }
    const std::vector<NodeHandle>& GetMembers() const {
        static const std::vector<NodeHandle> empty;
        return state_ ? state_->members : empty;
    }

private:
    friend class GraphScope;
    friend class RenderGraph;
    explicit SubGraphHandle(std::shared_ptr<SubGraphState> state)
        : state_(std::move(state)) {}

    std::shared_ptr<SubGraphState> state_;
};

/**
 * @brief CRTP marker for a C++-declared sub-graph type.
 *
 * A derived type supplies `using PortConfig = ...`, `using Params = ...`, and
 * `Build(GraphScope&, const Params&)`. PortConfig is a normal constexpr node
 * configuration made with CONSTEXPR_NODE_CONFIG and INPUT_SLOT/OUTPUT_SLOT.
 */
template<typename Derived>
class SubGraphType {
public:
    virtual ~SubGraphType() = default;
};

/**
 * @brief Composition facade that prefixes names and records expanded membership.
 *
 * GraphScope deliberately exposes graph building, not execution. Every node added
 * through it is a normal RenderGraph node with a scoped instance name.
 */
class GraphScope {
public:
    GraphScope(RenderGraph& graph, std::string scopeName)
        : graph_(&graph)
        , scopeName_(std::move(scopeName))
        , expansion_(std::make_shared<SubGraphExpansionContext>()) {}

    template<typename TNodeType>
    NodeHandle AddNode(const std::string& localName) {
        const NodeHandle handle = graph_->AddNode<TNodeType>(ScopedName(localName));
        members_.push_back(handle);
        return handle;
    }

    NodeHandle AddNode(const std::string& typeName, const std::string& localName) {
        const NodeHandle handle = graph_->AddNode(typeName, ScopedName(localName));
        members_.push_back(handle);
        return handle;
    }

    NodeHandle AddNode(NodeTypeId typeId, const std::string& localName) {
        const NodeHandle handle = graph_->AddNode(typeId, ScopedName(localName));
        members_.push_back(handle);
        return handle;
    }

    void Connect(NodeHandle source, uint32_t sourceOutput,
                 NodeHandle target, uint32_t targetInput) {
        graph_->ConnectNodes(source, sourceOutput, target, targetInput);
    }

    template<typename SourceSlot, typename TargetSlot>
        requires requires { SourceSlot::index; TargetSlot::index; }
    void Connect(NodeHandle source, SourceSlot sourceSlot,
                 NodeHandle target, TargetSlot targetSlot) {
        graph_->ConnectNodes(source, SourceSlot::index, target, TargetSlot::index);
    }

    template<typename Port, typename MemberSlot>
    void BindInput(Port /*port*/, NodeHandle member, MemberSlot /*memberSlot*/) {
        using PortType = std::remove_cv_t<std::remove_reference_t<Port>>;
        using SlotType = std::remove_cv_t<std::remove_reference_t<MemberSlot>>;
        static_assert(requires { PortType::index; typename PortType::Type; } &&
                      requires { SlotType::index; typename SlotType::Type; },
                      "GraphScope::BindInput requires constexpr port and slot types");
        static_assert(std::is_same_v<typename PortType::Type, typename SlotType::Type>,
                      "Sub-graph input port type must match its member input type");
        BindMember(member, SlotType::index);
        inputBindings_.push_back({PortType::index, member, SlotType::index});
    }

    template<typename Port, typename MemberSlot>
    void BindOutput(Port /*port*/, NodeHandle member, MemberSlot /*memberSlot*/) {
        using PortType = std::remove_cv_t<std::remove_reference_t<Port>>;
        using SlotType = std::remove_cv_t<std::remove_reference_t<MemberSlot>>;
        static_assert(requires { PortType::index; typename PortType::Type; } &&
                      requires { SlotType::index; typename SlotType::Type; },
                      "GraphScope::BindOutput requires constexpr port and slot types");
        static_assert(std::is_same_v<typename PortType::Type, typename SlotType::Type>,
                      "Sub-graph output port type must match its member output type");
        BindMember(member, SlotType::index);
        if (std::any_of(outputBindings_.begin(), outputBindings_.end(),
                        [portIndex = PortType::index](const SubGraphBinding& binding) {
                            return binding.port == portIndex;
                        })) {
            throw std::runtime_error("Sub-graph output port " +
                                     std::to_string(PortType::index) +
                                     " is bound more than once in " + scopeName_);
        }
        outputBindings_.push_back({PortType::index, member, SlotType::index});
    }

    template<typename Port, typename NestedPort>
    void BindInput(Port /*port*/, const SubGraphHandle& nested, NestedPort /*nestedPort*/) {
        using PortType = std::remove_cv_t<std::remove_reference_t<Port>>;
        using NestedPortType = std::remove_cv_t<std::remove_reference_t<NestedPort>>;
        static_assert(requires { PortType::index; typename PortType::Type; } &&
                      requires { NestedPortType::index; typename NestedPortType::Type; },
                      "Nested sub-graph binding requires constexpr port types");
        static_assert(std::is_same_v<typename PortType::Type, typename NestedPortType::Type>,
                      "Nested sub-graph input port type mismatch");
        ValidateNested(nested);
        bool found = false;
        for (const SubGraphBinding& binding : nested.state_->inputBindings) {
            if (binding.port == NestedPortType::index) {
                found = true;
                inputBindings_.push_back({PortType::index, binding.member, binding.memberSlot});
            }
        }
        if (!found) {
            throw std::runtime_error("Nested sub-graph input port " +
                                     std::to_string(NestedPortType::index) + " is not bound");
        }
    }

    template<typename Port, typename NestedPort>
    void BindOutput(Port /*port*/, const SubGraphHandle& nested, NestedPort /*nestedPort*/) {
        using PortType = std::remove_cv_t<std::remove_reference_t<Port>>;
        using NestedPortType = std::remove_cv_t<std::remove_reference_t<NestedPort>>;
        static_assert(requires { PortType::index; typename PortType::Type; } &&
                      requires { NestedPortType::index; typename NestedPortType::Type; },
                      "Nested sub-graph binding requires constexpr port types");
        static_assert(std::is_same_v<typename PortType::Type, typename NestedPortType::Type>,
                      "Nested sub-graph output port type mismatch");
        ValidateNested(nested);
        if (std::any_of(outputBindings_.begin(), outputBindings_.end(),
                        [portIndex = PortType::index](const SubGraphBinding& binding) {
                            return binding.port == portIndex;
                        })) {
            throw std::runtime_error("Sub-graph output port " +
                                     std::to_string(PortType::index) +
                                     " is bound more than once in " + scopeName_);
        }
        const auto nestedOutput = std::find_if(
            nested.state_->outputBindings.begin(), nested.state_->outputBindings.end(),
            [portIndex = NestedPortType::index](const SubGraphBinding& binding) {
                return binding.port == portIndex;
            });
        if (nestedOutput == nested.state_->outputBindings.end()) {
            throw std::runtime_error("Nested sub-graph output port " +
                                     std::to_string(NestedPortType::index) +
                                     " is not bound exactly once");
        }
        outputBindings_.push_back({PortType::index,
                                   nestedOutput->member,
                                   nestedOutput->memberSlot});
    }

    template<typename TSubGraph, typename Params>
    SubGraphHandle Instantiate(const std::string& localName, const Params& params) {
        const SubGraphHandle nested = graph_->InstantiateImpl<TSubGraph>(
            ScopedName(localName), params, expansion_);
        members_.insert(members_.end(), nested.GetMembers().begin(), nested.GetMembers().end());
        return nested;
    }

    template<typename TSubGraph>
    SubGraphHandle Instantiate(const std::string& localName) {
        using Params = typename TSubGraph::Params;
        return Instantiate<TSubGraph>(localName, Params{});
    }

    const std::vector<NodeHandle>& GetMembers() const { return members_; }
    const std::string& GetScopeName() const { return scopeName_; }

private:
    friend class RenderGraph;

    GraphScope(RenderGraph& graph, std::string scopeName,
               std::shared_ptr<SubGraphExpansionContext> expansion)
        : graph_(&graph)
        , scopeName_(std::move(scopeName))
        , expansion_(std::move(expansion)) {}

    std::string ScopedName(const std::string& localName) const {
        if (localName.empty()) {
            throw std::runtime_error("Sub-graph member name cannot be empty in " + scopeName_);
        }
        return scopeName_.empty() ? localName : scopeName_ + "/" + localName;
    }

    void BindMember(NodeHandle member, uint32_t slot) const {
        if (!graph_->GetInstance(member)) {
            throw std::runtime_error("Sub-graph binding references an invalid member handle in " +
                                     scopeName_);
        }
        (void)slot;
    }

    void ValidateNested(const SubGraphHandle& nested) const {
        if (!nested.state_ || nested.state_->graph != graph_) {
            throw std::runtime_error("Nested sub-graph handle belongs to a different graph");
        }
    }

    void Finalize(size_t inputCount, size_t outputCount) {
        for (const SubGraphBinding& binding : inputBindings_) {
            if (binding.port >= inputCount) {
                throw std::runtime_error("Sub-graph input port index " +
                                         std::to_string(binding.port) + " is out of range in " +
                                         scopeName_);
            }
        }
        for (const SubGraphBinding& binding : outputBindings_) {
            if (binding.port >= outputCount) {
                throw std::runtime_error("Sub-graph output port index " +
                                         std::to_string(binding.port) + " is out of range in " +
                                         scopeName_);
            }
        }
        for (size_t port = 0; port < inputCount; ++port) {
            const bool bound = std::any_of(inputBindings_.begin(), inputBindings_.end(),
                [port](const SubGraphBinding& binding) { return binding.port == port; });
            if (!bound) {
                throw std::runtime_error("Unbound sub-graph input port " +
                                         std::to_string(port) + " in " + scopeName_);
            }
        }
        for (size_t port = 0; port < outputCount; ++port) {
            const size_t count = static_cast<size_t>(std::count_if(
                outputBindings_.begin(), outputBindings_.end(),
                [port](const SubGraphBinding& binding) { return binding.port == port; }));
            if (count != 1) {
                throw std::runtime_error("Sub-graph output port " + std::to_string(port) +
                                         " must be bound exactly once in " + scopeName_);
            }
        }
    }

    void Rollback() {
        for (auto it = members_.rbegin(); it != members_.rend(); ++it) {
            graph_->RemoveNode(*it);
        }
        members_.clear();
    }

    RenderGraph* graph_;
    std::string scopeName_;
    std::shared_ptr<SubGraphExpansionContext> expansion_;
    std::vector<NodeHandle> members_;
    std::vector<SubGraphBinding> inputBindings_;
    std::vector<SubGraphBinding> outputBindings_;
};

template<typename TSubGraph, typename Params>
SubGraphHandle RenderGraph::Instantiate(
    const std::string& instanceName, const Params& params) {
    return InstantiateImpl<TSubGraph>(instanceName, params, nullptr);
}

template<typename TSubGraph>
SubGraphHandle RenderGraph::Instantiate(const std::string& instanceName) {
    using Params = typename TSubGraph::Params;
    return Instantiate<TSubGraph>(instanceName, Params{});
}

template<typename TSubGraph, typename Params>
SubGraphHandle RenderGraph::InstantiateImpl(
    const std::string& instanceName,
    const Params& params,
    const std::shared_ptr<SubGraphExpansionContext>& parentExpansion) {
    static_assert(std::is_base_of_v<SubGraphType<TSubGraph>, TSubGraph>,
                  "TSubGraph must derive from SubGraphType<TSubGraph>");
    if (instanceName.empty()) {
        throw std::runtime_error("Sub-graph instance name cannot be empty");
    }

    const auto expansion = parentExpansion ? parentExpansion
                                           : std::make_shared<SubGraphExpansionContext>();
    const std::type_index type = std::type_index(typeid(TSubGraph));
    if (expansion->typeStack.size() >= 8) {
        throw std::runtime_error("Sub-graph nesting depth cap 8 exceeded at " + instanceName);
    }
    if (std::find(expansion->typeStack.begin(), expansion->typeStack.end(), type) !=
        expansion->typeStack.end()) {
        std::string chain;
        for (const std::string& name : expansion->nameStack) {
            if (!chain.empty()) chain += " -> ";
            chain += name;
        }
        if (!chain.empty()) chain += " -> ";
        chain += instanceName;
        throw std::runtime_error("Sub-graph type-instantiation cycle: " + chain);
    }

    expansion->typeStack.push_back(type);
    expansion->nameStack.push_back(instanceName);
    GraphScope scope(*this, instanceName, expansion);
    try {
        TSubGraph definition;
        definition.Build(scope, params);
        using PortConfig = typename TSubGraph::PortConfig;
        scope.Finalize(PortConfig::INPUT_COUNT, PortConfig::OUTPUT_COUNT);

        auto state = std::make_shared<SubGraphState>();
        state->graph = this;
        state->name = instanceName;
        state->members = scope.members_;
        state->inputBindings = scope.inputBindings_;
        state->outputBindings = scope.outputBindings_;
        state->inputCount = PortConfig::INPUT_COUNT;
        state->outputCount = PortConfig::OUTPUT_COUNT;

        expansion->nameStack.pop_back();
        expansion->typeStack.pop_back();
        return SubGraphHandle(std::move(state));
    } catch (...) {
        scope.Rollback();
        expansion->nameStack.pop_back();
        expansion->typeStack.pop_back();
        throw;
    }
}

template<typename SourceSlot, typename Port>
void RenderGraph::Connect(NodeHandle source, SourceSlot sourceSlot,
                          const SubGraphHandle& target, Port targetPort) {
    using SourceType = std::remove_cv_t<std::remove_reference_t<SourceSlot>>;
    using PortType = std::remove_cv_t<std::remove_reference_t<Port>>;
    static_assert(requires { SourceType::index; typename SourceType::Type; } &&
                  requires { PortType::index; typename PortType::Type; },
                  "RenderGraph::Connect requires constexpr slot and port types");
    static_assert(std::is_same_v<typename SourceType::Type, typename PortType::Type>,
                  "Source slot type must match sub-graph input port type");
    if (!target.state_ || target.state_->graph != this) {
        throw std::runtime_error("Invalid sub-graph target handle");
    }
    bool found = false;
    for (const SubGraphBinding& binding : target.state_->inputBindings) {
        if (binding.port == PortType::index) {
            found = true;
            ConnectNodes(source, SourceType::index, binding.member, binding.memberSlot);
        }
    }
    if (!found) {
        throw std::runtime_error("Sub-graph input port " + std::to_string(PortType::index) +
                                 " is not bound");
    }
}

template<typename Port, typename TargetSlot>
void RenderGraph::Connect(const SubGraphHandle& source, Port sourcePort,
                          NodeHandle target, TargetSlot targetSlot) {
    using PortType = std::remove_cv_t<std::remove_reference_t<Port>>;
    using TargetType = std::remove_cv_t<std::remove_reference_t<TargetSlot>>;
    static_assert(requires { PortType::index; typename PortType::Type; } &&
                  requires { TargetType::index; typename TargetType::Type; },
                  "RenderGraph::Connect requires constexpr port and slot types");
    static_assert(std::is_same_v<typename PortType::Type, typename TargetType::Type>,
                  "Sub-graph output port type must match target slot type");
    if (!source.state_ || source.state_->graph != this) {
        throw std::runtime_error("Invalid sub-graph source handle");
    }
    size_t count = 0;
    for (const SubGraphBinding& binding : source.state_->outputBindings) {
        if (binding.port == PortType::index) {
            ++count;
            ConnectNodes(binding.member, binding.memberSlot, target, TargetType::index);
        }
    }
    if (count != 1) {
        throw std::runtime_error("Sub-graph output port " + std::to_string(PortType::index) +
                                 " must resolve to exactly one member");
    }
}

template<typename SourcePort, typename TargetPort>
void RenderGraph::Connect(const SubGraphHandle& source, SourcePort sourcePort,
                          const SubGraphHandle& target, TargetPort targetPort) {
    using SourceType = std::remove_cv_t<std::remove_reference_t<SourcePort>>;
    using TargetType = std::remove_cv_t<std::remove_reference_t<TargetPort>>;
    static_assert(requires { SourceType::index; typename SourceType::Type; } &&
                  requires { TargetType::index; typename TargetType::Type; },
                  "RenderGraph::Connect requires constexpr port types");
    static_assert(std::is_same_v<typename SourceType::Type, typename TargetType::Type>,
                  "Sub-graph port types must match");
    if (!source.state_ || source.state_->graph != this ||
        !target.state_ || target.state_->graph != this) {
        throw std::runtime_error("Sub-graph handles must belong to this graph");
    }
    size_t sourceCount = 0;
    for (const SubGraphBinding& sourceBinding : source.state_->outputBindings) {
        if (sourceBinding.port != SourceType::index) continue;
        ++sourceCount;
        bool targetFound = false;
        for (const SubGraphBinding& targetBinding : target.state_->inputBindings) {
            if (targetBinding.port != TargetType::index) continue;
            targetFound = true;
            ConnectNodes(sourceBinding.member, sourceBinding.memberSlot,
                         targetBinding.member, targetBinding.memberSlot);
        }
        if (!targetFound) {
            throw std::runtime_error("Sub-graph input port " + std::to_string(TargetType::index) +
                                     " is not bound");
        }
    }
    if (sourceCount != 1) {
        throw std::runtime_error("Sub-graph output port " + std::to_string(SourceType::index) +
                                 " must resolve to exactly one member");
    }
}

} // namespace Vixen::RenderGraph
