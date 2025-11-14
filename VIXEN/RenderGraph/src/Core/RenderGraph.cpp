#include "Core/RenderGraph.h"
#include "Core/IGraphCompilable.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/PresentNode.h"
#include "VulkanResources/VulkanDevice.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

// Logging macros for RenderGraph (uses mainLogger instead of nodeLogger)
#define GRAPH_LOG_DEBUG(msg) do { if (mainLogger) mainLogger->Debug(msg); } while(0)
#define GRAPH_LOG_INFO(msg) do { if (mainLogger) mainLogger->Info(msg); } while(0)
#define GRAPH_LOG_WARNING(msg) do { if (mainLogger) mainLogger->Warning(msg); } while(0)
#define GRAPH_LOG_ERROR(msg) do { if (mainLogger) mainLogger->Error(msg); } while(0)

namespace Vixen::RenderGraph {

RenderGraph::RenderGraph(
    NodeTypeRegistry* registry,
    EventBus::MessageBus* messageBus,
    Logger* mainLogger,
    CashSystem::MainCacher* mainCacher
)
    : typeRegistry(registry)
    , messageBus(messageBus)
    , mainCacher(mainCacher)
{
    if (!registry) {
        throw std::runtime_error("Node type registry cannot be null");
    }

    if (mainLogger) {
        this->mainLogger = mainLogger;
    }

    // Subscribe to cleanup events if bus provided
    if (messageBus) {
        cleanupEventSubscription = messageBus->Subscribe(
            EventTypes::CleanupRequestedMessage::TYPE,
            [this](const EventBus::BaseEventMessage& msg) {
                auto& cleanupMsg = static_cast<const EventTypes::CleanupRequestedMessage&>(msg);
                this->HandleCleanupRequest(cleanupMsg);
                return true;
            }
        );

        // Subscribe to window close event for graceful shutdown
        windowCloseSubscription = messageBus->Subscribe(
            EventBus::WindowCloseEvent::TYPE,
            [this](const EventBus::BaseEventMessage& msg) {
                this->HandleWindowClose();
                return true;
            }
        );

        // Subscribe to render pause events
        renderPauseSubscription = messageBus->Subscribe(
            EventTypes::RenderPauseEvent::TYPE,
            [this](const EventBus::BaseEventMessage& msg) {
                auto& pauseMsg = static_cast<const EventTypes::RenderPauseEvent&>(msg);
                this->HandleRenderPause(pauseMsg);
                return true;
            }
        );

        // Subscribe to window resize events
        windowResizeSubscription = messageBus->Subscribe(
            EventTypes::WindowResizedMessage::TYPE,
            [this](const EventBus::BaseEventMessage& msg) {
                auto& resizeMsg = static_cast<const EventTypes::WindowResizedMessage&>(msg);
                this->HandleWindowResize(resizeMsg);
                return true;
            }
        );

        // Subscribe to window state change events (minimize/maximize/restore)
        windowStateSubscription = messageBus->Subscribe(
            EventBus::WindowStateChangeEvent::TYPE,
            [this](const EventBus::BaseEventMessage& msg) {
                auto& stateMsg = static_cast<const EventBus::WindowStateChangeEvent&>(msg);
                this->HandleWindowStateChange(stateMsg);
                return true;
            }
        );

        // Subscribe to device sync events
        deviceSyncSubscription = messageBus->Subscribe(
            EventTypes::DeviceSyncRequestedMessage::TYPE,
            [this](const EventBus::BaseEventMessage& msg) {
                auto& syncMsg = static_cast<const EventTypes::DeviceSyncRequestedMessage&>(msg);
                this->HandleDeviceSyncRequest(syncMsg);
                return true;
            }
        );
    }
}

RenderGraph::~RenderGraph() {
    // Note: Device-dependent cache cleanup happens in DeviceNode::CleanupImpl()
    // Only cleanup global (device-independent) caches here
    if (mainCacher) {
        mainCacher->CleanupGlobalCaches();
    }

    // Unsubscribe from events
    if (messageBus) {
        if (cleanupEventSubscription != 0) {
            messageBus->Unsubscribe(cleanupEventSubscription);
        }
        if (renderPauseSubscription != 0) {
            messageBus->Unsubscribe(renderPauseSubscription);
        }
        if (windowResizeSubscription != 0) {
            messageBus->Unsubscribe(windowResizeSubscription);
        }
        if (windowStateSubscription != 0) {
            messageBus->Unsubscribe(windowStateSubscription);
        }
        if (deviceSyncSubscription != 0) {
            messageBus->Unsubscribe(deviceSyncSubscription);
        }
    }

    // Flush deferred destructions before cleanup
    deferredDestruction.Flush();

    Clear();
}

NodeHandle RenderGraph::AddNode(
    const std::string& typeName,
    const std::string& instanceName
) {
    // Check if instance name already exists
    if (nameToHandle.find(instanceName) != nameToHandle.end()) {
        throw std::runtime_error("Instance name already exists: " + instanceName);
    }

    // Get node type
    NodeType* type = typeRegistry->GetNodeType(typeName);
    if (!type) {
        throw std::runtime_error("Unknown node type: " + typeName);
    }

    // Check instancing limits
    NodeTypeId typeId = type->GetTypeId();
    uint32_t currentCount = GetInstanceCount(typeId);
    uint32_t maxInstances = type->GetMaxInstances();
    if (maxInstances > 0 && currentCount >= maxInstances) {
        throw std::runtime_error("Max instance count reached for type: " + typeName);
    }

    // Create instance
    auto instance = type->CreateInstance(instanceName);
    if (!instance) {
        throw std::runtime_error("Failed to create instance for type: " + typeName);
    }

    // Add to graph
    uint32_t index = static_cast<uint32_t>(instances.size());
    NodeHandle handle = CreateHandle(index);

    instances.push_back(std::move(instance));
    nameToHandle[instanceName] = handle;
    instancesByType[typeId].push_back(instances[index].get());

    // Set handle and owning graph pointer
    instances[index]->SetHandle(handle);
    instances[index]->SetOwningGraph(this);

    // Inject MessageBus for event publishing/subscription
    instances[index]->SetMessageBus(messageBus);

    // Attach node to parent logger (loggers disabled by default)
    instances[index]->RegisterToParentLogger(mainLogger);

    // Add to topology
    topology.AddNode(instances[index].get());

    // Mark as needing compilation
    isCompiled = false;

    return handle;
}

NodeHandle RenderGraph::AddNode(NodeTypeId typeId, const std::string& instanceName) {
    NodeType* type = typeRegistry->GetNodeType(typeId);
    if (!type) {
        throw std::runtime_error("Unknown node type ID");
    }
    return AddNode(type->GetTypeName(), instanceName);
}

void RenderGraph::ConnectNodes(
    NodeHandle from,
    uint32_t outputIdx,
    NodeHandle to,
    uint32_t inputIdx
) {
    NodeInstance* fromNode = GetInstanceInternal(from);
    NodeInstance* toNode = GetInstanceInternal(to);

    if (!fromNode || !toNode) {
        throw std::runtime_error("Invalid node handle");
    }

    GRAPH_LOG_INFO("[RenderGraph::ConnectNodes] Connecting " + fromNode->GetInstanceName() +
                  "[" + std::to_string(outputIdx) + "] -> " + toNode->GetInstanceName() +
                  "[" + std::to_string(inputIdx) + "]");

    // Validate indices
    NodeType* fromType = fromNode->GetNodeType();
    NodeType* toType = toNode->GetNodeType();

    if (outputIdx >= fromType->GetOutputCount()) {
        std::string error = "Invalid output index " + std::to_string(outputIdx) + 
                          " (max: " + std::to_string(fromType->GetOutputCount()) + 
                          ") for node: " + fromNode->GetInstanceName();
        throw std::runtime_error(error);
    }

    if (inputIdx >= toType->GetInputCount()) {
        std::string error = "Invalid input index " + std::to_string(inputIdx) + 
                          " (max: " + std::to_string(toType->GetInputCount()) + 
                          ") for node: " + toNode->GetInstanceName();
        throw std::runtime_error(error);
    }

    // Connect all array elements from output to input
    // Check if the source output has multiple array elements
    size_t outputArraySize = fromNode->GetOutputCount(outputIdx);

    // If no outputs set yet, assume scalar (size 1)
    if (outputArraySize == 0) {
        outputArraySize = 1;
    }

    // Connect each array element
    for (size_t arrayIdx = 0; arrayIdx < outputArraySize; arrayIdx++) {
        Resource* resource = fromNode->GetOutput(outputIdx, static_cast<uint32_t>(arrayIdx));
        if (!resource) {
            resource = CreateResourceForOutput(fromNode, outputIdx);
            fromNode->SetOutput(outputIdx, static_cast<uint32_t>(arrayIdx), resource);
        }

        // Register resource producer for dependency tracking
        dependencyTracker.RegisterResourceProducer(resource, fromNode, outputIdx);

        // Connect to input at same array index
        toNode->SetInput(inputIdx, static_cast<uint32_t>(arrayIdx), resource);
    }

    // Add dependency (once per connection, not per array element)
    toNode->AddDependency(fromNode);
    GRAPH_LOG_INFO("[RenderGraph::ConnectNodes] Added dependency: " + toNode->GetInstanceName() +
                  " depends on " + fromNode->GetInstanceName());

    // Add edge to topology
    GraphEdge edge;
    edge.source = fromNode;
    edge.sourceOutputIndex = outputIdx;
    edge.target = toNode;
    edge.targetInputIndex = inputIdx;
    topology.AddEdge(edge);
    GRAPH_LOG_INFO("[RenderGraph::ConnectNodes] Added topology edge");

    // Mark as needing compilation
    isCompiled = false;
}

void RenderGraph::RemoveNode(NodeHandle handle) {
    NodeInstance* node = GetInstanceInternal(handle);
    if (!node) {
        return;
    }

    // Remove from topology
    topology.RemoveNode(node);

    // Remove from name map
    nameToHandle.erase(node->GetInstanceName());

    // Remove from type map
    NodeTypeId typeId = node->GetTypeId();
    auto& typeInstances = instancesByType[typeId];
    typeInstances.erase(
        std::remove(typeInstances.begin(), typeInstances.end(), node),
        typeInstances.end()
    );

    // Remove from instances array
    // Note: This invalidates handles, so this is a destructive operation
    instances.erase(instances.begin() + handle.index);

    // Rebuild handle mappings
    nameToHandle.clear();
    for (size_t i = 0; i < instances.size(); ++i) {
        nameToHandle[instances[i]->GetInstanceName()] = CreateHandle(static_cast<uint32_t>(i));
    }

    isCompiled = false;
}

void RenderGraph::Clear() {
    // Execute cleanup callbacks in dependency order BEFORE clearing instances
    ExecuteCleanup();

    instances.clear();
    resources.clear();  // Destroys all graph-owned resources (centralized cleanup)
    nameToHandle.clear();
    instancesByType.clear();
    executionOrder.clear();
    topology.Clear();
    dependencyTracker.Clear();
    // usedDevices.clear();
    // usedDevices.push_back(primaryDevice);
    isCompiled = false;
}

void RenderGraph::HandleWindowClose() {
    GRAPH_LOG_INFO("[RenderGraph] Received WindowCloseEvent - initiating graceful cleanup");

    // Wait for GPU to finish all work before cleanup
    for (const auto& inst : instances) {
        if (inst) {
            auto* device = inst->GetDevice();
            if (device && device->device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device->device);
                break;  // Only need to wait once per unique device
            }
        }
    }

