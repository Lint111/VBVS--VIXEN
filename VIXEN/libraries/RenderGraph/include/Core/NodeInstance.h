#pragma once

#include "Data/Core/CompileTimeResourceSystem.h"
#include "NodeType.h"
#include "Data/ParameterDataTypes.h"
#include "Data/NodeParameterManager.h"
#include "CleanupStack.h"
#include "LoopManager.h"
#include "INodeWiring.h"
#include "SlotTask.h"
#include "Memory/ResourceBudgetManager.h"
#include "NodeContext.h"
#include "ITaskProfile.h"  // Sprint 6.3: Task profile system
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <memory>
#include <cstddef>
#include <cstdint>
#include "Logger.h"
#include "MessageBus.h"

// Forward declare Logger to avoid circular dependency
class Logger;

namespace Vixen::RenderGraph {

// Forward declarations
class RenderGraph;
class TaskProfileRegistry;
// NodeHandle defined in CleanupStack.h

/**
 * @brief Lifecycle phases used to organize node task profiles.
 *
 * These phases describe profiling and budget-estimation data only. Execution
 * remains owned by the node lifecycle and SlotTaskManager.
 */
enum class TaskProfilePhase : uint8_t {
    Setup,
    Compile,
    Execute,
    Cleanup
};

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

    // Forwarding nodes identify the outputs containing the same image inputs. Ordinary
    // dependencies (extent, format, camera) do not forward an image and contribute no usage.
    virtual std::vector<uint32_t> GetForwardedImageOutputs(uint32_t, bool) const {
        return {};
    }
    NodeTypeId GetTypeId() const;
    // Backwards-compatible accessor used in multiple places (legacy GetType calls)
    NodeType* GetType() const { return nodeType; }

    // Logger access
    Logger* GetLogger() const { return nodeLogger.get(); }
    uint64_t GetInstanceId() const { return instanceId; }
    NodeHandle GetHandle() const { return nodeHandle; }
    void SetHandle(NodeHandle handle) { nodeHandle = handle; }

    // Lock-free phase 1 (SubmitChannel): the node's stable position in the graph's executionOrder,
    // assigned at compile. Used as the node's per-frame submit-channel slot index so a frame-path
    // submit publishes into its OWN slot with no lock (design §2.5). SIZE_MAX = not assigned (a node
    // that never runs through the lowered executor, e.g. a headless/test path); such a node must not
    // publish. See RenderGraph::PublishSubmit / SubmitChannel.h.
    std::size_t GetSubmitOrdinal() const { return submitOrdinal_; }
    void SetSubmitOrdinal(std::size_t ordinal) { submitOrdinal_ = ordinal; }

    // Tags (for bulk operations via events)
    void AddTag(const std::string& tag);
    void RemoveTag(const std::string& tag);
    bool HasTag(const std::string& tag) const;
    const std::vector<std::string>& GetTags() const { return tags; }

    // Device affinity
    Vixen::Vulkan::Resources::VulkanDevice* GetDevice() const { return device; }

    /// Device-owning nodes return false after a compile that produced no VkDevice (adapter enumeration
    /// or vkCreateDevice failed). RenderGraph::RecoverFromDeviceLoss checks it after each node's
    /// Compile so a failed recreation stops the rebuild instead of handing a null device downstream.
    virtual bool ProducesUsableDevice() const { return true; }

    // Owning graph access (for cleanup registration)
    RenderGraph* GetOwningGraph() const { return owningGraph; }
    void SetOwningGraph(RenderGraph* graph) { owningGraph = graph; }

    // =========================================================================
    // Task Profile System (Sprint 6.3) - Cacher-style API
    // =========================================================================
    //
    // Architecture: NodeInstance→TaskProfileRegistry wiring
    //
    // Profiles are OWNED by RenderGraph's TaskProfileRegistry (central ownership).
    // NodeInstance provides convenient access via owningGraph->GetTaskProfileRegistry().
    //
    // Initialization flow:
    // 1. RenderGraph creates TaskProfileRegistry in constructor
    // 2. RenderGraph::AddNode() sets node->SetOwningGraph(this)
    // 3. Node's Setup/Compile calls GetOrCreateProfile<T>() which:
    //    a. Checks if profile exists in registry (via GetTaskProfile)
    //    b. If not, creates new profile and registers it
    //    c. Returns non-owning pointer to registry-owned profile
    //
    // Profile lifetime: Owned by registry until RenderGraph destruction or
    // explicit UnregisterTask(). Safe to store returned pointers in node members.
    //
    // Thread safety: Single-threaded (all RenderGraph calls from main thread).
    // =========================================================================

