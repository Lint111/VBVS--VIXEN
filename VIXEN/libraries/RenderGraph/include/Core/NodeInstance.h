#pragma once

#include "Data/Core/CompileTimeResourceSystem.h"
#include "NodeType.h"
#include "Data/ParameterDataTypes.h"
#include "Data/NodeParameterManager.h"
#include "CleanupStack.h"
#include "LoopManager.h"
#include "INodeWiring.h"
#include "SlotTask.h"
#include "ResourceBudgetManager.h"
#include "GraphLifecycleHooks.h"
#include "NodeContext.h"
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <memory>
#include "Logger.h"
#include "MessageBus.h"

// Forward declare Logger to avoid circular dependency
class Logger;

namespace Vixen::RenderGraph {

// Forward declarations
class RenderGraph;
// NodeHandle defined in CleanupStack.h

/**
 * @brief Connection point for graph edges
 */
struct NodeConnection {
    NodeInstance* sourceNode = nullptr;
    uint32_t sourceOutputIndex = 0;
    NodeInstance* targetNode = nullptr;
    uint32_t targetInputIndex = 0;
};

/**
 * @brief Node Instance - Concrete instantiation of a NodeType
 *
 * Represents a specific usage of a rendering operation in the graph.
 * Multiple instances can be created from the same NodeType.
 *
 * **Encapsulation**: Inherits from INodeWiring to provide controlled access
 * to graph wiring methods without exposing all internals via friend declarations.
 */
class NodeInstance : public INodeWiring {
    // Allow ConnectionBatch to access protected resource methods for bulk connection operations
    friend class ConnectionBatch;

public:
    // Phase F: Bundle structure - ensures inputs/outputs stay aligned
    // Each bundle represents one task/array index with all its slots
    struct Bundle {
        std::vector<Resource*> inputs;   // One entry per static input slot
        std::vector<Resource*> outputs;  // One entry per static output slot
    };

public:
    // Phase 0.4: Auto-generated loop slots (reserved slot indices)
    // These slots are automatically available on all nodes for loop connections
    static constexpr uint32_t AUTO_LOOP_IN_SLOT = UINT32_MAX - 1;
    static constexpr uint32_t AUTO_LOOP_OUT_SLOT = UINT32_MAX - 2;

    NodeInstance(
        const std::string& instanceName,
        NodeType* nodeType
    );

    virtual ~NodeInstance();

    // Prevent copying
    NodeInstance(const NodeInstance&) = delete;
    NodeInstance& operator=(const NodeInstance&) = delete;

    // Identity
    const std::string& GetInstanceName() const { return instanceName; }
    NodeType* GetNodeType() const { return nodeType; }
    NodeTypeId GetTypeId() const;
    // Backwards-compatible accessor used in multiple places (legacy GetType calls)
    NodeType* GetType() const { return nodeType; }

    // Logger access
    Logger* GetLogger() const { return nodeLogger.get(); }
    uint64_t GetInstanceId() const { return instanceId; }
    NodeHandle GetHandle() const { return nodeHandle; }
    void SetHandle(NodeHandle handle) { nodeHandle = handle; }

    // Tags (for bulk operations via events)
    void AddTag(const std::string& tag);
    void RemoveTag(const std::string& tag);
    bool HasTag(const std::string& tag) const;
    const std::vector<std::string>& GetTags() const { return tags; }

    // Device affinity
    Vixen::Vulkan::Resources::VulkanDevice* GetDevice() const { return device; }

    // Owning graph access (for cleanup registration)
    RenderGraph* GetOwningGraph() const { return owningGraph; }
    void SetOwningGraph(RenderGraph* graph) { owningGraph = graph; }

    // Node arrayable flag
    bool AllowsInputArrays() const { return allowInputArrays; }
    void SetAllowInputArrays(bool allow) { allowInputArrays = allow; }

    // Get array size for a slot
    size_t GetInputCount(uint32_t slotIndex) const;
    size_t GetOutputCount(uint32_t slotIndex) const;

    // Parameters (delegated to NodeParameterManager)
    void SetParameter(const std::string& name, const ParamTypeValue& value) {
        parameterManager.SetParameter(name, value);
    }
    const ParamTypeValue* GetParameter(const std::string& name) const {
        return parameterManager.GetParameter(name);
    }
    template<typename T>
    T GetParameterValue(const std::string& name, const T& defaultValue = T{}) const {
        return parameterManager.GetParameterValue<T>(name, defaultValue);
    }

