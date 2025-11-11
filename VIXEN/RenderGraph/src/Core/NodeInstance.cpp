#include "Core/NodeInstance.h"
#include "Core/RenderGraph.h"
#include "Core/ResourceDependencyTracker.h"
#include "Core/GraphLifecycleHooks.h"
#include "Core/NodeLogging.h"
#include "VulkanResources/VulkanDevice.h"
#include <algorithm>
#include <functional>
#include <atomic>
#include "EventBus/MessageBus.h"
#include "EventBus/Message.h"

namespace Vixen::EventBus {
    class MessageBus;
}



namespace Vixen::RenderGraph {

// Static counter for unique instance IDs
static std::atomic<uint64_t> nextInstanceId{1};

NodeInstance::NodeInstance(
    const std::string& instanceName,
    NodeType* nodeType
)
    : instanceName(instanceName)
    , instanceId(nextInstanceId.fetch_add(1))
    , nodeType(nodeType)
    , device(nullptr)
{
    // Phase F: Bundles start empty - they grow as inputs/outputs are connected
    // Each bundle will be sized to hold all slots when first accessed via SetInput/SetOutput

    if (nodeType) {
        // Initialize node-level behavior flags from type
        allowInputArrays = nodeType->GetAllowInputArrays();
    }

    // Initialize logger (disabled by default)
    nodeLogger = std::make_unique<Logger>(instanceName, false);
}

NodeInstance::~NodeInstance() {
    // Unsubscribe from all EventBus messages
    if (messageBus) {
        for (EventBus::EventSubscriptionID id : eventSubscriptions) {
            messageBus->Unsubscribe(id);
        }
        eventSubscriptions.clear();
    }
    
    Cleanup();
}

NodeTypeId NodeInstance::GetTypeId() const {
    return nodeType ? nodeType->GetTypeId() : 0;
}

void NodeInstance::AddTag(const std::string& tag) {
    if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
        tags.push_back(tag);
    }
}

void NodeInstance::RemoveTag(const std::string& tag) {
    tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
}

bool NodeInstance::HasTag(const std::string& tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

Resource* NodeInstance::GetInput(uint32_t slotIndex, uint32_t arrayIndex) const {
    // Phase F: Bundle-first organization
    if (arrayIndex < bundles.size() && slotIndex < bundles[arrayIndex].inputs.size()) {
        return bundles[arrayIndex].inputs[slotIndex];
    }
    return nullptr;
}

Resource* NodeInstance::GetOutput(uint32_t slotIndex, uint32_t arrayIndex) const {
    // Phase F: Bundle-first organization
    if (arrayIndex < bundles.size() && slotIndex < bundles[arrayIndex].outputs.size()) {
        return bundles[arrayIndex].outputs[slotIndex];
    }
    return nullptr;
}

Resource* NodeInstance::GetOutputResource(uint32_t slotIndex) const {
    // Simply return the Resource* from the bundle slot
    // Resources are stored in bundles[arrayIndex].outputs[slotIndex]
    // For now, use arrayIndex = 0 (non-array case)
    return GetOutput(slotIndex, 0);
}

void NodeInstance::SetInput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource) {
    // Phase F: Bundle-first organization - ensure bundle exists
    if (arrayIndex >= bundles.size()) {
        bundles.resize(arrayIndex + 1);
    }

    // Ensure slot exists in this bundle's inputs
    if (slotIndex >= bundles[arrayIndex].inputs.size()) {
        bundles[arrayIndex].inputs.resize(slotIndex + 1, nullptr);
    }

    bundles[arrayIndex].inputs[slotIndex] = resource;
}

void NodeInstance::SetOutput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource) {
    // Phase F: Bundle-first organization - ensure bundle exists
    if (arrayIndex >= bundles.size()) {
        bundles.resize(arrayIndex + 1);
    }

    // Ensure slot exists in this bundle's outputs
    if (slotIndex >= bundles[arrayIndex].outputs.size()) {
        bundles[arrayIndex].outputs.resize(slotIndex + 1, nullptr);
    }

    bundles[arrayIndex].outputs[slotIndex] = resource;
}

size_t NodeInstance::GetInputCount(uint32_t slotIndex) const {
    // Phase F: Count how many bundles have this input slot populated
    size_t count = 0;
    for (const auto& bundle : bundles) {
        if (slotIndex < bundle.inputs.size() && bundle.inputs[slotIndex] != nullptr) {
            count++;
        }
    }
    return count;
}

