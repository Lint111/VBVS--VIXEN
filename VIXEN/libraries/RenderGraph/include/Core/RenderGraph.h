#pragma once

#include "Headers.h"

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
#include "Core/TBBGraphExecutor.h"       // Sprint 6.4: Parallel execution
#include "Core/ResourceAccessTracker.h"  // Sprint 6.4: Conflict detection
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

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
     * @param mainCacher Main cache system (optional, defaults to singleton)
     */
    explicit RenderGraph(
        NodeTypeRegistry* registry,
        EventBus::MessageBus* messageBus = nullptr,
        Logger* mainLogger = nullptr,
        CashSystem::MainCacher* mainCacher = nullptr
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
     * @brief Check if graph is compiled
     */
    bool IsCompiled() const { return isCompiled; }

    // ====== Execution ======

    /**
     * @brief Execute the render graph (low-level)
     * @param commandBuffer Command buffer to record into
     *
     * NOTE: This is a low-level method for recording into an external command buffer.
     * For full frame rendering, use RenderFrame() instead.
     */
    void Execute(VkCommandBuffer commandBuffer);

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
        return mainCacher ? *mainCacher : CashSystem::MainCacher::Instance();
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

    // ====== Parallel Execution (Sprint 6.4) ======

    /**
     * @brief Enable or disable parallel node execution
     *
     * When enabled, nodes without resource conflicts execute concurrently
     * using Intel TBB flow_graph. Requires graph recompilation to take effect.
     *
     * IMPORTANT: Parallel execution is experimental. Use only for graphs where:
     * - Nodes have proper resource access tracking
     * - No implicit ordering dependencies (only explicit connections)
     * - All node Execute() methods are thread-safe
     *
     * @param enable true to enable parallel execution, false for sequential
     */
    void SetParallelExecutionEnabled(bool enable);

    /**
     * @brief Check if parallel execution is enabled
     */
    [[nodiscard]] bool IsParallelExecutionEnabled() const {
        return parallelExecutionEnabled_;
    }

    /**
     * @brief Set the execution mode for TBB executor
     *
     * @param mode Parallel, Sequential, or Limited
     */
    void SetExecutionMode(TBBExecutionMode mode);

    /**
     * @brief Get current execution mode
     */
    [[nodiscard]] TBBExecutionMode GetExecutionMode() const;

    /**
     * @brief Set maximum concurrency for parallel execution
     *
     * @param maxConcurrency Maximum concurrent nodes (0 = unlimited, hardware_concurrency)
     */
    void SetMaxConcurrency(size_t maxConcurrency);

    /**
     * @brief Get TBB executor statistics
     *
     * Useful for debugging and performance analysis.
     */
    [[nodiscard]] TBBExecutorStats GetExecutorStats() const;

    /**
     * @brief Get the resource access tracker (for debugging/analysis)
     */
    [[nodiscard]] const ResourceAccessTracker& GetResourceAccessTracker() const {
        return resourceAccessTracker_;
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
    bool isCompiled = false;

    // Event-driven recompilation
    std::set<NodeHandle> dirtyNodes;
    bool renderPaused = false;

    // Cleanup management
    CleanupStack cleanupStack;
    ResourceDependencyTracker dependencyTracker;
    std::unordered_map<NodeInstance*, size_t> dependentCounts;  // Reference counting for partial cleanup
    DeferredDestructionQueue deferredDestruction;  // Zero-stutter hot-reload

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

    // Sprint 6.4: Parallel execution with TBB flow_graph
    TBBGraphExecutor tbbExecutor_;
    ResourceAccessTracker resourceAccessTracker_;
    bool parallelExecutionEnabled_ = false;
    bool executorNeedsRebuild_ = true;  // Rebuild TBB graph after compilation

    // Sprint 4 Phase B: Lifetime scope management (optional, externally provided)
    LifetimeScopeManager* scopeManager_ = nullptr;

    // Compilation phases
    void AnalyzeDependencies();
    void AllocateResources();
    void GeneratePipelines();
    void BuildExecutionOrder();
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

} // namespace Vixen::RenderGraph