    // Save persistent caches BEFORE cleanup destroys resources (async for responsiveness)
    if (mainCacher) {
        std::filesystem::path cacheDir = "cache";
        GRAPH_LOG_INFO("[RenderGraph] Saving persistent caches asynchronously to: " + cacheDir.string());

        auto saveFuture = mainCacher->SaveAllAsync(cacheDir);

        // Wait for async save to complete before cleanup
        bool saveSuccess = saveFuture.get();

        if (saveSuccess) {
            GRAPH_LOG_INFO("[RenderGraph] Persistent caches saved successfully");
        } else {
            GRAPH_LOG_WARNING("[RenderGraph] Warning: Some caches failed to save");
        }
    }

    // Execute cleanup (destroys all Vulkan resources)
    ExecuteCleanup();

    GRAPH_LOG_INFO("[RenderGraph] Cleanup complete");

    // Acknowledge shutdown to application
    if (messageBus) {
        messageBus->Publish(
            std::make_unique<EventBus::ShutdownAckEvent>(0, "RenderGraph")
        );
    }
}

void RenderGraph::ExecuteCleanup() {
    GRAPH_LOG_INFO("[RenderGraph::ExecuteCleanup] Executing cleanup callbacks...");

    // Check if cleanup has already been executed
    if (cleanupStack.GetNodeCount() == 0) {
        GRAPH_LOG_INFO("[RenderGraph::ExecuteCleanup] Cleanup already executed, skipping");
        return;
    }

    // Ensure devices are idle before running cleanup
    WaitForGraphDevicesIdle();

    cleanupStack.ExecuteAll();
    GRAPH_LOG_INFO("[RenderGraph::ExecuteCleanup] Cleanup complete");
}

void RenderGraph::RegisterPostNodeCompileCallback(PostNodeCompileCallback callback) {
    postNodeCompileCallbacks.push_back(std::move(callback));
}