    // Dependencies
    const std::vector<NodeInstance*>& GetDependencies() const { return dependencies; }
    void AddDependency(NodeInstance* node);
    void RemoveDependency(NodeInstance* node);
    bool DependsOn(NodeInstance* node) const;

    // Slot validation
    /**
     * @brief Validate if an input slot index is valid for this node
     *
     * @param slotIndex The input slot index to validate
     * @param errorMessage Output parameter for error description
     * @return true if valid, false otherwise
     */
    virtual bool ValidateInputSlot(uint32_t slotIndex, std::string& errorMessage) const;

    /**
     * @brief Validate if an output slot index is valid for this node
     *
     * @param slotIndex The output slot index to validate
     * @param errorMessage Output parameter for error description
     * @return true if valid, false otherwise
     */
    virtual bool ValidateOutputSlot(uint32_t slotIndex, std::string& errorMessage) const;

    // State (read-only access - lifecycle methods manage state internally)
    NodeState GetState() const { return state; }

    // Execution order (set during compilation)
    uint32_t GetExecutionOrder() const { return executionOrder; }
    void SetExecutionOrder(uint32_t order) { executionOrder = order; }

    // Workload metrics
    size_t GetInputMemoryFootprint() const { return inputMemoryFootprint; }
    void SetInputMemoryFootprint(size_t size) { inputMemoryFootprint = size; }

    // Cleanup registration helper
    /**
     * @brief Register cleanup with automatic dependency resolution
     * 
     * Automatically builds dependency list from input slots using ResourceDependencyTracker.
     * Call this at the end of Compile() after all outputs are set.
     */
    void RegisterCleanup();

    // Logger Registration
    void RegisterToParentLogger(Logger* parentLogger);
    void DeregisterFromParentLogger(Logger* parentLogger);

    // EventBus Integration
    /**
     * @brief Set the message bus for event publishing/subscription
     * 
     * Called by RenderGraph during AddNode() if EventBus is available.
     * Nodes can publish events and subscribe to relevant messages.
     */
    void SetMessageBus(EventBus::MessageBus* bus) { messageBus = bus; }
    EventBus::MessageBus* GetMessageBus() const { return messageBus; }

    /**
     * @brief Set the Vulkan device for this node instance
     *
     * Many node implementations read a device handle from inputs during Setup/Compile
     * and store it in a per-node variable. To centralize state, nodes should call
     * SetDevice(...) so the base class holds the canonical device pointer which can
     * be queried by the RenderGraph and other systems via GetDevice().
     */
    void SetDevice(Vixen::Vulkan::Resources::VulkanDevice* dev) { device = dev; }

    /**
     * @brief Subscribe to a specific message type
     * @param type Message type to subscribe to
     * @param handler Callback function
     * @return Subscription ID for unsubscribing
     */
    EventBus::EventSubscriptionID SubscribeToMessage(
        EventBus::MessageType type,
        EventBus::MessageHandler handler
    );

    /**
     * @brief Subscribe to messages by category
     * @param category Event category to subscribe to
     * @param handler Callback function
     * @return Subscription ID for unsubscribing
     */
    EventBus::EventSubscriptionID SubscribeToCategory(
        EventBus::EventCategory category,
        EventBus::MessageHandler handler
    );

    /**
     * @brief Unsubscribe from a message
     * @param subscriptionId ID returned by SubscribeToMessage/Category
     */
    void UnsubscribeFromMessage(EventBus::EventSubscriptionID subscriptionId);

    /**
     * @brief Mark this node as needing recompilation
     * 
     * Called when the node receives an event that invalidates its current state.
     * The RenderGraph will recompile dirty nodes at the next safe point.
     */
    void MarkNeedsRecompile();

    /**
     * @brief Check if node needs recompilation
     */
    bool NeedsRecompile() const { return needsRecompile; }

    /**
     * @brief Clear the recompilation flag
     * Called by RenderGraph after recompiling the node
     */
    void ClearNeedsRecompile() { needsRecompile = false; }

    /**
     * @brief Reset the cleanup flag
     * Called by RenderGraph after successful recompilation so node can be cleaned up again
     */
    void ResetCleanupFlag() { cleanedUp = false; }