    /**
     * @brief Get or create a task profile (cacher pattern)
     *
     * Looks up profile by taskId. If not found, creates it using the provided
     * arguments and registers it. Returns pointer to the profile.
     *
     * Usage:
     * @code
     * // Simple - creates SimpleTaskProfile("shadowPass", "shadow")
     * auto* profile = GetOrCreateProfile<SimpleTaskProfile>("shadowPass", "shadow");
     *
     * // Resolution-based profile with custom table
     * std::array<uint32_t, 11> resolutions = {...};
     * auto* profile = GetOrCreateProfile<ResolutionTaskProfile>(
     *     "shadowMap", "shadow", resolutions);
     * @endcode
     *
     * @tparam ProfileType Concrete profile type (e.g., SimpleTaskProfile)
     * @param taskId Unique task identifier
     * @param args Constructor arguments for ProfileType (taskId, category, ...)
     * @return Non-owning pointer to profile (owned by registry)
     */
    template<typename ProfileType, typename... Args>
    ProfileType* GetOrCreateProfile(const std::string& taskId, Args&&... args);

    /**
     * @brief Get existing task profile (no creation)
     *
     * @param taskId Task identifier
     * @return Pointer to profile, or nullptr if not found
     */
    ITaskProfile* GetTaskProfile(const std::string& taskId);
    const ITaskProfile* GetTaskProfile(const std::string& taskId) const;

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

    // ========================================================================
    // Pre-Allocation Requirements (Sprint 5.5)
    // ========================================================================

    /**
     * @brief Pre-allocation requirements for zero-allocation runtime
     *
     * Nodes return this from GetPreAllocationRequirements() during graph
     * compilation. The graph aggregates requirements and pre-allocates
     * pools to prevent runtime allocations.
     */
    struct PreAllocationRequirements {
        uint32_t commandBufferCount = 0;    ///< Command buffers needed from pool
        uint32_t descriptorSetCount = 0;    ///< Descriptor sets needed (future)
        size_t stagingBufferBytes = 0;      ///< Staging buffer space needed (future)

        PreAllocationRequirements& operator+=(const PreAllocationRequirements& other) {
            commandBufferCount += other.commandBufferCount;
            descriptorSetCount += other.descriptorSetCount;
            stagingBufferBytes += other.stagingBufferBytes;
            return *this;
        }
    };

    /**
     * @brief Get pre-allocation requirements for this node
     *
     * Override in nodes that allocate Vulkan resources at runtime.
     * Called during graph compilation to determine pool sizes.
     *
     * Default: Returns zero requirements (no pre-allocation needed).
     *
     * @return Pre-allocation requirements for this node instance
     */
    virtual PreAllocationRequirements GetPreAllocationRequirements() const {
        return PreAllocationRequirements{};
    }

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
     * The RenderGraph will recompile dirty nodes at the next safe point, and hands `cause` to the
     * node's RecompileImpl.
     */
    void MarkNeedsRecompile(RecompileCause cause = RecompileCause::InputsChanged);

    /// @brief The node in this node's graph with `handle`, or null (no graph / invalid handle)
    NodeInstance* ResolveGraphNode(NodeHandle handle) const;

    /// @brief Cause recorded by the last MarkNeedsRecompile (read by RenderGraph's recompile wave)
    RecompileCause GetPendingRecompileCause() const { return pendingRecompileCause_; }

    /**
     * @brief Recompile entry point used by RenderGraph: RecompileImpl(cause), then the recompile
     *        teardown (CleanupImpl only). The graph then runs Setup() and Compile().
     */
    void BeginRecompile(RecompileCause cause) {
        RecompileImpl(cause);
        Cleanup(CleanupReason::Recompile);
    }

    /**
     * @brief Check if node needs recompilation
     */
    bool NeedsRecompile() const { return needsRecompile; }

    /**
     * @brief Clear the recompilation flag
     * Called by RenderGraph after recompiling the node
     */
    void ClearNeedsRecompile() {
        needsRecompile = false;
        pendingRecompileCause_ = RecompileCause::InputsChanged;
    }

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

