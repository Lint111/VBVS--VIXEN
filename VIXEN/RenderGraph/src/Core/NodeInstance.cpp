#include "Core/NodeInstance.h"
#include "Core/RenderGraph.h"
#include "Core/ResourceDependencyTracker.h"
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

// Phase F: Thread-local task index for parallel-safe slot access
thread_local uint32_t NodeInstance::currentTaskIndex = 0;

NodeInstance::NodeInstance(
    const std::string& instanceName,
    NodeType* nodeType
)
    : instanceName(instanceName)
    , instanceId(nextInstanceId.fetch_add(1))
    , nodeType(nodeType)
    , device(nullptr)
{
    // Allocate space for inputs and outputs based on schema
    // Each slot is a vector (size 1 for scalar, size N for array)
    if (nodeType) {
        inputs.resize(nodeType->GetInputCount());
        outputs.resize(nodeType->GetOutputCount());

        // Initialize each slot with empty vector (will be populated during connection/execution)
        for (auto& slot : inputs) {
            slot = {};
        }
        for (auto& slot : outputs) {
            slot = {};
        }

        // Initialize node-level behavior flags from type
        allowInputArrays = nodeType->GetAllowInputArrays();
    }

// Initialize logger in debug builds (MSVC: _DEBUG, GCC/Clang: DEBUG or !NDEBUG)
#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
    nodeLogger = std::make_unique<Logger>(instanceName);
#endif
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
    if (slotIndex < inputs.size() && arrayIndex < inputs[slotIndex].size()) {
        return inputs[slotIndex][arrayIndex];
    }
    return nullptr;
}

Resource* NodeInstance::GetOutput(uint32_t slotIndex, uint32_t arrayIndex) const {
    if (slotIndex < outputs.size() && arrayIndex < outputs[slotIndex].size()) {
        return outputs[slotIndex][arrayIndex];
    }
    return nullptr;
}

void NodeInstance::SetInput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource) {
    if (slotIndex < inputs.size()) {
        if (arrayIndex >= inputs[slotIndex].size()) {
            inputs[slotIndex].resize(arrayIndex + 1, nullptr);
        }
        inputs[slotIndex][arrayIndex] = resource;
    }
}

void NodeInstance::SetOutput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource) {
    if (slotIndex < outputs.size()) {
        if (arrayIndex >= outputs[slotIndex].size()) {
            outputs[slotIndex].resize(arrayIndex + 1, nullptr);
        }
        outputs[slotIndex][arrayIndex] = resource;
    }
}

size_t NodeInstance::GetInputCount(uint32_t slotIndex) const {
    if (slotIndex < inputs.size()) {
        return inputs[slotIndex].size();
    }
    return 0;
}

size_t NodeInstance::GetOutputCount(uint32_t slotIndex) const {
    if (slotIndex < outputs.size()) {
        return outputs[slotIndex].size();
    }
    return 0;
}

void NodeInstance::SetParameter(const std::string& name, const ParamTypeValue& value) {
    parameters[name] = value;
    
    // Invalidate cache when parameters change
    cacheKey = 0;
}

const ParamTypeValue* NodeInstance::GetParameter(const std::string& name) const {
    auto it = parameters.find(name);
    if (it != parameters.end()) {
        return &it->second;
    }
    return nullptr;
}

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

void NodeInstance::UpdatePerformanceStats(uint64_t executionTimeNs, uint64_t cpuTimeNs) {
    performanceStats.executionTimeNs = executionTimeNs;
    performanceStats.cpuTimeNs = cpuTimeNs;
    performanceStats.executionCount++;
    
    // Calculate running average
    float currentMs = executionTimeNs / 1000000.0f;
    if (performanceStats.executionCount == 1) {
        performanceStats.averageExecutionTimeMs = currentMs;
    } else {
        // Exponential moving average with alpha = 0.1
        performanceStats.averageExecutionTimeMs = 
            performanceStats.averageExecutionTimeMs * 0.9f + currentMs * 0.1f;
    }
}

uint64_t NodeInstance::ComputeCacheKey() const {
    // Simple hash combining type, parameters, and resource descriptions
    // This is a simplified version - production code would use proper hashing
    
    std::hash<std::string> hasher;
    uint64_t hash = hasher(instanceName);
    
    // Hash type ID
    hash ^= static_cast<uint64_t>(GetTypeId()) << 1;
    
    // Hash parameters
    for (const auto& [name, value] : parameters) {
        hash ^= hasher(name) << 2;
        
        // Hash parameter value based on type
        std::visit([&hash](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>) {
                hash ^= std::hash<std::string>{}(val);
            } else if constexpr (std::is_arithmetic_v<T>) {
                hash ^= std::hash<T>{}(val);
            }
        }, value);
    }
    
    // Hash input resource descriptors (iterate through 2D vector)
    for (const auto& inputSlot : inputs) {
        for (const auto* input : inputSlot) {
            if (input) {
                // Try to get image descriptor for hashing
                if (const auto* imgDesc = input->GetDescriptor<ImageDescriptor>()) {
                    hash ^= static_cast<uint64_t>(imgDesc->format) << 3;
                    hash ^= (static_cast<uint64_t>(imgDesc->width) << 4) |
                            (static_cast<uint64_t>(imgDesc->height) << 5);
                }
                // Could add more descriptor types if needed for better hash distribution
            }
        }
    }

    return hash;
}

#ifdef _DEBUG
void NodeInstance::RegisterToParentLogger(Logger* parentLogger)
{
    if (parentLogger) {
        parentLogger->AddChild(nodeLogger.get());
    }
}
void NodeInstance::DeregisterFromParentLogger(Logger* parentLogger)
{
    if (parentLogger) {
        parentLogger->RemoveChild(nodeLogger.get());
    }
}
#endif

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
    // Calculate input memory footprint from descriptors
    inputMemoryFootprint = 0;
    for (const auto& inputSlot : inputs) {
        for (const auto* input : inputSlot) {
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
    // For now, simple implementation: default to 1 task for all nodes
    // In future, this will analyze slot configuration:
    // - If all slots are NodeLevel: return 1
    // - If there are TaskLevel/ParameterizedInput slots: return array length
    // - If there are InstanceLevel slots: return instance count (async parallelism)

    // TODO: Query node type's slot metadata to detect TaskLevel/ParameterizedInput slots
    // For Phase F, all current nodes use NodeLevel, so return 1
    return 1;
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
