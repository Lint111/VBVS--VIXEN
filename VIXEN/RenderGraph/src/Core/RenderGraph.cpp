#include "Core/RenderGraph.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/PresentNode.h"
#include "core/Resource.h"
#include "VulkanResources/VulkanDevice.h"
#include <algorithm>
#include <stdexcept>

namespace Vixen::RenderGraph {

RenderGraph::RenderGraph(
    NodeTypeRegistry* registry,
    EventBus::MessageBus* messageBus,
    Logger* mainLogger
)
    : typeRegistry(registry)
    , messageBus(messageBus)
{
    if (!registry) {
        throw std::runtime_error("Node type registry cannot be null");
    }

#ifdef _DEBUG
    if (mainLogger) {
        this->mainLogger = mainLogger;
    }
#else
    (void)mainLogger;
#endif

    // Subscribe to cleanup events if bus provided
    if (messageBus) {
        cleanupEventSubscription = messageBus->Subscribe(
            CleanupRequestedMessage::TYPE,
            [this](const EventBus::Message& msg) {
                auto& cleanupMsg = static_cast<const CleanupRequestedMessage&>(msg);
                this->HandleCleanupRequest(cleanupMsg);
                return true;
            }
        );
    }
}

RenderGraph::~RenderGraph() {
    // Unsubscribe from events
    if (messageBus && cleanupEventSubscription != 0) {
        messageBus->Unsubscribe(cleanupEventSubscription);
    }
    
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

    // Set owning graph pointer for cleanup registration
    instances[index]->SetOwningGraph(this);

    // Add logger if in debug mode (attach node to parent logger)
    #ifdef _DEBUG
    instances[index]->RegisterToParentLogger(mainLogger);
    #endif

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

    // Create or get resource for the output
    // For now, assume scalar connections (array index 0)
    Resource* resource = fromNode->GetOutput(outputIdx, 0);
    if (!resource) {
        resource = CreateResourceForOutput(fromNode, outputIdx);
        fromNode->SetOutput(outputIdx, 0, resource);
    }

    // Register resource producer for dependency tracking
    dependencyTracker.RegisterResourceProducer(resource, fromNode, outputIdx);

    // Connect to input (scalar connection at array index 0)
    toNode->SetInput(inputIdx, 0, resource);

    // Add dependency
    toNode->AddDependency(fromNode);

    // Add edge to topology
    GraphEdge edge;
    edge.source = fromNode;
    edge.sourceOutputIndex = outputIdx;
    edge.target = toNode;
    edge.targetInputIndex = inputIdx;
    topology.AddEdge(edge);

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

void RenderGraph::ExecuteCleanup() {
    std::cout << "[RenderGraph::ExecuteCleanup] Executing cleanup callbacks..." << std::endl;
    cleanupStack.ExecuteAll();
    std::cout << "[RenderGraph::ExecuteCleanup] Cleanup complete" << std::endl;
}

void RenderGraph::Compile() {
    std::cout << "[RenderGraph::Compile] Starting compilation..." << std::endl;
    
    // Validation
    std::cout << "[RenderGraph::Compile] Phase: Validation..." << std::endl;
    std::string errorMessage;
    if (!Validate(errorMessage)) {
        throw std::runtime_error("Graph validation failed: " + errorMessage);
    }

    // Phase 1: Propagate device affinity
    // PropagateDeviceAffinity();  // Removed - single device system

    // Phase 2: Analyze dependencies and build execution order
    std::cout << "[RenderGraph::Compile] Phase: AnalyzeDependencies..." << std::endl;
    AnalyzeDependencies();

    // Phase 3: Allocate resources
    std::cout << "[RenderGraph::Compile] Phase: AllocateResources..." << std::endl;
    AllocateResources();

    // Phase 4: Generate pipelines
    std::cout << "[RenderGraph::Compile] Phase: GeneratePipelines..." << std::endl;
    GeneratePipelines();

    // Phase 5: Build final execution order
    std::cout << "[RenderGraph::Compile] Phase: BuildExecutionOrder..." << std::endl;
    BuildExecutionOrder();

    std::cout << "[RenderGraph::Compile] Compilation complete!" << std::endl;
    isCompiled = true;
}

void RenderGraph::Execute(VkCommandBuffer commandBuffer) {
    if (!isCompiled) {
        throw std::runtime_error("Graph must be compiled before execution");
    }

    // Execute nodes in order
    for (NodeInstance* node : executionOrder) {
        if (node->GetState() == NodeState::Ready ||
            node->GetState() == NodeState::Compiled) {

            node->SetState(NodeState::Executing);
            node->Execute(commandBuffer);
            node->SetState(NodeState::Complete);
        }
    }
}

VkResult RenderGraph::RenderFrame() {
    if (!isCompiled) {
        throw std::runtime_error("Graph must be compiled before rendering");
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
            node->GetState() == NodeState::Compiled) {

            node->SetState(NodeState::Executing);

            // NOTE: Legacy PHASE 1 HACK removed - PresentNode now uses typed slots.
            // SwapChain → PresentNode connections should be established via:
            // Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN, presentNode, PresentNodeConfig::SWAPCHAIN)
            // Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, presentNode, PresentNodeConfig::IMAGE_INDEX)
            // etc.

            // Pass VK_NULL_HANDLE - nodes manage their own command buffers
            node->Execute(VK_NULL_HANDLE);

            node->SetState(NodeState::Complete);
        }
    }

    // NOTE: Legacy GetLastResult() removed - use Out(PresentNodeConfig::PRESENT_RESULT) instead
    // Get result via typed slot if needed, or check presentNode directly

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
            if (!inputSchema[i].optional && !instance->GetInput(static_cast<uint32_t>(i), 0)) {
                errorMessage = "Node " + instance->GetInstanceName() +
                             " missing required input at index " + std::to_string(i);
                return false;
            }
        }
    }

    return true;
}