    // =========================================================================
    // Task profile API
    // =========================================================================
    //
    // =========================================================================

    /**
     * @brief Register a profile for a specific execution phase
     *
     * Profiles attached to a phase provide lifecycle-specific cost data for
     * budget tracking and diagnostics.
     *
     * Multiple profiles per phase enable composable sub-task measurement.
     *
     * @param phase The execution phase
     * @param profile Non-owning pointer to profile (must outlive node)
     */
    void RegisterPhaseProfile(TaskProfilePhase phase, ITaskProfile* profile);

    /**
     * @brief Get profiles registered for a phase
     *
     * @param phase The execution phase
     * @return Vector of profile pointers (may be empty)
     */
    const std::vector<ITaskProfile*>& GetPhaseProfiles(TaskProfilePhase phase) const;

    /**
     * @brief Clear all profiles for a phase
     */
    void ClearPhaseProfiles(TaskProfilePhase phase);

    /**
     * @brief Clear all registered phase profiles
     */
    void ClearAllPhaseProfiles();

    /**
     * @brief Estimate execution cost for a specific task
     *
     * Returns estimated cost in nanoseconds for budget-aware scheduling.
     * Used by TimelineCapacityTracker for frame budget management.
     *
     * Default implementation sums estimates from Execute phase profiles
     * registered via RegisterPhaseProfile(). Returns 0 if no profiles.
     *
     * Derived nodes can override to provide per-task estimation logic.
     *
     * @param taskIndex Bundle/task index (unused in default impl)
     * @return Estimated cost in nanoseconds (0 = unknown)
     */
    virtual uint64_t EstimateTaskCost(uint32_t taskIndex) const;