void RenderGraph::Compile() {
    GRAPH_LOG_INFO("[RenderGraph::Compile] Starting compilation...");

    // Validation
    GRAPH_LOG_INFO("[RenderGraph::Compile] Phase: Validation...");
    std::string errorMessage;
    if (!Validate(errorMessage)) {
        throw std::runtime_error("Graph validation failed: " + errorMessage);
    }

    // Hook: PreTopologyBuild
    lifecycleHooks.ExecuteGraphHooks(GraphLifecyclePhase::PreTopologyBuild, this);

    // Phase 1: Propagate device affinity
    // PropagateDeviceAffinity();  // Removed - single device system

    // Phase 2: Analyze dependencies and build execution order
    GRAPH_LOG_INFO("[RenderGraph::Compile] Phase: AnalyzeDependencies...");
    AnalyzeDependencies();

    // Hook: PostTopologyBuild
    lifecycleHooks.ExecuteGraphHooks(GraphLifecyclePhase::PostTopologyBuild, this);

    // Hook: PreExecutionOrder
    lifecycleHooks.ExecuteGraphHooks(GraphLifecyclePhase::PreExecutionOrder, this);

    // Phase 3: Allocate resources
    GRAPH_LOG_INFO("[RenderGraph::Compile] Phase: AllocateResources...");
    AllocateResources();

    // Hook: PostExecutionOrder (after resource allocation, before compilation)
    lifecycleHooks.ExecuteGraphHooks(GraphLifecyclePhase::PostExecutionOrder, this);

    // Hook: PreCompilation
    lifecycleHooks.ExecuteGraphHooks(GraphLifecyclePhase::PreCompilation, this);

    // Phase 4: Generate pipelines
    GRAPH_LOG_INFO("[RenderGraph::Compile] Phase: GeneratePipelines...");
    GeneratePipelines();

    // Phase 5: Build final execution order
    GRAPH_LOG_INFO("[RenderGraph::Compile] Phase: BuildExecutionOrder...");
    BuildExecutionOrder();

    // Hook: PostCompilation
    lifecycleHooks.ExecuteGraphHooks(GraphLifecyclePhase::PostCompilation, this);

    GRAPH_LOG_INFO("[RenderGraph::Compile] Compilation complete!");
    isCompiled = true;
}

void RenderGraph::Execute(VkCommandBuffer commandBuffer) {
    if (!isCompiled) {
        throw std::runtime_error("Graph must be compiled before execution");
    }

    // Phase 0.4: Update loop states
    double frameTime = frameTimer.GetDeltaTime();
    loopManager.SetCurrentFrame(globalFrameIndex);
    loopManager.UpdateLoops(frameTime);
    globalFrameIndex++;

    // Phase 0.4: Propagate loop references through AUTO_LOOP_IN/OUT connections
    for (const auto& edge : topology.GetEdges()) {
        if (edge.sourceOutputIndex == NodeInstance::AUTO_LOOP_OUT_SLOT &&
            edge.targetInputIndex == NodeInstance::AUTO_LOOP_IN_SLOT) {

            const LoopReference* loopRef = edge.source->GetLoopOutput();
            edge.target->SetLoopInput(loopRef);
        }
    }

    // Execute nodes in order (now with loop gating via ShouldExecuteThisFrame)
    for (NodeInstance* node : executionOrder) {
        if (node->GetState() == NodeState::Ready ||
            node->GetState() == NodeState::Compiled ||
            node->GetState() == NodeState::Complete) {  // Execute completed nodes again each frame

            // Phase 0.4: Check if node should execute this frame (loop gating)
            if (node->ShouldExecuteThisFrame()) {
                node->SetState(NodeState::Executing);
                node->Execute();
                node->SetState(NodeState::Complete);
            }
        }
    }
}

VkResult RenderGraph::RenderFrame() {
    if (!isCompiled) {
        throw std::runtime_error("Graph must be compiled before rendering");
    }

    // Event processing now handled in application's Update() phase
    // This allows updating without rendering and different frame rates

    // Check if rendering should be paused (e.g., during swapchain recreation or window minimization)
    if (renderPaused) {
        // Continue processing events and updates but skip rendering
        return VK_SUCCESS;
    }

    // The render graph orchestrates the frame by calling specialized nodes:
    //
    // 1. SwapChainNode - Acquires next swapchain image (internally manages semaphores)
    // 2. GeometryRenderNode - Records draw commands (internally manages command buffers)
    // 3. PresentNode - Presents to swapchain (internally handles queue submission)
    //
    // Each node owns and manages its own Vulkan resources.
    // The graph just calls Execute() on each node in dependency order.

    // TODO Phase 1: For minimal MVP, manually wire SwapChainNode -> PresentNode
    // In future phases, this will be done automatically via the dependency graph
    Vixen::RenderGraph::SwapChainNode* swapChainNode = nullptr;
    Vixen::RenderGraph::PresentNode* presentNode = nullptr;

    // Find SwapChain and Present nodes
    for (NodeInstance* node : executionOrder) {
        if (node->GetNodeType()->GetTypeName() == "SwapChain") {
            swapChainNode = static_cast<Vixen::RenderGraph::SwapChainNode*>(node);
        }
        if (node->GetNodeType()->GetTypeName() == "Present") {
            presentNode = static_cast<Vixen::RenderGraph::PresentNode*>(node);
        }
    }

    // Execute all nodes in topological order
    // Nodes handle their own synchronization, command recording, and presentation
    for (NodeInstance* node : executionOrder) {
        if (node->GetState() == NodeState::Ready ||
            node->GetState() == NodeState::Compiled ||
            node->GetState() == NodeState::Complete) {  // Execute completed nodes again each frame

            node->SetState(NodeState::Executing);

            // Pass VK_NULL_HANDLE - nodes manage their own command buffers
            node->Execute();

            node->SetState(NodeState::Complete);

            // Check if this node was marked for recompilation during execution
            if (node->HasDeferredRecompile()) {
                node->ClearDeferredRecompile();
                // Find the handle and mark as dirty
                for (size_t i = 0; i < instances.size(); ++i) {
                    if (instances[i].get() == node) {
                        MarkNodeNeedsRecompile({static_cast<uint32_t>(i)});
                        break;
                    }
                }
            }
        }
    }

    return VK_SUCCESS;
}

NodeInstance* RenderGraph::GetInstance(NodeHandle handle) {
    return GetInstanceInternal(handle);
}

const NodeInstance* RenderGraph::GetInstance(NodeHandle handle) const {
    if (handle.index < instances.size()) {
        return instances[handle.index].get();
    }
    return nullptr;
}

NodeInstance* RenderGraph::GetInstanceByName(const std::string& name) {
    auto it = nameToHandle.find(name);
    if (it != nameToHandle.end()) {
        return GetInstanceInternal(it->second);
    }
    return nullptr;
}

const NodeInstance* RenderGraph::GetInstanceByName(const std::string& name) const {
    auto it = nameToHandle.find(name);
    if (it != nameToHandle.end()) {
        if (it->second.index < instances.size()) {
            return instances[it->second.index].get();
        }
    }
    return nullptr;
}