    /**
     * @brief Check if node was marked for deferred recompilation during execution
     *
     * Deferred recompilation occurs when a node is marked dirty (via events)
     * DURING graph execution. The node will be recompiled on the next frame.
     *
     * @return true if deferred recompilation requested
     */
    bool HasDeferredRecompile() const { return deferredRecompile; }

    /**
     * @brief Clear the deferred recompilation flag
     *
     * Called by RenderGraph after detecting deferred recompilation
     * and marking the node for recompilation on next frame.
     */
    void ClearDeferredRecompile() { deferredRecompile = false; }

    // Phase 0.4: Loop connection API
    /**
     * @brief Connect this node to a loop
     *
     * Adds loopRef to the node's connected loops. Nodes can be connected to
     * multiple loops (OR logic - executes if ANY loop active).
     *
     * @param loopRef Stable pointer to LoopReference from LoopManager
     */
    void SetLoopInput(const LoopReference* loopRef);

    /**
     * @brief Get loop reference for pass-through to connected nodes
     *
     * Returns the first connected loop, or nullptr if no loops connected.
     * Used by RenderGraph during loop propagation.
     *
     * @return Const pointer to first LoopReference, or nullptr
     */
    const LoopReference* GetLoopOutput() const;

    /**
     * @brief Check if this node should execute this frame
     *
     * Returns true if:
     * - No loops connected (always execute), OR
     * - At least one connected loop has shouldExecuteThisFrame = true
     *
     * @return true if node should execute, false otherwise
     */
    bool ShouldExecuteThisFrame() const;

    /**
     * @brief Get fixed timestep delta time from connected loop
     *
     * Returns deltaTime from the first active loop, or 0.0 if no loops active.
     * Useful for nodes that need to know their update rate.
     *
     * @return Delta time in seconds
     */
    double GetLoopDeltaTime() const;

    /**
     * @brief Get step count from connected loop
     *
     * Returns stepCount from the first active loop, or 0 if no loops active.
     *
     * @return Total steps executed by loop
     */
    uint64_t GetLoopStepCount() const;

    // Template method pattern - public final methods with automatic boilerplate
    /**
     * @brief Setup lifecycle method with automatic boilerplate
     *
     * Automatically handles:
     * - Reset compile-time input tracking
     * - Execute PreSetup hooks
     * - Calls SetupImpl() for derived class logic
     * - Execute PostSetup hooks
     *
     * Derived classes should override SetupImpl(), NOT this method.
     */
    virtual void Setup() {
        try {
            ResetInputsUsedInCompile();
            std::cout << "[NodeInstance::Setup] Executing PreSetup hook for: " << GetInstanceName() << std::endl;
            ExecuteNodeHook(NodeLifecyclePhase::PreSetup);
            SetupImpl();
            std::cout << "[NodeInstance::Setup] Executing PostSetup hook for: " << GetInstanceName() << std::endl;
            ExecuteNodeHook(NodeLifecyclePhase::PostSetup);
        } catch (const std::exception& e) {
            std::cerr << "[NodeInstance::Setup] EXCEPTION in node '" << GetInstanceName() << "': " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cerr << "[NodeInstance::Setup] UNKNOWN EXCEPTION in node '" << GetInstanceName() << "'" << std::endl;
            throw;
        }
    }

    /**
     * @brief Compile lifecycle method with automatic cleanup registration
     *
     * Automatically handles:
     * - Execute PreCompile hooks
     * - Calls CompileImpl() for derived class logic
     * - Execute PostCompile hooks
     * - Registers node in CleanupStack (prevents forgetting RegisterCleanup())
     *
     * Derived classes should override CompileImpl(), NOT this method.
     */
    virtual void Compile() final {
        try {
            ExecuteNodeHook(NodeLifecyclePhase::PreCompile);
            CompileImpl();

            ExecuteNodeHook(NodeLifecyclePhase::PostCompile);
            RegisterCleanup();
        } catch (const std::exception& e) {
            std::cerr << "[NodeInstance::Compile] EXCEPTION in node '" << GetInstanceName() << "': " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cerr << "[NodeInstance::Compile] UNKNOWN EXCEPTION in node '" << GetInstanceName() << "'" << std::endl;
            throw;
        }
    }

