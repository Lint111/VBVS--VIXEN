#pragma once

#include "Headers.h"

#include "NodeInstance.h"
#include "NodeTypeRegistry.h"
#include "GraphTopology.h"
#include "ResourceVariant.h"
#include "CleanupStack.h"
#include "ResourceDependencyTracker.h"
#include "DeferredDestruction.h"
#include "EventTypes/RenderGraphEvents.h"
#include "EventBus/MessageBus.h"
#include "EventBus/Message.h"
#include "Time/EngineTime.h"
#include "CashSystem/MainCacher.h"
#include "LoopManager.h"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include "EventTypes/RenderGraphEvents.h"

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

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
     * @brief Add a node to the graph
     * @param typeName The name of the node type
     * @param instanceName Unique name for this instance
     * @return Handle to the created node
     */
    NodeHandle AddNode(const std::string& typeName, const std::string& instanceName);

    /**
     * @brief Add a node using type ID
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
     * @brief Get execution order (after compilation)
     */
    const std::vector<NodeInstance*>& GetExecutionOrder() const { return executionOrder; }

    /**
     * @brief Get the graph topology
     */
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

    
private:
    // Core components
    NodeTypeRegistry* typeRegistry;
    EventBus::MessageBus* messageBus = nullptr;  // Non-owning pointer
    CashSystem::MainCacher* mainCacher = nullptr;  // Non-owning pointer
    EventBus::EventSubscriptionID cleanupEventSubscription = 0;
    EventBus::EventSubscriptionID renderPauseSubscription = 0;
    EventBus::EventSubscriptionID windowResizeSubscription = 0;
    EventBus::EventSubscriptionID windowStateSubscription = 0;
    EventBus::EventSubscriptionID deviceSyncSubscription = 0;
    EventBus::EventSubscriptionID windowCloseSubscription = 0;
    // Vixen::Vulkan::Resources::VulkanDevice* primaryDevice;  // Removed - nodes access device directly

    #ifdef _DEBUG
    // Debug logger (non-owning pointer — application owns the logger)
    Logger* mainLogger = nullptr;
    #endif

    // Graph data
    std::vector<std::unique_ptr<NodeInstance>> instances;
    std::map<std::string, NodeHandle> nameToHandle;
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

    // Compilation phases
    void AnalyzeDependencies();
    void AllocateResources();
    void GeneratePipelines();
    void BuildExecutionOrder();
    void ComputeDependentCounts();
    void RecursiveCleanup(NodeInstance* node, std::set<NodeInstance*>& cleaned);

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