    // Template method pattern - public final methods with automatic boilerplate
    /**
     * @brief Setup lifecycle method with automatic boilerplate
     *
     * Automatically handles:
     * - Reset compile-time input tracking
     * - Calls SetupImpl() for derived class logic
     *
     * Derived classes should override SetupImpl(), NOT this method.
     */
    virtual void Setup() {
        try {
            ResetInputsUsedInCompile();
            SetupImpl();
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
     * - Calls CompileImpl() for derived class logic
     * - Registers node in CleanupStack (prevents forgetting RegisterCleanup())
     *
     * Derived classes should override CompileImpl(), NOT this method.
     */
    virtual void Compile() final {
        try {
            CompileImpl();
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
     * - Analyzes slot configuration to determine task count
     * - Generates tasks based on SlotScope and array sizes
     * - Sets up task-local In()/Out() context for each task
     * - Calls ExecuteImpl() for each task with pre-bound slot access
     * - Node implementations use In()/Out() without knowing about indices
     *
     * Derived classes should override ExecuteImpl(), NOT this method.
     */
    virtual void Execute() final {
        try {
            ExecuteImpl();
        } catch (const std::exception& e) {
            std::cerr << "[NodeInstance::Execute] EXCEPTION in node '" << GetInstanceName() << "': " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cerr << "[NodeInstance::Execute] UNKNOWN EXCEPTION in node '" << GetInstanceName() << "'" << std::endl;
            throw;
        }
    }

    /**
     * @brief Execute one lowered graph task with the node's transient state transition.
     *
     * RenderGraph's sequential oracle performs this state transition around Execute(). The shared
     * task adapter uses this entry point so a worker owns only its node's state; graph callbacks and
     * completion publication remain on the graph thread after the wave barrier.
     */
    void ExecuteAsGraphTask() {
        state = NodeState::Executing;
        Execute();
    }

    /**
     * @brief Cleanup method with double-cleanup protection
     *
     * This is the public interface for cleanup. It ensures CleanupImpl()
     * is only called once, even if Cleanup() is called multiple times
     * (e.g., from CleanupStack and from destructor).
     *
     * The graph says why it is tearing the node down; the node never has to ask. Each resource
     * scope has its own event, run inner to outer, so a node puts each resource in the event that
     * matches how long it lives and never branches on the reason:
     * - CleanupImpl()       every teardown: recompile, device loss and shutdown (what Compile built)
     * - DeviceReleaseImpl() device loss and shutdown: device-child state kept across recompiles
     * - ShutdownImpl()      shutdown only: state that outlives the device (OS window, RmlUi, ...)
     * A recompile therefore never runs DeviceReleaseImpl/ShutdownImpl, so nothing persistent is
     * destroyed and no device wait can be reached from a resize (FR-7).
     */
    virtual void Cleanup(CleanupReason reason = CleanupReason::FinalTeardown) final {
        if (cleanedUp) {
            return;  // Already cleaned up
        }
        try {
            CleanupImpl();
            if (reason != CleanupReason::Recompile) {
                DeviceReleaseImpl();
            }
            if (reason == CleanupReason::FinalTeardown) {
                ShutdownImpl();
            }
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
    virtual void SetupImpl(SetupContext& /*ctx*/) {
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
     * TypedNode dispatches the compile phase through TypedCompileImpl(TypedCompileContext&).
     * Default: no-op (TypedNode provides typed dispatch).
     */
    virtual void CompileImpl(CompileContext& /*ctx*/) {
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
     * TypedNode dispatches the execute phase through TypedExecuteImpl(TypedExecuteContext&).
     * Default: no-op (TypedNode provides typed dispatch).
     */
    virtual void ExecuteImpl(ExecuteContext& /*ctx*/) {
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
     * @brief Release what Compile built (final implementation)
     *
     * Runs on EVERY teardown: recompile, device loss and shutdown. Keep it lightweight: no
     * vkDeviceWaitIdle or fence wait (the recompile path drains the graph devices once, before any
     * node cleanup). State that must survive a recompile belongs in DeviceReleaseImpl/ShutdownImpl.
     *
     * @param ctx Cleanup context (no input/output access during cleanup)
     *
     * Default: no-op (some nodes don't allocate resources).
     * Always null out VulkanDevice pointers and handles after destroying resources.
     */
    virtual void CleanupImpl(CleanupContext& /*ctx*/) {
        // Default: no-op (some nodes don't allocate resources)
    }

    /// @brief Task orchestration for the device-release event (see Cleanup()).
    virtual void DeviceReleaseImpl() {
        uint32_t taskCount = DetermineTaskCount();
        for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            CleanupContext ctx = CreateCleanupContext(taskIndex);
            DeviceReleaseImpl(ctx);
        }
    }

    /**
     * @brief Release device-child state that persists across recompiles
     *
     * Runs on device loss and on shutdown, after CleanupImpl; never on a recompile. The device is
     * drained before this runs (RecoverFromDeviceLoss / graph teardown).
     */
    virtual void DeviceReleaseImpl(CleanupContext& /*ctx*/) {}

    /// @brief Task orchestration for the shutdown event (see Cleanup()).
    virtual void ShutdownImpl() {
        uint32_t taskCount = DetermineTaskCount();
        for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            CleanupContext ctx = CreateCleanupContext(taskIndex);
            ShutdownImpl(ctx);
        }
    }

    /**
     * @brief Release state that outlives the device (OS window, UI library, host-side caches)
     *
     * Runs once, on final teardown, after DeviceReleaseImpl.
     */
    virtual void ShutdownImpl(CleanupContext& /*ctx*/) {}

    /**
     * @brief The node is about to be recompiled (before CleanupImpl, Setup and Compile run again)
     *
     * @param cause Why: the node's own state was invalidated, its surface was resized, or a node it
     *        depends on is being recompiled in the same wave.
     */
    virtual void RecompileImpl(RecompileCause /*cause*/) {}

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
     * @brief Get SlotScope for an input slot
     *
     * Queries the node's config to determine the resource scope for a slot.
     * Used internally by ExecuteTasks() to decide task granularity.
     *
     * @param slotIndex Input slot index
     * @return SlotScope enum value (defaults to NodeLevel if not found)
     */
    SlotScope GetSlotScope(uint32_t slotIndex) const;

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

    // ============================================================================
    // ACCUMULATION GATHER (typed runtime fan-in)
    // ============================================================================

    /**
     * @brief Recorded producer connection feeding an accumulation input slot.
     *
     * An accumulation input slot gathers an array of values from N producer nodes
     * (fan-in). Unlike a direct connection -- which wires one producer output Resource
     * into the consumer input slot -- an accumulation slot records each producer here
     * and assembles the std::vector<T> at Execute time (see TypedNode::InAll).
     *
     * The gather is deferred to Execute (not Resolve) because producer outputs carry
     * per-frame values: each producer's CURRENT output Resource is read at gather time.
     */
    struct AccumulationSource {
        NodeInstance* producer = nullptr;  ///< Producer node whose output feeds the slot
        uint32_t outputSlot = 0;           ///< Producer output slot index
        int32_t sortKey = 0;               ///< Ordering key (ByMetadata); stable insertion order ties
    };

    /**
     * @brief Record a producer connection feeding an accumulation input slot.
     *
     * Called by AccumulationConnectionRule::Resolve() once per source connection.
     * Order of registration is preserved; the gather may additionally sort by sortKey.
     *
     * @param inputSlot  Consumer accumulation input slot index
     * @param producer   Producer node supplying a value
     * @param outputSlot Producer output slot index to read
     * @param sortKey    Ordering metadata (0 = connection order)
     */
    void RegisterAccumulationSource(uint32_t inputSlot, NodeInstance* producer,
                                    uint32_t outputSlot, int32_t sortKey = 0) {
        if (!producer) {
            return;
        }
        accumulationSources_[inputSlot].push_back(AccumulationSource{producer, outputSlot, sortKey});
    }

    /**
     * @brief Get the recorded producer connections for an accumulation input slot.
     *
     * @param inputSlot Consumer accumulation input slot index
     * @return Pointer to the source list, or nullptr if the slot has no sources.
     */
    const std::vector<AccumulationSource>* GetAccumulationSources(uint32_t inputSlot) const {
        auto it = accumulationSources_.find(inputSlot);
        if (it == accumulationSources_.end()) {
            return nullptr;
        }
        return &it->second;
    }

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
    // Lock-free phase 1: stable executionOrder position, assigned at Compile (SIZE_MAX until then).
    // Used as the node's per-frame SubmitChannel slot index. See GetSubmitOrdinal / SubmitChannel.h.
    std::size_t submitOrdinal_ = static_cast<std::size_t>(-1);
    NodeType* nodeType;
    std::vector<std::string> tags;  // Tags for bulk operations (e.g., "shadow-maps", "post-process")


    // Device affinity
    Vixen::Vulkan::Resources::VulkanDevice* device;

    // Owning graph pointer (for cleanup registration)
    RenderGraph* owningGraph = nullptr;

    // EventBus integration
    EventBus::MessageBus* messageBus = nullptr;  // Non-owning pointer
    EventBus::ScopedSubscriptions subscriptions_;  // RAII subscriptions (auto-unsubscribe on destruction)
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

    // Accumulation gather: per-(input slot) list of producer connections feeding it.
    // Populated by AccumulationConnectionRule::Resolve(); consumed at Execute by
    // TypedNode::InAll() to assemble std::vector<T> from the producers' current outputs.
    std::map<uint32_t, std::vector<AccumulationSource>> accumulationSources_;
    bool cleanedUp = false;  // Cleanup protection flag
    RecompileCause pendingRecompileCause_ = RecompileCause::InputsChanged;  // set by MarkNeedsRecompile

    // Metrics
    size_t inputMemoryFootprint = 0;

    // Phase→Profile mapping for task-level profiling (Sprint 6.5)
    std::map<TaskProfilePhase, std::vector<ITaskProfile*>> phaseProfiles_;

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

    // Internal helper for GetOrCreateProfile (implemented in .cpp)
    ITaskProfile* RegisterProfileIfAbsent(const std::string& taskId, std::unique_ptr<ITaskProfile> profile);
};

// ============================================================================
// Template Implementation (inline, no RenderGraph.h dependency)
// ============================================================================

template<typename ProfileType, typename... Args>
ProfileType* NodeInstance::GetOrCreateProfile(const std::string& taskId, Args&&... args) {
    static_assert(std::is_base_of_v<ITaskProfile, ProfileType>,
        "ProfileType must derive from ITaskProfile");

    // Check if exists first
    if (ITaskProfile* existing = GetTaskProfile(taskId)) {
        return dynamic_cast<ProfileType*>(existing);
    }

    // Create new and register
    auto newProfile = std::make_unique<ProfileType>(std::forward<Args>(args)...);
    newProfile->SetTaskId(taskId);

    ITaskProfile* registered = RegisterProfileIfAbsent(taskId, std::move(newProfile));
    return dynamic_cast<ProfileType*>(registered);
}

} // namespace Vixen::RenderGraph