    /**
     * @brief Execute lifecycle method with automatic task orchestration
     *
     * Automatically handles:
     * - Execute PreExecute hooks
     * - Analyzes slot configuration to determine task count
     * - Generates tasks based on SlotScope and array sizes
     * - Sets up task-local In()/Out() context for each task
     * - Calls ExecuteImpl() for each task with pre-bound slot access
     * - Node implementations use In()/Out() without knowing about indices
     * - Execute PostExecute hooks
     *
     * Derived classes should override ExecuteImpl(), NOT this method.
     */
    virtual void Execute() final {
        try {
            ExecuteNodeHook(NodeLifecyclePhase::PreExecute);
            ExecuteImpl();
            ExecuteNodeHook(NodeLifecyclePhase::PostExecute);
        } catch (const std::exception& e) {
            std::cerr << "[NodeInstance::Execute] EXCEPTION in node '" << GetInstanceName() << "': " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cerr << "[NodeInstance::Execute] UNKNOWN EXCEPTION in node '" << GetInstanceName() << "'" << std::endl;
            throw;
        }
    }

    /**
     * @brief Cleanup method with double-cleanup protection
     *
     * This is the public interface for cleanup. It ensures CleanupImpl()
     * is only called once, even if Cleanup() is called multiple times
     * (e.g., from CleanupStack and from destructor).
     *
     * Automatically handles:
     * - Execute PreCleanup hooks
     * - Calls CleanupImpl() for derived class logic
     * - Execute PostCleanup hooks
     *
     * Derived classes (like TypedNode) can override this method to implement
     * task-based cleanup, following the same pattern as Execute().
     */
    virtual void Cleanup() final {
        if (cleanedUp) {
            return;  // Already cleaned up
        }
        try {
            ExecuteNodeHook(NodeLifecyclePhase::PreCleanup);
            CleanupImpl();
            ExecuteNodeHook(NodeLifecyclePhase::PostCleanup);
            cleanedUp = true;
        } catch (const std::exception& e) {
            std::cerr << "[NodeInstance::Cleanup] EXCEPTION in node '" << GetInstanceName() << "': " << e.what() << std::endl;
            cleanedUp = true;  // Mark as cleaned up even if it failed
            throw;
        } catch (...) {
            std::cerr << "[NodeInstance::Cleanup] UNKNOWN EXCEPTION in node '" << GetInstanceName() << "'" << std::endl;
            cleanedUp = true;  // Mark as cleaned up even if it failed
            throw;
        }
    }

protected:
    // ============================================================================
    // CONTEXT FACTORY METHODS - Override to provide specialized contexts
    // ============================================================================

    /**
     * @brief Create SetupContext for this node
     *
     * Override in derived classes to provide specialized setup contexts.
     * Default: Returns base SetupContext.
     */
    virtual SetupContext CreateSetupContext(uint32_t taskIndex) {
        return SetupContext(this, taskIndex);
    }

    /**
     * @brief Create CompileContext for this node
     *
     * Override in derived classes to provide specialized compile contexts.
     * Default: Returns base CompileContext.
     */
    virtual CompileContext CreateCompileContext(uint32_t taskIndex) {
        return CompileContext(this, taskIndex);
    }

    /**
     * @brief Create ExecuteContext for this node
     *
     * Override in derived classes to provide specialized execute contexts.
     * Default: Returns base ExecuteContext.
     */
    virtual ExecuteContext CreateExecuteContext(uint32_t taskIndex) {
        return ExecuteContext(this, taskIndex);
    }

    /**
     * @brief Create CleanupContext for this node
     *
     * Override in derived classes to provide specialized cleanup contexts.
     * Default: Returns base CleanupContext.
     */
    virtual CleanupContext CreateCleanupContext(uint32_t taskIndex) {
        return CleanupContext(this, taskIndex);
    }
    /**
     * @brief Setup implementation for derived classes
     *
     * Task orchestration and context creation happens here.
     * Creates tasks based on DetermineTaskCount() and calls SetupImpl(SetupContext&) for each.
     *
     * Override this if you need custom task orchestration logic.
     */
    virtual void SetupImpl() {
        uint32_t taskCount = DetermineTaskCount();
        for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            SetupContext ctx = CreateSetupContext(taskIndex);
            SetupImpl(ctx);
        }
    }

    /**
     * @brief Setup implementation with context (final implementation)
     *
     * Override this in concrete nodes to implement actual setup logic.
     * Access inputs/outputs via context methods (if context supports them).
     *
     * @param ctx Setup context with phase-specific capabilities
     */
    virtual void SetupImpl(SetupContext& ctx) {
        // Default: no-op (many nodes don't need setup)
    }