size_t NodeInstance::GetOutputCount(uint32_t slotIndex) const {
    // Phase F: Count how many bundles have this output slot populated
    size_t count = 0;
    for (const auto& bundle : bundles) {
        if (slotIndex < bundle.outputs.size() && bundle.outputs[slotIndex] != nullptr) {
            count++;
        }
    }
    return count;
}

// Parameter management now delegated to NodeParameterManager (inline in header)

void NodeInstance::AddDependency(NodeInstance* node) {
    if (node && !DependsOn(node)) {
        dependencies.push_back(node);
    }
}

void NodeInstance::RemoveDependency(NodeInstance* node) {
    auto it = std::find(dependencies.begin(), dependencies.end(), node);
    if (it != dependencies.end()) {
        dependencies.erase(it);
    }
}

bool NodeInstance::DependsOn(NodeInstance* node) const {
    return std::find(dependencies.begin(), dependencies.end(), node) != dependencies.end();
}

// Dead code removed: UpdatePerformanceStats, ComputeCacheKey

void NodeInstance::RegisterToParentLogger(Logger* parentLogger)
{
    if (parentLogger && nodeLogger) {
        parentLogger->AddChild(nodeLogger.get());
    }
}
void NodeInstance::DeregisterFromParentLogger(Logger* parentLogger)
{
    if (parentLogger && nodeLogger) {
        parentLogger->RemoveChild(nodeLogger.get());
    }
}

void NodeInstance::ExecuteNodeHook(NodeLifecyclePhase phase) {
    if (!owningGraph) {
        NODE_LOG_WARNING("[NodeInstance::ExecuteNodeHook] WARNING: No owning graph for node: " + GetInstanceName());
        return; // No graph - can't execute hooks
    }

    owningGraph->GetLifecycleHooks().ExecuteNodeHooks(phase, this);
}

void NodeInstance::RegisterCleanup() {
    if (!owningGraph) {
        return; // Can't register without graph reference
    }

    // Build dependency list automatically from input resources
    auto& tracker = owningGraph->GetDependencyTracker();
    std::vector<NodeHandle> cleanupDeps = tracker.BuildCleanupDependencies(this);

    // Register with cleanup stack using handle
    owningGraph->GetCleanupStack().Register(
        nodeHandle,
        GetInstanceName() + "_Cleanup",
        [this]() { this->Cleanup(); },
        cleanupDeps
    );
}

void NodeInstance::AllocateResources() {
    // Phase F: Calculate input memory footprint from bundle descriptors
    inputMemoryFootprint = 0;
    for (const auto& bundle : bundles) {
        for (const auto* input : bundle.inputs) {
            if (input) {
                // Calculate memory from image descriptor
                if (const auto* imgDesc = input->GetDescriptor<ImageDescriptor>()) {
                    // Estimate: width * height * bytesPerPixel
                    uint32_t bytesPerPixel = 4; // Default RGBA8
                    // TODO: Calculate actual bytes based on imgDesc->format
                    inputMemoryFootprint += imgDesc->width * imgDesc->height * bytesPerPixel;
                }
                // Calculate memory from buffer descriptor
                else if (const auto* bufDesc = input->GetDescriptor<BufferDescriptor>()) {
                    inputMemoryFootprint += bufDesc->size;
                }
            }
        }
    }
}

void NodeInstance::DeallocateResources() {
    // Resources are now managed via the graph's resource system
    // Nothing to deallocate here
    state = NodeState::Created;
}

// EventBus Integration Implementation

EventBus::EventSubscriptionID NodeInstance::SubscribeToMessage(
    EventBus::MessageType type,
    EventBus::MessageHandler handler
) {
    if (!messageBus) {
        return 0;  // No bus available
    }

    EventBus::EventSubscriptionID id = messageBus->Subscribe(type, std::move(handler));
    eventSubscriptions.push_back(id);
    return id;
}

EventBus::EventSubscriptionID NodeInstance::SubscribeToCategory(
    EventBus::EventCategory category,
    EventBus::MessageHandler handler
) {
    if (!messageBus) {
        return 0;  // No bus available
    }

    EventBus::EventSubscriptionID id = messageBus->SubscribeCategory(category, std::move(handler));
    eventSubscriptions.push_back(id);
    return id;
}