std::vector<NodeInstance*> RenderGraph::GetInstancesOfType(NodeTypeId typeId) const {
    auto it = instancesByType.find(typeId);
    if (it != instancesByType.end()) {
        return it->second;
    }
    return {};
}

uint32_t RenderGraph::GetInstanceCount(NodeTypeId typeId) const {
    auto it = instancesByType.find(typeId);
    if (it != instancesByType.end()) {
        return static_cast<uint32_t>(it->second.size());
    }
    return 0;
}

bool RenderGraph::Validate(std::string& errorMessage) const {
    // Check topology
    if (!topology.ValidateGraph(errorMessage)) {
        return false;
    }

    // Check all nodes have required inputs
    for (const auto& instance : instances) {
        NodeType* type = instance->GetNodeType();
        const auto& inputSchema = type->GetInputSchema();

        for (size_t i = 0; i < inputSchema.size(); ++i) {
            // Check if slot has at least one resource (array index 0)
            if (!inputSchema[i].nullable && !instance->GetInput(static_cast<uint32_t>(i), 0)) {
                errorMessage = "Node " + instance->GetInstanceName() +
                             " missing required input at index " + std::to_string(i);
                return false;
            }
        }
    }

    // Phase C.3: Validate render pass compatibility between pipelines and framebuffers
    // Check GeometryRenderNode instances for compatible render passes
    for (const auto& instance : instances) {
        NodeType* type = instance->GetNodeType();
        if (type->GetTypeName() == "GeometryRender") {
            // GeometryRenderNode uses RENDER_PASS (from PipelineNode) and FRAMEBUFFERS (from FramebufferNode)
            // Pipeline's render pass must be compatible with framebuffer's render pass

            // Try to get render pass from input (may be null if not connected)
            Resource* renderPassRes = instance->GetInput(0, 0);  // RENDER_PASS is input 0
            Resource* framebufferRes = instance->GetInput(2, 0);  // FRAMEBUFFERS is input 2

            if (renderPassRes && framebufferRes) {
                // Both resources exist - validate compatibility
                // Note: Vulkan render pass compatibility is complex (attachment counts, formats, etc.)
                // For now, we just ensure both exist. Full validation would require:
                // 1. Extracting VkRenderPass from pipeline
                // 2. Extracting VkRenderPass from framebuffer
                // 3. Checking compatibility via format/attachment/subpass rules
                // This is a placeholder for future comprehensive validation

                // Placeholder: Just ensure resources are not null
                // Full implementation would call vkGetRenderPassCreateInfo equivalents
            }
        }
    }

    return true;
}

// ====== Private Methods ======

NodeHandle RenderGraph::CreateHandle(uint32_t index) const {
    NodeHandle handle;
    handle.index = index;
    return handle;
}

NodeInstance* RenderGraph::GetInstanceInternal(NodeHandle handle) {
    if (handle.index < instances.size()) {
        return instances[handle.index].get();
    }
    return nullptr;
}

void RenderGraph::WaitForGraphDevicesIdle(const std::vector<NodeInstance*>& instancesToCheck) {
    // Prefer waiting on device(s) created by DeviceNode instances to avoid
    // accidentally dereferencing dangling or uninitialized VulkanDevice wrappers
    // that some nodes may hold as non-owning pointers.
    std::unordered_set<VkDevice> devicesToWait;

    // First, collect VulkanDevice pointers from explicit Device nodes (trusted owners)
    for (const auto& instPtrUp : instances) {
        if (!instPtrUp) continue;
        NodeInstance* instPtr = instPtrUp.get();
        if (!instPtr) continue;
        NodeType* type = instPtr->GetNodeType();
        if (type && type->GetTypeName() == "Device") {
            auto* vdev = instPtr->GetDevice();
            if (vdev && vdev->device != VK_NULL_HANDLE) {
                devicesToWait.insert(vdev->device);
            }
        }
    }

    // If no Device nodes found, fall back to scanning the provided instances list
    if (devicesToWait.empty()) {
        std::vector<NodeInstance*> nodesToCheck;
        if (instancesToCheck.empty()) {
            nodesToCheck.reserve(instances.size());
            for (const auto& inst : instances)
                nodesToCheck.push_back(inst.get());
        }
        else {
            nodesToCheck = instancesToCheck;
        }

        for (auto* instPtr : nodesToCheck) {
            if (!instPtr) continue;
            auto* vdev = instPtr->GetDevice();
            if (vdev && vdev->device != VK_NULL_HANDLE) {
                devicesToWait.insert(vdev->device);
            }
        }
    }

    WaitForDevicesIdle(devicesToWait);
}

void RenderGraph::WaitForDevicesIdle(const std::unordered_set<VkDevice>& devices) {
    for (VkDevice dev : devices) {
        if (dev != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(dev);
        }
    }
}

Resource* RenderGraph::CreateResourceForOutput(NodeInstance* node, uint32_t outputIndex) {
    NodeType* type = node->GetNodeType();
    const auto& outputSchema = type->GetOutputSchema();
    
    if (outputIndex >= outputSchema.size()) {
        return nullptr;
    }

    const ResourceDescriptor& resourceDesc = outputSchema[outputIndex];

    // Create empty resource - nodes will populate it during Compile()
    // The variant system allows nodes to set any registered type via SetHandle<T>()
    std::unique_ptr<Resource> resource = std::make_unique<Resource>();
    resource->SetLifetime(resourceDesc.lifetime);
    
    // Store in graph's resource vector for lifetime management
    // The graph owns all resources; nodes access them via raw pointers
    Resource* resourcePtr = resource.get();
    resources.push_back(std::move(resource));
    
    return resourcePtr;
}

void RenderGraph::AnalyzeDependencies() {
    // Topological sort gives us execution order
    executionOrder = topology.TopologicalSort();
    
    // Assign execution order indices
    for (size_t i = 0; i < executionOrder.size(); ++i) {
        executionOrder[i]->SetExecutionOrder(static_cast<uint32_t>(i));
    }
}

void RenderGraph::AllocateResources() {
    // NOTE: Resource allocation is handled by individual nodes during Compile()
    // This function is reserved for future centralized resource allocation features:
    // - Memory aliasing (reusing GPU memory for non-overlapping resources)
    // - Resource pooling (reducing allocation churn)
    // - Batch allocation (single large allocation for multiple resources)
    
    // VkDevice device = primaryDevice->device;  // Removed - nodes access device directly
    
    // TODO: Implement centralized resource allocation with aliasing
    /*
    for (auto& resource : resources) {
        if (!resource->IsAllocated()) {
            if (resource->GetType() == ResourceType::Image ||
                resource->GetType() == ResourceType::CubeMap ||
                resource->GetType() == ResourceType::Image3D ||
                resource->GetType() == ResourceType::StorageImage) {
                
                const ImageDescription* desc = resource->GetDescription<ImageDescription>();
                if (desc) {
                    // Will need physical device - this is simplified
                    // resource->AllocateImage(device, *desc);
                }
            }
            else if (resource->GetType() == ResourceType::Buffer) {
                const BufferDescription* desc = resource->GetDescription<BufferDescription>();
                if (desc) {
                    // resource->AllocateBuffer(device, *desc);
                }
            }
        }
    }
    */
    
    // Placeholder: Nodes handle their own resource allocation in Compile()
}