    /**
     * @brief Compile implementation for derived classes
     *
     * Task orchestration and context creation happens here.
     * Creates tasks based on DetermineTaskCount() and calls CompileImpl(CompileContext&) for each.
     *
     * Override this if you need custom task orchestration logic.
     */
    virtual void CompileImpl() {
        uint32_t taskCount = DetermineTaskCount();
        for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            CompileContext ctx = CreateCompileContext(taskIndex);
            CompileImpl(ctx);
        }
    }

    /**
     * @brief Compile implementation with context (final implementation)
     *
     * Override this in concrete nodes to implement actual compilation logic.
     * Access inputs/outputs via context methods.
     *
     * @param ctx Compile context with phase-specific capabilities
     *
     * NOTE: TypedNode overrides this with TypedCompileContext variant.
     * Default: no-op (TypedNode provides implementation).
     */
    virtual void CompileImpl(CompileContext& ctx) {
        // Default: no-op (TypedNode hierarchy provides override)
    }

    /**
     * @brief Execute implementation for derived classes
     *
     * Task orchestration and context creation happens here.
     * Creates tasks based on DetermineTaskCount() and executes them.
     * For task-based nodes, executes tasks in parallel if supported.
     *
     * Override this if you need custom task orchestration logic.
     */
    virtual void ExecuteImpl() {
        uint32_t taskCount = DetermineTaskCount();

        // TODO: Add parallel execution support for arrayable nodes
        // For now, execute sequentially
        for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            ExecuteContext ctx = CreateExecuteContext(taskIndex);
            ExecuteImpl(ctx);
        }
    }

    /**
     * @brief Execute implementation with context (final implementation)
     *
     * Override this in concrete nodes to implement actual execution logic.
     * Access inputs/outputs via context methods with automatic task binding.
     *
     * @param ctx Execute context with task-bound input/output access
     *
     * NOTE: TypedNode overrides this with TypedExecuteContext variant.
     * Default: no-op (TypedNode provides implementation).
     */
    virtual void ExecuteImpl(ExecuteContext& ctx) {
        // Default: no-op (TypedNode hierarchy provides override)
    }

    /**
     * @brief Cleanup implementation for derived classes
     *
     * Task orchestration and context creation happens here.
     * Creates tasks based on DetermineTaskCount() and calls CleanupImpl(CleanupContext&) for each.
     *
     * Override this if you need custom task orchestration logic.
     */
    virtual void CleanupImpl() {
        uint32_t taskCount = DetermineTaskCount();
        for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            CleanupContext ctx = CreateCleanupContext(taskIndex);
            CleanupImpl(ctx);
        }
    }

    /**
     * @brief Cleanup implementation with context (final implementation)
     *
     * Override this in concrete nodes to implement actual cleanup logic.
     *
     * @param ctx Cleanup context (no input/output access during cleanup)
     *
     * Default: no-op (some nodes don't allocate resources).
     * Always null out VulkanDevice pointers and handles after destroying resources.
     */
    virtual void CleanupImpl(CleanupContext& ctx) {
        // Default: no-op (some nodes don't allocate resources)
    }

    // ============================================================================
    // PHASE F: SLOT TASK SYSTEM - Task-based array processing with budget awareness
    // ============================================================================

    /**
     * @brief Execute tasks for array-based slot processing
     *
     * Generates tasks from an array input slot and executes them with optional
     * budget-aware parallelism. Nodes can use this to process array elements
     * independently without manually writing loop code.
     *
     * Example usage in a TextureLoader node:
     * @code
     * // Slot config with SlotScope::TaskLevel for per-element processing
     * INPUT_SLOT(FILE_PATHS, std::vector<std::string>, 0,
     *            SlotNullability::Required, SlotRole::Dependency,
     *            SlotMutability::ReadOnly, SlotScope::TaskLevel);
     *
     * void CompileImpl() override {
     *     // Define task function for loading one texture
     *     auto loadTexture = [this](SlotTaskContext& ctx) -> bool {
     *         uint32_t index = ctx.GetElementIndex();
     *         std::string path = filePaths[index];
     *         // Load texture, create VkImage, store in outputs[index]
     *         return true;
     *     };
     *
     *     // Execute tasks (automatically determines sequential vs parallel)
     *     uint32_t successCount = ExecuteTasks(FILE_PATHS_SLOT_INDEX, loadTexture);
     *     LogInfo("Loaded " + std::to_string(successCount) + " textures");
     * }
     * @endcode
     *
     * @param slotIndex Input slot containing array data
     * @param taskFunction Function to execute per array element
     * @param budgetManager Optional budget manager for parallelism calculation
     * @param forceSequential If true, always execute sequentially (default: auto-detect)
     * @return Number of successful tasks
     */
    uint32_t ExecuteTasks(
        uint32_t slotIndex,
        const SlotTaskFunction& taskFunction,
        ResourceBudgetManager* budgetManager = nullptr,
        bool forceSequential = false
    );

    /**
     * @brief Determine task count based on slot configuration
     *
     * Analyzes all input slots to determine how many tasks should be generated:
     * - NodeLevel only: 1 task (all inputs processed together)
     * - TaskLevel/ParameterizedInput: N tasks (one per element in parameterized slot)
     *
     * @return Number of tasks to execute (0 if no inputs connected)
     */
    uint32_t DetermineTaskCount() const;

    /**
     * @brief Get ResourceBudgetManager from owning graph
     *
     * Retrieves the budget manager for use with task execution.
     * Returns nullptr if graph doesn't have a budget manager configured.
     *
     * @return Pointer to ResourceBudgetManager, or nullptr
     */
    ResourceBudgetManager* GetBudgetManager() const;

    /**
     * @brief Get the unified resource manager from owning graph
     *
     * Provides access to the unified resource management facade.
     * Use this for:
     * - Tracking BoundedArray allocations
     * - Querying stack usage
     * - Budget management
     *
     * @return Pointer to ResourceManagerBase, or nullptr if graph not set
     */
    ResourceManagerBase* GetResourceManager() const;

    /**
     * @brief Get SlotScope for an input slot
     *
     * Queries the node's config to determine the resource scope for a slot.
     * Used internally by ExecuteTasks() to decide task granularity.
     *
     * @param slotIndex Input slot index
     * @return SlotScope enum value (defaults to NodeLevel if not found)
     */
    SlotScope GetSlotScope(uint32_t slotIndex) const;

    /**
     * @brief Execute node-level lifecycle hooks
     *
     * Called automatically during Setup/Compile/Execute/Cleanup lifecycle methods.
     * Delegates to RenderGraph's hook system if graph is available.
     *
     * @param phase Lifecycle phase to execute hooks for
     */
    void ExecuteNodeHook(NodeLifecyclePhase phase);

