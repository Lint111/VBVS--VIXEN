#include "RenderGraph/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include <algorithm>
#include <stdexcept>

namespace Vixen::RenderGraph {

RenderGraph::RenderGraph(
    Vixen::Vulkan::Resources::VulkanDevice* primaryDevice,
    NodeTypeRegistry* registry
)
    : primaryDevice(primaryDevice)
    , typeRegistry(registry)
{
    if (!primaryDevice) {
        throw std::runtime_error("Primary device cannot be null");
    }
    if (!registry) {
        throw std::runtime_error("Node type registry cannot be null");
    }

    usedDevices.push_back(primaryDevice);
}

RenderGraph::~RenderGraph() {
    Clear();
}

NodeHandle RenderGraph::AddNode(const std::string& typeName, const std::string& instanceName) {
    return AddNode(typeName, instanceName, primaryDevice);
}

NodeHandle RenderGraph::AddNode(
    const std::string& typeName,
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
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
    auto instance = type->CreateInstance(instanceName, device);
    if (!instance) {
        throw std::runtime_error("Failed to create instance for type: " + typeName);
    }

    // Add to graph
    uint32_t index = static_cast<uint32_t>(instances.size());
    NodeHandle handle = CreateHandle(index);

    instances.push_back(std::move(instance));
    nameToHandle[instanceName] = handle;
    instancesByType[typeId].push_back(instances[index].get());

    // Add to topology
    topology.AddNode(instances[index].get());

    // Track device usage
    if (device != primaryDevice) {
        auto it = std::find(usedDevices.begin(), usedDevices.end(), device);
        if (it == usedDevices.end()) {
            usedDevices.push_back(device);
        }
    }

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
        throw std::runtime_error("Invalid output index for node: " + fromNode->GetInstanceName());
    }

    if (inputIdx >= toType->GetInputCount()) {
        throw std::runtime_error("Invalid input index for node: " + toNode->GetInstanceName());
    }

    // Create or get resource for the output
    Resource* resource = fromNode->GetOutput(outputIdx);
    if (!resource) {
        resource = CreateResourceForOutput(fromNode, outputIdx);
        fromNode->SetOutput(outputIdx, resource);
    }

    // Connect to input
    toNode->SetInput(inputIdx, resource);

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
    instances.clear();
    resources.clear();
    nameToHandle.clear();
    instancesByType.clear();
    executionOrder.clear();
    topology.Clear();
    usedDevices.clear();
    usedDevices.push_back(primaryDevice);
    isCompiled = false;
}

void RenderGraph::Compile() {
    // Validation
    std::string errorMessage;
    if (!Validate(errorMessage)) {
        throw std::runtime_error("Graph validation failed: " + errorMessage);
    }

    // Phase 1: Propagate device affinity
    PropagateDeviceAffinity();

    // Phase 2: Analyze dependencies and build execution order
    AnalyzeDependencies();

    // Phase 3: Allocate resources
    AllocateResources();

    // Phase 4: Generate pipelines
    GeneratePipelines();

    // Phase 5: Build final execution order
    BuildExecutionOrder();

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

    // Execute all nodes in topological order
    // Nodes handle their own synchronization, command recording, and presentation
    for (NodeInstance* node : executionOrder) {
        if (node->GetState() == NodeState::Ready ||
            node->GetState() == NodeState::Compiled) {

            node->SetState(NodeState::Executing);

            // Pass VK_NULL_HANDLE - nodes manage their own command buffers
            node->Execute(VK_NULL_HANDLE);

            node->SetState(NodeState::Complete);
        }
    }

    // PresentNode returns the presentation result
    // For now, return success (nodes handle errors internally)
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
            if (!inputSchema[i].optional && !instance->GetInput(static_cast<uint32_t>(i))) {
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

    const ResourceDescriptor& desc = outputSchema[outputIndex];
    
    // Create resource
    auto resource = std::make_unique<Resource>(
        desc.type,
        desc.lifetime,
        desc.description
    );

    Resource* resourcePtr = resource.get();
    resources.push_back(std::move(resource));
    
    return resourcePtr;
}

void RenderGraph::PropagateDeviceAffinity() {
    // TODO: Implement device affinity propagation
    // For now, all nodes use their initially assigned device
    
    for (size_t i = 0; i < instances.size(); ++i) {
        instances[i]->SetDeviceIndex(0); // Simple: all use device 0
    }
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
    // TODO: Implement full resource allocation with aliasing
    // For now, just allocate each resource
    
    VkDevice device = primaryDevice->device;
    
    for (auto& resource : resources) {
        if (!resource->IsAllocated()) {
            if (resource->GetType() == ResourceType::Image ||
                resource->GetType() == ResourceType::CubeMap ||
                resource->GetType() == ResourceType::Image3D ||
                resource->GetType() == ResourceType::StorageImage) {
                
                const ImageDescription* desc = resource->GetImageDescription();
                if (desc) {
                    // Will need physical device - this is simplified
                    // resource->AllocateImage(device, *desc);
                }
            }
            else if (resource->GetType() == ResourceType::Buffer) {
                const BufferDescription* desc = resource->GetBufferDescription();
                if (desc) {
                    // resource->AllocateBuffer(device, *desc);
                }
            }
        }
    }
}

void RenderGraph::GeneratePipelines() {
    // TODO: Implement pipeline generation
    // This will create/reuse pipelines for each node
    
    for (auto& instance : instances) {
        instance->Compile();
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

} // namespace Vixen::RenderGraph