void RenderGraph::GeneratePipelines() {
    // Compile nodes in dependency order (executionOrder is topologically sorted)
    GRAPH_LOG_INFO("[GeneratePipelines] Total instances to compile: " + std::to_string(executionOrder.size()));
    for (size_t i = 0; i < executionOrder.size(); ++i) {
        GRAPH_LOG_DEBUG("[GeneratePipelines]   " + std::to_string(i) + ": " + executionOrder[i]->GetInstanceName());
    }

    // Phase 1: GraphCompileSetup - discover dynamic slots before deferred connections
    GRAPH_LOG_INFO("[GeneratePipelines] Phase 1: GraphCompileSetup (discovering dynamic slots)...");
    for (NodeInstance* instance : executionOrder) {
        if (instance->GetState() == NodeState::Compiled) {
            continue;  // Skip already-compiled nodes
        }

        // Check if node implements IGraphCompilable
        if (auto* compilable = dynamic_cast<IGraphCompilable*>(instance)) {
            GRAPH_LOG_DEBUG("[GeneratePipelines] Calling GraphCompileSetup() on: " + instance->GetInstanceName());
            compilable->GraphCompileSetup();
        }
    }

    // Phase 2: ProcessDeferredConnections - resolve ConnectVariadic, ConnectMember, etc.
    // TODO: Implement deferred connection queue
    GRAPH_LOG_INFO("[GeneratePipelines] Phase 2: ProcessDeferredConnections (future implementation)");

    // Phase 3: Setup all nodes first, then compile all nodes
    // This ensures PostSetup hooks can populate resources before any Compile phase begins
    GRAPH_LOG_INFO("[GeneratePipelines] Phase 3a: Setup all nodes...");

    for (NodeInstance* instance : executionOrder) {
        // Skip nodes that are already compiled (e.g., pre-compiled device node)
        if (instance->GetState() == NodeState::Compiled) {
            GRAPH_LOG_DEBUG("[GeneratePipelines] Skipping already-compiled node: " + instance->GetInstanceName());
            continue;
        }

        GRAPH_LOG_DEBUG("[GeneratePipelines] Calling Setup() on node: " + instance->GetInstanceName());
        instance->Setup();
    }

    // Load caches after all Setup() calls but before first Compile()
    // This ensures all cachers are registered and all PostSetup hooks have executed
    GRAPH_LOG_INFO("[GeneratePipelines] Loading persistent caches...");
    if (mainCacher) {
        std::filesystem::path cacheDir = "cache";
        if (std::filesystem::exists(cacheDir)) {
            GRAPH_LOG_INFO("[RenderGraph] Loading persistent caches from: " + cacheDir.string());
            auto loadFuture = mainCacher->LoadAllAsync(cacheDir);

            bool loadSuccess = loadFuture.get();
            if (loadSuccess) {
                GRAPH_LOG_INFO("[RenderGraph] Persistent caches loaded successfully");
            } else {
                GRAPH_LOG_WARNING("[RenderGraph] WARNING: Some caches failed to load (will recreate)");
            }
        } else {
            GRAPH_LOG_INFO("[RenderGraph] No existing caches found (first run)");
        }
    }

    GRAPH_LOG_INFO("[GeneratePipelines] Phase 3b: Compile all nodes...");

    for (NodeInstance* instance : executionOrder) {
        // Skip nodes that are already compiled
        if (instance->GetState() == NodeState::Compiled) {
            continue;
        }

        GRAPH_LOG_DEBUG("[GeneratePipelines] Calling Compile() on node: " + instance->GetInstanceName());
        instance->Compile();

        GRAPH_LOG_DEBUG("[GeneratePipelines] Node compiled successfully: " + instance->GetInstanceName());

        // Execute post-compile callbacks immediately after compilation
        // This ensures extracted values are available before dependent nodes compile
        for (auto& callback : postNodeCompileCallbacks) {
            callback(instance);
        }

        instance->SetState(NodeState::Compiled);
    }
}

void RenderGraph::BuildExecutionOrder() {
    // Already built in AnalyzeDependencies
    // This phase would handle additional optimizations like:
    // - Batching compatible nodes
    // - Parallel execution groups
    // - GPU timeline optimization
}

void RenderGraph::ComputeDependentCounts() {
    dependentCounts.clear();
    
    // Initialize all nodes with zero count
    for (const auto& instance : instances) {
        dependentCounts[instance.get()] = 0;
    }
    
    // Count how many nodes depend on each node
    for (const auto& instance : instances) {
        for (NodeInstance* dependency : instance->GetDependencies()) {
            dependentCounts[dependency]++;
        }
    }
}

void RenderGraph::RecursiveCleanup(NodeInstance* node, std::set<NodeInstance*>& cleaned) {
    if (!node || cleaned.count(node) > 0) {
        return; // Already cleaned or invalid
    }

    GRAPH_LOG_DEBUG("[RecursiveCleanup] Checking node: " + node->GetInstanceName());

    // First, recursively clean dependencies that would become orphaned
    for (NodeInstance* dependency : node->GetDependencies()) {
        auto it = dependentCounts.find(dependency);
        if (it != dependentCounts.end()) {
            GRAPH_LOG_DEBUG("[RecursiveCleanup]   Dependency " + dependency->GetInstanceName() +
                          " has refCount=" + std::to_string(it->second));

            // Check if this dependency has refCount == 1 (only current node uses it)
            if (it->second == 1) {
                GRAPH_LOG_DEBUG("[RecursiveCleanup]   -> Will be orphaned, cleaning recursively");
                RecursiveCleanup(dependency, cleaned);
            } else {
                GRAPH_LOG_DEBUG("[RecursiveCleanup]   -> Still has other users, keeping");
            }

            // Decrement count after checking (simulate removing this edge)
            it->second--;
        }
    }

    // Now clean this node
    GRAPH_LOG_DEBUG("[RecursiveCleanup] Cleaning node: " + node->GetInstanceName());

    // Execute cleanup callback via CleanupStack using handle
    cleanupStack.ExecuteFrom(node->GetHandle());
    
    // Mark as cleaned
    cleaned.insert(node);
}

std::string RenderGraph::GetDeviceCleanupNodeName() const {
    // Find first instance with type name "Device" and return its cleanup node name
    for (const auto& instPtr : instances) {
        if (!instPtr) continue;
        NodeType* t = instPtr->GetNodeType();
        if (t && t->GetTypeName() == "Device") {
            return instPtr->GetInstanceName() + std::string("_Cleanup");
        }
    }
    // Legacy fallback used by older code
    return std::string("DeviceNode_Cleanup");
}