public:
    // ============================================================================
    // INodeWiring interface implementation (graph wiring methods)
    // ============================================================================

    /**
     * @brief Get input resource at slot/array index (INodeWiring implementation)
     *
     * **Usage**:
     * - RenderGraph uses during validation and connection setup
     * - Node implementations should use In() from TypedNodeInstance instead
     *
     * @param slotIndex Slot index (0-based)
     * @param arrayIndex Array element index (0 for non-array slots)
     * @return Resource pointer, or nullptr if not connected
     */
    Resource* GetInput(uint32_t slotIndex, uint32_t arrayIndex = 0) const override;

    /**
     * @brief Get output resource at slot/array index (INodeWiring implementation)
     *
     * **Usage**:
     * - RenderGraph uses during connection setup
     * - Node implementations should use Out() from TypedNodeInstance instead
     *
     * @param slotIndex Slot index (0-based)
     * @param arrayIndex Array element index (0 for non-array slots)
     * @return Resource pointer, or nullptr if not yet created
     */
    Resource* GetOutput(uint32_t slotIndex, uint32_t arrayIndex = 0) const override;

    /**
     * @brief Set input resource at slot/array index (INodeWiring implementation)
     *
     * **Usage**:
     * - RenderGraph uses during ConnectNodes() to wire inputs
     * - Node implementations should NOT call this directly
     *
     * @param slotIndex Slot index (0-based)
     * @param arrayIndex Array element index (0 for non-array slots)
     * @param resource Resource pointer (lifetime managed by graph)
     */
    void SetInput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource) override;

    /**
     * @brief Set output resource at slot/array index (INodeWiring implementation)
     *
     * **Usage**:
     * - RenderGraph uses during ConnectNodes() to allocate outputs
     * - Node implementations should use Out() from TypedNodeInstance instead
     *
     * @param slotIndex Slot index (0-based)
     * @param arrayIndex Array element index (0 for non-array slots)
     * @param resource Resource pointer (lifetime managed by graph)
     */
    void SetOutput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource) override;