// ====== Private Methods ======

NodeHandle RenderGraph::CreateHandle(uint32_t index) {
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
    std::cout << "[GeneratePipelines] Total instances to compile: " << executionOrder.size() << std::endl;
    std::cout.flush();
    for (size_t i = 0; i < executionOrder.size(); ++i) {
        std::cout << "[GeneratePipelines]   " << i << ": " << executionOrder[i]->GetInstanceName() << std::endl;
        std::cout.flush();
    }

    for (NodeInstance* instance : executionOrder) {
        // Skip nodes that are already compiled (e.g., pre-compiled device node)
        if (instance->GetState() == NodeState::Compiled) {
            std::cout << "[GeneratePipelines] Skipping already-compiled node: " << instance->GetInstanceName() << std::endl;
            continue;
        }

        std::cout << "[GeneratePipelines] Calling Setup() on node: " << instance->GetInstanceName() << std::endl;
        instance->Setup();    // Call Setup() before Compile()

        std::cout << "[GeneratePipelines] Calling Compile() on node: " << instance->GetInstanceName() << std::endl;
        instance->Compile();

        std::cout << "[GeneratePipelines] Node compiled successfully: " << instance->GetInstanceName() << std::endl;
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
    
    std::cout << "[RecursiveCleanup] Checking node: " << node->GetInstanceName() << std::endl;
    
    // First, recursively clean dependencies that would become orphaned
    for (NodeInstance* dependency : node->GetDependencies()) {
        auto it = dependentCounts.find(dependency);
        if (it != dependentCounts.end()) {
            std::cout << "[RecursiveCleanup]   Dependency " << dependency->GetInstanceName() 
                      << " has refCount=" << it->second << std::endl;
            
            // Check if this dependency has refCount == 1 (only current node uses it)
            if (it->second == 1) {
                std::cout << "[RecursiveCleanup]   -> Will be orphaned, cleaning recursively" << std::endl;
                RecursiveCleanup(dependency, cleaned);
            } else {
                std::cout << "[RecursiveCleanup]   -> Still has other users, keeping" << std::endl;
            }
            
            // Decrement count after checking (simulate removing this edge)
            it->second--;
        }
    }
    
    // Now clean this node
    std::cout << "[RecursiveCleanup] Cleaning node: " << node->GetInstanceName() << std::endl;
    
    // Execute cleanup callback via CleanupStack
    cleanupStack.ExecuteFrom(node->GetInstanceName() + "_Cleanup");
    
    // Mark as cleaned
    cleaned.insert(node);
}

size_t RenderGraph::CleanupSubgraph(const std::string& rootNodeName) {
    NodeInstance* rootNode = GetInstanceByName(rootNodeName);
    if (!rootNode) {
        std::cerr << "[CleanupSubgraph] Node not found: " << rootNodeName << std::endl;
        return 0;
    }
    
    std::cout << "[CleanupSubgraph] Starting partial cleanup from: " << rootNodeName << std::endl;
    
    // Compute reference counts
    ComputeDependentCounts();
    
    // Track cleaned nodes
    std::set<NodeInstance*> cleaned;
    
    // Recursively clean
    RecursiveCleanup(rootNode, cleaned);
    
    std::cout << "[CleanupSubgraph] Cleaned " << cleaned.size() << " nodes" << std::endl;
    
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
    std::cout << "[CleanupByTag] Cleaning nodes with tag: " << tag << std::endl;
    
    // Find all nodes with this tag
    std::vector<NodeInstance*> matchingNodes;
    for (const auto& instance : instances) {
        if (instance->HasTag(tag)) {
            matchingNodes.push_back(instance.get());
        }
    }
    
    if (matchingNodes.empty()) {
        std::cout << "[CleanupByTag] No nodes found with tag: " << tag << std::endl;
        return 0;
    }
    
    // Compute reference counts
    ComputeDependentCounts();
    
    // Cleanup each matching node
    std::set<NodeInstance*> cleaned;
    for (NodeInstance* node : matchingNodes) {
        RecursiveCleanup(node, cleaned);
    }
    
    std::cout << "[CleanupByTag] Cleaned " << cleaned.size() << " nodes" << std::endl;
    return cleaned.size();
}

size_t RenderGraph::CleanupByType(const std::string& typeName) {
    std::cout << "[CleanupByType] Cleaning nodes of type: " << typeName << std::endl;
    
    // Find all nodes of this type
    std::vector<NodeInstance*> matchingNodes;
    for (const auto& instance : instances) {
        if (instance->GetNodeType()->GetTypeName() == typeName) {
            matchingNodes.push_back(instance.get());
        }
    }
    
    if (matchingNodes.empty()) {
        std::cout << "[CleanupByType] No nodes found of type: " << typeName << std::endl;
        return 0;
    }
    
    // Compute reference counts
    ComputeDependentCounts();
    
    // Cleanup each matching node
    std::set<NodeInstance*> cleaned;
    for (NodeInstance* node : matchingNodes) {
        RecursiveCleanup(node, cleaned);
    }
    
    std::cout << "[CleanupByType] Cleaned " << cleaned.size() << " nodes" << std::endl;
    return cleaned.size();
}

void RenderGraph::HandleCleanupRequest(const CleanupRequestedMessage& msg) {
    std::cout << "[RenderGraph] Received cleanup request";
    if (!msg.reason.empty()) {
        std::cout << " - Reason: " << msg.reason;
    }
    std::cout << std::endl;
    
    std::vector<std::string> cleanedNodes;
    size_t cleanedCount = 0;
    
    switch (msg.scope) {
        case CleanupScope::Specific:
            if (msg.targetNodeName.has_value()) {
                cleanedCount = CleanupSubgraph(msg.targetNodeName.value());
            }
            break;
            
        case CleanupScope::ByTag:
            if (msg.tag.has_value()) {
                cleanedCount = CleanupByTag(msg.tag.value());
            }
            break;
            
        case CleanupScope::ByType:
            if (msg.typeName.has_value()) {
                cleanedCount = CleanupByType(msg.typeName.value());
            }
            break;
            
        case CleanupScope::Full:
            std::cout << "[RenderGraph] Executing full cleanup" << std::endl;
            ExecuteCleanup();
            cleanedCount = instances.size();
            break;
    }
    
    // Publish completion event
    if (messageBus) {
        auto completionMsg = std::make_unique<CleanupCompletedMessage>(0);
        completionMsg->cleanedCount = cleanedCount;
        messageBus->Publish(std::move(completionMsg));
    }
}

} // namespace Vixen::RenderGraph