size_t RenderGraph::CleanupSubgraph(const std::string& rootNodeName) {
    NodeInstance* rootNode = GetInstanceByName(rootNodeName);
    if (!rootNode) {
        GRAPH_LOG_ERROR("[CleanupSubgraph] Node not found: " + rootNodeName);
        return 0;
    }

    GRAPH_LOG_INFO("[CleanupSubgraph] Starting partial cleanup from: " + rootNodeName);
    
    // Compute which nodes would be cleaned in this subgraph (dry-run)
    std::vector<std::string> scope = GetCleanupScope(rootNodeName);

    // Build list of NodeInstance pointers for the computed subgraph scope
    std::vector<NodeInstance*> instancesToWait;
    instancesToWait.reserve(scope.size());
    for (const std::string& nodeName : scope) {
        NodeInstance* inst = GetInstanceByName(nodeName);
        if (!inst) continue;
        instancesToWait.push_back(inst);
    }

    // Wait only on devices that are used by the subgraph about to be cleaned
    WaitForGraphDevicesIdle(instancesToWait);

    // Compute reference counts
    ComputeDependentCounts();
    
    // Track cleaned nodes
    std::set<NodeInstance*> cleaned;
    
    // Recursively clean
    RecursiveCleanup(rootNode, cleaned);

    GRAPH_LOG_INFO("[CleanupSubgraph] Cleaned " + std::to_string(cleaned.size()) + " nodes");

    return cleaned.size();
}

std::vector<std::string> RenderGraph::GetCleanupScope(const std::string& rootNodeName) const {
    const NodeInstance* rootNode = GetInstanceByName(rootNodeName);
    if (!rootNode) {
        return {};
    }
    
    // Create temporary copy of dependent counts for dry-run
    std::unordered_map<const NodeInstance*, size_t> tempCounts;
    for (const auto& instance : instances) {
        tempCounts[instance.get()] = 0;
    }
    for (const auto& instance : instances) {
        for (NodeInstance* dependency : instance->GetDependencies()) {
            tempCounts[dependency]++;
        }
    }
    
    // Simulate cleanup recursively
    std::vector<std::string> scope;
    std::set<const NodeInstance*> visited;
    
    std::function<void(const NodeInstance*)> simulate = [&](const NodeInstance* node) {
        if (!node || visited.count(node) > 0) {
            return;
        }
        
        visited.insert(node);
        
        // Check dependencies
        for (NodeInstance* dep : node->GetDependencies()) {
            auto it = tempCounts.find(dep);
            if (it != tempCounts.end() && it->second == 1) {
                simulate(dep);
            }
            if (it != tempCounts.end()) {
                it->second--;
            }
        }
        
        // Add to scope
        scope.push_back(node->GetInstanceName());
    };
    
    simulate(rootNode);
    
    return scope;
}

size_t RenderGraph::CleanupByTag(const std::string& tag) {
    GRAPH_LOG_INFO("[CleanupByTag] Cleaning nodes with tag: " + tag);

    // Find all nodes with this tag
    std::vector<NodeInstance*> matchingNodes;
    for (const auto& instance : instances) {
        if (instance->HasTag(tag)) {
            matchingNodes.push_back(instance.get());
        }
    }

    if (matchingNodes.empty()) {
        GRAPH_LOG_INFO("[CleanupByTag] No nodes found with tag: " + tag);
        return 0;
    }
    
    // Ensure devices are idle before performing cleanup
    WaitForGraphDevicesIdle();

    // Compute reference counts
    ComputeDependentCounts();
    
    // Cleanup each matching node
    std::set<NodeInstance*> cleaned;
    for (NodeInstance* node : matchingNodes) {
        RecursiveCleanup(node, cleaned);
    }

    GRAPH_LOG_INFO("[CleanupByTag] Cleaned " + std::to_string(cleaned.size()) + " nodes");
    return cleaned.size();
}

size_t RenderGraph::CleanupByType(const std::string& typeName) {
    GRAPH_LOG_INFO("[CleanupByType] Cleaning nodes of type: " + typeName);

    // Find all nodes of this type
    std::vector<NodeInstance*> matchingNodes;
    for (const auto& instance : instances) {
        if (instance->GetNodeType()->GetTypeName() == typeName) {
            matchingNodes.push_back(instance.get());
        }
    }

    if (matchingNodes.empty()) {
        GRAPH_LOG_INFO("[CleanupByType] No nodes found of type: " + typeName);
        return 0;
    }
    
    // Ensure devices are idle before performing cleanup
    WaitForGraphDevicesIdle();

    // Compute reference counts
    ComputeDependentCounts();
    
    // Cleanup each matching node
    std::set<NodeInstance*> cleaned;
    for (NodeInstance* node : matchingNodes) {
        RecursiveCleanup(node, cleaned);
    }

    GRAPH_LOG_INFO("[CleanupByType] Cleaned " + std::to_string(cleaned.size()) + " nodes");
    return cleaned.size();
}

void RenderGraph::ProcessEvents() {
    if (!messageBus) {
        return;  // No event bus configured
    }

    // Process all queued events
    messageBus->ProcessMessages();
}