void NodeInstance::UnsubscribeFromMessage(EventBus::EventSubscriptionID subscriptionId) {
    if (!messageBus) {
        return;
    }

    messageBus->Unsubscribe(subscriptionId);
    
    // Remove from our tracking list
    eventSubscriptions.erase(
        std::remove(eventSubscriptions.begin(), eventSubscriptions.end(), subscriptionId),
        eventSubscriptions.end()
    );
}

void NodeInstance::MarkNeedsRecompile() {
    needsRecompile = true;
    
    // Notify the owning graph that this node is dirty
    if (owningGraph) {
        // Find our handle in the graph
        for (size_t i = 0; i < owningGraph->GetNodeCount(); ++i) {
            if (owningGraph->GetInstance({static_cast<uint32_t>(i)}) == this) {
                // If currently executing, defer the dirty marking until execution completes
                if (state == NodeState::Executing) {
                    deferredRecompile = true;
                } else {
                    owningGraph->MarkNodeNeedsRecompile({static_cast<uint32_t>(i)});
                }
                break;
            }
        }
    }
}

// Phase 0.4: Loop connection implementation
void NodeInstance::SetLoopInput(const LoopReference* loopRef) {
    if (!loopRef) return;

    // Add to connected loops (avoid duplicates)
    if (std::find(connectedLoops.begin(), connectedLoops.end(), loopRef) == connectedLoops.end()) {
        connectedLoops.push_back(loopRef);
    }
}

const LoopReference* NodeInstance::GetLoopOutput() const {
    // Pass through first connected loop (or nullptr)
    return connectedLoops.empty() ? nullptr : connectedLoops[0];
}

bool NodeInstance::ShouldExecuteThisFrame() const {
    if (connectedLoops.empty()) {
        return true;  // No loops = always execute
    }

    // Execute if ANY connected loop is active (OR logic)
    for (const auto* loop : connectedLoops) {
        if (loop && loop->shouldExecuteThisFrame) {
            return true;
        }
    }

    return false;
}

double NodeInstance::GetLoopDeltaTime() const {
    // Return first active loop's delta time
    for (const auto* loop : connectedLoops) {
        if (loop && loop->shouldExecuteThisFrame) {
            return loop->deltaTime;
        }
    }
    return 0.0;
}

uint64_t NodeInstance::GetLoopStepCount() const {
    // Return first active loop's step count
    for (const auto* loop : connectedLoops) {
        if (loop && loop->shouldExecuteThisFrame) {
            return loop->stepCount;
        }
    }
    return 0;
}

// ============================================================================
// PHASE F: SLOT TASK SYSTEM IMPLEMENTATION
// ============================================================================

uint32_t NodeInstance::DetermineTaskCount() const {
    // Phase F: Task count = number of bundles
    // Each bundle represents one task with aligned inputs/outputs
    return bundles.empty() ? 1 : static_cast<uint32_t>(bundles.size());
}

uint32_t NodeInstance::ExecuteTasks(
    uint32_t slotIndex,
    const SlotTaskFunction& taskFunction,
    ResourceBudgetManager* budgetManager,
    bool forceSequential
) {
    // Get SlotScope from config to determine task granularity
    SlotScope scope = GetSlotScope(slotIndex);

    // Generate tasks based on array size and scope
    auto tasks = taskManager.GenerateTasks(this, slotIndex, scope);

    if (tasks.empty()) {
        return 0;
    }

    // Use provided budget manager or get from graph
    ResourceBudgetManager* budget = budgetManager ? budgetManager : GetBudgetManager();

    // Execute sequentially or in parallel based on flags and budget
    if (forceSequential || !budget) {
        return taskManager.ExecuteSequential(tasks, taskFunction);
    } else {
        return taskManager.ExecuteParallel(tasks, taskFunction, budget);
    }
}

ResourceBudgetManager* NodeInstance::GetBudgetManager() const {
    if (owningGraph) {
        return owningGraph->GetBudgetManager();
    }
    return nullptr;
}

SlotScope NodeInstance::GetSlotScope(uint32_t slotIndex) const {
    if (!nodeType) {
        return SlotScope::NodeLevel;
    }

    // Query NodeType for slot metadata
    // For now, default to TaskLevel for array processing
    // TODO: Extend NodeType to store SlotScope metadata from configs
    return SlotScope::TaskLevel;
}

} // namespace Vixen::RenderGraph