protected:
    /**
     * @brief Set node state (internal use only)
     *
     * State transitions managed automatically by lifecycle methods and RenderGraph.
     * External code should not manually change state.
     *
     * @param newState New state to transition to
     */
    void SetState(NodeState newState) { state = newState; }

    // Grant access to internal state management
    friend class RenderGraph;

    // Grant TypedNode access to bundles (it's a derived class needing direct access)
    template<typename ConfigType>
    friend class TypedNode;

    // Instance identification
    std::string instanceName;
    uint64_t instanceId;
    NodeHandle nodeHandle;  // Handle to this node in the graph
    NodeType* nodeType;
    std::vector<std::string> tags;  // Tags for bulk operations (e.g., "shadow-maps", "post-process")


    // Device affinity
    Vixen::Vulkan::Resources::VulkanDevice* device;

    // Owning graph pointer (for cleanup registration)
    RenderGraph* owningGraph = nullptr;

    // EventBus integration
    EventBus::MessageBus* messageBus = nullptr;  // Non-owning pointer
    std::vector<EventBus::EventSubscriptionID> eventSubscriptions;
    bool needsRecompile = false;
    bool deferredRecompile = false;  // Set when marked dirty during execution

    // Node-level behavior flags
    // When true the node will accept either single inputs or array-shaped inputs
    // (IA<I>) and should handle producing scalar or array outputs accordingly.
    // Default false to preserve existing behavior.
    bool allowInputArrays = false;

protected:
    // Phase F: Resources organized as bundles (one bundle per task/array index)
    // bundles[taskIndex].inputs[slotIndex] -> Resource for that task and slot
    std::vector<Bundle> bundles;

private:
    // Parameter management (encapsulated)
    NodeParameterManager parameterManager;

    // Runtime tracking: which input slots were used during the last Compile() call.
    // This is transient runtime state (not serialized). It is mutable so that
    // const accessors (like TypedNode::In()) can mark usage during Compile().
    mutable std::vector<std::vector<bool>> inputUsedInCompile;

public:
    // Phase F: Bundle access (for graph-level operations)
    const std::vector<Bundle>& GetBundles() const { return bundles; }

    // Mark that a specific input slot (slotIndex) was used during compile at given arrayIndex.
    // This method is const so it can be called from const accessors.
    void MarkInputUsedInCompile(uint32_t slotIndex, uint32_t arrayIndex) const {
        // Ensure the vector dimensions
        if (inputUsedInCompile.size() <= slotIndex) {
            inputUsedInCompile.resize(slotIndex + 1);
        }
        auto& vec = inputUsedInCompile[slotIndex];
        if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1, false);
        vec[arrayIndex] = true;
    }

    // Query whether a given input slot/array index was marked as used during
    // the last Compile() call. Safe to call from external systems that need to
    // decide compile-time dependencies.
    bool IsInputUsedInCompile(uint32_t slotIndex, uint32_t arrayIndex) const {
        if (slotIndex >= inputUsedInCompile.size()) return false;
        const auto& vec = inputUsedInCompile[slotIndex];
        if (arrayIndex >= vec.size()) return false;
        return vec[arrayIndex];
    }

    // Reset used-in-compile markers for all inputs. Called before a new Compile().
    void ResetInputsUsedInCompile() const {
        for (auto& vec : inputUsedInCompile) {
            std::fill(vec.begin(), vec.end(), false);
        }
    }

protected:
    // Phase 0.4: Loop connections (zero or more loops)
    std::vector<const LoopReference*> connectedLoops;

    // Execution state
    NodeState state = NodeState::Created;
    std::vector<NodeInstance*> dependencies;
    uint32_t executionOrder = 0;
    bool cleanedUp = false;  // Cleanup protection flag

    // Metrics
    size_t inputMemoryFootprint = 0;

    // Phase F: Task manager for array processing
    SlotTaskManager taskManager;

    // Hierarchical logger (shared ownership for lifecycle management)
    // - Node holds shared_ptr (strong reference)
    // - Parent logger also holds shared_ptr (strong reference)
    // - When node destroyed: refcount 2→1, logger stays alive for extraction
    // - After log extraction: parent clears children, refcount 1→0, cleanup
    std::shared_ptr<Logger> nodeLogger;

    // Helper methods
    void AllocateResources();
    void DeallocateResources();
};

} // namespace Vixen::RenderGraph