void RenderGraph::RecompileDirtyNodes() {
    if (dirtyNodes.empty()) {
        return;  // Nothing to recompile
    }

    // Defer recompilation if rendering is paused (e.g., during swapchain recreation)
    if (renderPaused) {
        GRAPH_LOG_INFO("[RenderGraph::RecompileDirtyNodes] Deferring recompilation - rendering paused");
        return;
    }

    // Log which nodes triggered recompilation
    GRAPH_LOG_INFO("[RenderGraph::RecompileDirtyNodes] ===== RECOMPILATION TRIGGERED =====");
    GRAPH_LOG_INFO("[RenderGraph::RecompileDirtyNodes] Dirty nodes count: " + std::to_string(dirtyNodes.size()));
    for (NodeHandle handle : dirtyNodes) {
        if (handle.IsValid() && handle.index < instances.size()) {
            NodeInstance* node = instances[handle.index].get();
            if (node) {
                GRAPH_LOG_INFO("[RenderGraph::RecompileDirtyNodes]   - Node '" + node->GetInstanceName() + "' marked dirty");
            }
        }
    }

    // Check if any dirty nodes are currently executing on GPU
    // If so, defer recompilation until they're complete
    std::vector<NodeHandle> deferredNodes;
    for (NodeHandle handle : dirtyNodes) {
        if (handle.IsValid() && handle.index < instances.size()) {
            NodeInstance* node = instances[handle.index].get();
            if (node && node->GetState() == NodeState::Executing) {
                deferredNodes.push_back(handle);
            }
        }
    }

    // Remove deferred nodes from dirty set (they'll be re-added when execution completes)
    for (NodeHandle handle : deferredNodes) {
        dirtyNodes.erase(handle);
    }

    if (dirtyNodes.empty()) {
        if (!deferredNodes.empty()) {
            GRAPH_LOG_INFO("[RenderGraph] Deferring recompilation of " + std::to_string(deferredNodes.size()) +
                         " nodes currently executing on GPU");
        }
        return;  // All dirty nodes are still executing
    }

    // Track if all nodes successfully recompiled
    bool allNodesSucceeded = true;

    // Track which nodes failed during this recompilation
    std::unordered_set<NodeInstance*> failedNodes;

    // Keep recompiling until there are no more dirty nodes
    // (recompiling a node may mark its dependents as dirty)
    while (!dirtyNodes.empty()) {
        GRAPH_LOG_INFO("[RenderGraph] Recompiling " + std::to_string(dirtyNodes.size()) + " dirty nodes");

        // Convert dirty handles to set of node pointers for fast lookup
        std::unordered_set<NodeInstance*> dirtyNodeSet;
        for (NodeHandle handle : dirtyNodes) {
            if (handle.IsValid() && handle.index < instances.size()) {
                NodeInstance* node = instances[handle.index].get();
                if (node) {
                    dirtyNodeSet.insert(node);
                }
            }
        }

        // Collect nodes to recompile in execution order (respects dependencies)
        std::vector<NodeInstance*> nodesToRecompile;
        for (NodeInstance* node : executionOrder) {
            if (dirtyNodeSet.count(node) > 0) {
                nodesToRecompile.push_back(node);
            }
        }

        // Clear dirty set before recompiling this batch
        dirtyNodes.clear();

        // Recompile each dirty node
        // Note: We skip vkDeviceWaitIdle during recompilation because:
        // 1. FrameSyncNode already handles frame-in-flight synchronization
        // 2. Node pointers may be stale during cleanup (accessing node->GetDevice() unsafe)
        // 3. Individual nodes handle their own device waits in CleanupImpl if needed

        for (NodeInstance* node : nodesToRecompile) {
            if (!node) continue;

            // Check if any input dependencies failed during this recompilation
            bool dependencyFailed = false;
            std::vector<NodeInstance*> dependencies = dependencyTracker.GetDependenciesForNode(node);
            for (NodeInstance* dep : dependencies) {
                if (failedNodes.count(dep) > 0) {
                    dependencyFailed = true;
                    GRAPH_LOG_WARNING("[RenderGraph] Skipping node '" + node->GetInstanceName() +
                                    "' - dependency '" + dep->GetInstanceName() + "' failed to recompile");
                    break;
                }
            }
    
            if (dependencyFailed) {
                // Mark this node as failed and re-add to dirty set
                failedNodes.insert(node);
                for (uint32_t i = 0; i < instances.size(); ++i) {
                    if (instances[i].get() == node) {
                        MarkNodeNeedsRecompile(CreateHandle(i));
                        break;
                    }
                }
                allNodesSucceeded = false;
                continue;
            }

            GRAPH_LOG_INFO("[RenderGraph] Recompiling node: " + node->GetInstanceName());
    
            try {
                // Call cleanup first (destroy old resources)
                node->Cleanup();
    
                // Ensure node has a chance to recreate transient objects (e.g., swapchain wrapper)
                // Some nodes may not have device inputs available immediately; if Setup/Compile
                // fails we'll catch and defer the recompilation to a later safe point.
                node->Setup();
    
                // Recompile (create new resources)
                // Reset per-input "used in compile" markers so the Compile() pass
                // can record which inputs are actually used during compilation.
                node->ResetInputsUsedInCompile();
                node->Compile();
                
                // Clear recompilation flag - node is now compiled
                node->ClearNeedsRecompile();

                // Reset cleanup flag so node can be cleaned up again on next recompilation
                node->ResetCleanupFlag();

                // Reset the CleanupStack's executed flag for this node
                cleanupStack.ResetExecuted(node->GetHandle());
    
                // Mark all dependent nodes (consumers of this node's outputs) as dirty
                // The cleanup stack tracks the dependency graph: provider -> dependents
                // We need to compile in the opposite direction: when a provider recompiles,
                // all its dependents need to recompile
                NodeHandle nodeHandle = node->GetHandle();
                std::unordered_set<NodeHandle> dependentHandles = cleanupStack.GetAllDependents(nodeHandle);

                for (NodeHandle depHandle : dependentHandles) {
                    if (depHandle.IsValid() && depHandle.index < instances.size()) {
                        NodeInstance* dependent = instances[depHandle.index].get();
                        if (dependent) {
                            GRAPH_LOG_INFO("[RenderGraph] Marking dependent node '" + dependent->GetInstanceName() +
                                         "' for recompilation");
                            dependent->MarkNeedsRecompile();
                        }
                    }
                }
            } catch (const std::exception& e) {
                GRAPH_LOG_ERROR("[RenderGraph] Failed to recompile node '" + node->GetInstanceName() +
                              "' - deferring. Error: " + std::string(e.what()));
    
                // Re-add to dirty set to retry next frame
                // Find the node index to create a handle
                for (uint32_t i = 0; i < instances.size(); ++i) {
                    if (instances[i].get() == node) {
                        MarkNodeNeedsRecompile(CreateHandle(i));
                        break;
                    }
                }
    
                // Mark that at least one node failed
                failedNodes.insert(node);
                allNodesSucceeded = false;
            }
        } // End for (NodeInstance* node : nodesToRecompile)
    } // End while loop

    // Update graph compiled state
    if (allNodesSucceeded && dirtyNodes.empty()) {
        // All nodes successfully recompiled and no new dirty nodes were added
        isCompiled = true;
        GRAPH_LOG_INFO("[RenderGraph] All nodes successfully recompiled - graph is compiled");
    } else {
        // Some nodes failed or new dirty nodes were added during recompilation
        isCompiled = false;
        if (!allNodesSucceeded) {
            GRAPH_LOG_WARNING("[RenderGraph] Some nodes failed to recompile - graph remains uncompiled");
        }
    }
}

void RenderGraph::MarkNodeNeedsRecompile(NodeHandle nodeHandle) {
    if (nodeHandle.IsValid()) {
        dirtyNodes.insert(nodeHandle);
    }
}

void RenderGraph::HandleCleanupRequest(const EventTypes::CleanupRequestedMessage& msg) {
    GRAPH_LOG_INFO("[RenderGraph] Received cleanup request (ID: " + std::to_string(msg.requestId) + ")");

    // NOTE: CleanupRequestedMessage should NOT trigger full ExecuteCleanup during runtime
    // Recompilation handles selective cleanup via node->Cleanup() for dirty nodes only
    // Full ExecuteCleanup is ONLY for application shutdown (via Cleanup() or WindowCloseEvent)

    // Legacy behavior was: ExecuteCleanup() -> destroy entire graph including VkInstance/VkDevice
    // New behavior: Do nothing - let recompilation handle selective cleanup
    GRAPH_LOG_INFO("[RenderGraph] CleanupRequest ignored - recompilation handles selective cleanup");

    // Still publish completion for backward compatibility
    if (messageBus) {
        auto completionMsg = std::make_unique<EventTypes::CleanupCompletedMessage>(0, 0);
        messageBus->Publish(std::move(completionMsg));
    }
}

void RenderGraph::HandleRenderPause(const EventTypes::RenderPauseEvent& msg) {
    GRAPH_LOG_INFO("[RenderGraph] Render pause event: " +
                 std::string(msg.pauseAction == EventTypes::RenderPauseEvent::Action::PAUSE_START ? "START" : "END") +
                 " (reason: " + std::to_string(static_cast<int>(msg.pauseReason)) + ")");

    renderPaused = (msg.pauseAction == EventTypes::RenderPauseEvent::Action::PAUSE_START);

    if (renderPaused) {
        GRAPH_LOG_INFO("[RenderGraph] Rendering paused - continuing with event processing only");
    } else {
        GRAPH_LOG_INFO("[RenderGraph] Rendering resumed");
    }
}

void RenderGraph::HandleWindowResize(const EventTypes::WindowResizedMessage& msg) {
    GRAPH_LOG_INFO("[RenderGraph] Window resized: " + std::to_string(msg.newWidth) + "x" + std::to_string(msg.newHeight));
    // Note: Nodes that need to respond to window resize (e.g., SwapChainNode)
    // subscribe to WindowResizedMessage and mark themselves dirty.
    // Downstream consumers (DepthBufferNode, FramebufferNode, etc.) are automatically
    // marked dirty by the recompilation system when their dependencies change.
}

void RenderGraph::HandleWindowStateChange(const EventBus::WindowStateChangeEvent& msg) {
    std::string stateStr;

    switch (msg.newState) {
        case EventBus::WindowStateChangeEvent::State::Minimized:
            stateStr = "MINIMIZED - pausing rendering, continuing updates";
            renderPaused = true;
            break;
        case EventBus::WindowStateChangeEvent::State::Maximized:
            stateStr = "MAXIMIZED - resuming rendering";
            renderPaused = false;
            break;
        case EventBus::WindowStateChangeEvent::State::Restored:
            stateStr = "RESTORED - resuming rendering";
            renderPaused = false;
            break;
        case EventBus::WindowStateChangeEvent::State::Focused:
            stateStr = "FOCUSED";
            break;
        case EventBus::WindowStateChangeEvent::State::Unfocused:
            stateStr = "UNFOCUSED";
            break;
    }
    GRAPH_LOG_INFO("[RenderGraph] Window state changed: " + stateStr);
}

void RenderGraph::HandleDeviceSyncRequest(const EventTypes::DeviceSyncRequestedMessage& msg) {
    auto startTime = std::chrono::steady_clock::now();

    std::unordered_set<VkDevice> devicesToWait;
    size_t deviceCount = 0;

    switch (msg.scope) {
        case EventTypes::DeviceSyncRequestedMessage::Scope::AllDevices:
            {
                // Wait for all devices referenced by graph instances
                WaitForGraphDevicesIdle({});

                // Count unique devices for statistics
                for (const auto& instPtr : instances) {
                    if (!instPtr) continue;
                    auto* vdev = instPtr->GetDevice();
                    if (vdev && vdev->device != VK_NULL_HANDLE) {
                        devicesToWait.insert(vdev->device);
                    }
                }
                deviceCount = devicesToWait.size();

                std::string logMsg = "[RenderGraph] Device sync completed for all devices (" +
                                    std::to_string(deviceCount) + " devices)";
                if (!msg.reason.empty()) {
                    logMsg += " - Reason: " + msg.reason;
                }
                GRAPH_LOG_INFO(logMsg);
            }
            break;

        case EventTypes::DeviceSyncRequestedMessage::Scope::SpecificNodes:
            {
                // Collect instances for specific nodes
                std::vector<NodeInstance*> nodesToSync;
                for (const auto& nodeName : msg.nodeNames) {
                    NodeInstance* node = GetInstanceByName(nodeName);
                    if (node) {
                        nodesToSync.push_back(node);
                    } else {
                        GRAPH_LOG_WARNING("[RenderGraph] Warning: Node not found for sync: " + nodeName);
                    }
                }

                if (!nodesToSync.empty()) {
                    WaitForGraphDevicesIdle(nodesToSync);

                    // Count unique devices
                    for (NodeInstance* node : nodesToSync) {
                        auto* vdev = node->GetDevice();
                        if (vdev && vdev->device != VK_NULL_HANDLE) {
                            devicesToWait.insert(vdev->device);
                        }
                    }
                    deviceCount = devicesToWait.size();

                    std::string logMsg = "[RenderGraph] Device sync completed for " +
                                        std::to_string(nodesToSync.size()) + " nodes (" +
                                        std::to_string(deviceCount) + " devices)";
                    if (!msg.reason.empty()) {
                        logMsg += " - Reason: " + msg.reason;
                    }
                    GRAPH_LOG_INFO(logMsg);
                }
            }
            break;

        case EventTypes::DeviceSyncRequestedMessage::Scope::SpecificDevices:
            {
                // Wait for specific VkDevice handles
                devicesToWait.insert(msg.devices.begin(), msg.devices.end());
                WaitForDevicesIdle(devicesToWait);
                deviceCount = devicesToWait.size();

                std::string logMsg = "[RenderGraph] Device sync completed for " +
                                    std::to_string(deviceCount) + " specific devices";
                if (!msg.reason.empty()) {
                    logMsg += " - Reason: " + msg.reason;
                }
                GRAPH_LOG_INFO(logMsg);
            }
            break;
    }

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime
    );

    // Publish completion event
    if (messageBus) {
        auto completionMsg = std::make_unique<EventTypes::DeviceSyncCompletedMessage>(
            0,
            deviceCount,
            duration
        );
        messageBus->Publish(std::move(completionMsg));
    }

    GRAPH_LOG_INFO("[RenderGraph] Device sync took " + std::to_string(duration.count()) + "ms");
}

void RenderGraph::RegisterResourceProducer(Resource* resource, NodeInstance* producer, size_t outputIndex) {
    dependencyTracker.RegisterResourceProducer(resource, producer, static_cast<uint32_t>(outputIndex));
}

NodeInstance* RenderGraph::GetNodeByName(const std::string& name) const {
    auto it = nameToHandle.find(name);
    if (it == nameToHandle.end()) {
        return nullptr;
    }
    NodeHandle handle = it->second;
    if (handle.index >= instances.size()) {
        return nullptr;
    }
    return instances[handle.index].get();
}

} // namespace Vixen::RenderGraph
