#include "Core/NodeTypeRegistry.h"
#include <algorithm>

// Phase F+ node types
#include "Nodes/WindowNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/DepthBufferNode.h"
#include "Nodes/RenderPassNode.h"
#include "Nodes/FramebufferNode.h"
#include "Nodes/FrameSyncNode.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Nodes/GraphicsPipelineNode.h"
#include "Nodes/DescriptorSetNode.h"
#include "Nodes/VertexBufferNode.h"
#include "Nodes/TextureLoaderNode.h"
#include "Nodes/CommandPoolNode.h"
#include "Nodes/GeometryRenderNode.h"
#include "Nodes/PresentNode.h"
#include "Nodes/LoopBridgeNode.h"

// Phase G node types
#include "Nodes/ComputePipelineNode.h"
#include "Nodes/ComputeDispatchNode.h"

// Phase H node types
#include "Nodes/InstanceNode.h"
#include "Nodes/CameraNode.h"
#include "Nodes/VoxelGridNode.h"
#include "Nodes/InputNode.h"
// Note: ConstantNodeType excluded - has circular dependency (ConstantNode.h uses RenderGraph inline)
// ConstantNodeType must be registered separately where RenderGraph.h is already included
#include "Nodes/DescriptorResourceGathererNode.h"
#include "Nodes/PushConstantGathererNode.h"
#include "Nodes/DebugBufferReaderNode.h"

namespace Vixen::RenderGraph {

bool NodeTypeRegistry::RegisterNodeType(std::unique_ptr<NodeType> nodeType) {
    if (!nodeType) {
        return false;
    }

    NodeTypeId typeId = nodeType->GetTypeId();
    const std::string& typeName = nodeType->GetTypeName();

    // Check for ID collision
    if (nodeTypesById.find(typeId) != nodeTypesById.end()) {
        return false; // Type ID already registered
    }

    // Check for name collision
    if (nameToId.find(typeName) != nameToId.end()) {
        return false; // Type name already registered
    }

    // Register
    nameToId[typeName] = typeId;
    nodeTypesById[typeId] = std::move(nodeType);

    return true;
}

bool NodeTypeRegistry::UnregisterNodeType(NodeTypeId typeId) {
    auto it = nodeTypesById.find(typeId);
    if (it == nodeTypesById.end()) {
        return false;
    }

    // Remove from name map
    const std::string& typeName = it->second->GetTypeName();
    nameToId.erase(typeName);

    // Remove from ID map
    nodeTypesById.erase(it);

    return true;
}

bool NodeTypeRegistry::UnregisterNodeType(const std::string& typeName) {
    auto nameIt = nameToId.find(typeName);
    if (nameIt == nameToId.end()) {
        return false;
    }

    NodeTypeId typeId = nameIt->second;
    return UnregisterNodeType(typeId);
}

NodeType* NodeTypeRegistry::GetNodeType(NodeTypeId typeId) const {
    auto it = nodeTypesById.find(typeId);
    if (it != nodeTypesById.end()) {
        return it->second.get();
    }
    return nullptr;
}

NodeType* NodeTypeRegistry::GetNodeType(const std::string& typeName) const {
    auto nameIt = nameToId.find(typeName);
    if (nameIt != nameToId.end()) {
        auto typeIt = nodeTypesById.find(nameIt->second);
        if (typeIt != nodeTypesById.end()) {
            return typeIt->second.get();
        }
    }
    return nullptr;
}

bool NodeTypeRegistry::HasNodeType(NodeTypeId typeId) const {
    return nodeTypesById.find(typeId) != nodeTypesById.end();
}

bool NodeTypeRegistry::HasNodeType(const std::string& typeName) const {
    return nameToId.find(typeName) != nameToId.end();
}

std::vector<NodeType*> NodeTypeRegistry::GetAllNodeTypes() const {
    std::vector<NodeType*> types;
    types.reserve(nodeTypesById.size());
    
    for (const auto& [id, type] : nodeTypesById) {
        types.push_back(type.get());
    }
    
    return types;
}

std::vector<NodeType*> NodeTypeRegistry::GetNodeTypesByPipeline(PipelineType pipelineType) const {
    std::vector<NodeType*> types;
    
    for (const auto& [id, type] : nodeTypesById) {
        if (type->GetPipelineType() == pipelineType) {
            types.push_back(type.get());
        }
    }
    
    return types;
}

std::vector<NodeType*> NodeTypeRegistry::GetNodeTypesWithCapability(DeviceCapability capability) const {
    std::vector<NodeType*> types;
    
    for (const auto& [id, type] : nodeTypesById) {
        if (HasCapability(type->GetRequiredCapabilities(), capability)) {
            types.push_back(type.get());
        }
    }
    
    return types;
}

void NodeTypeRegistry::Clear() {
    nodeTypesById.clear();
    nameToId.clear();
    typeIndexToId.clear();
    nextTypeId = 1;
}

void RegisterBuiltInNodeTypes(NodeTypeRegistry& registry) {
    // Register built-in node types using type-based API (zero string literals)
    // Phase F+ nodes:
    registry.Register<WindowNodeType>();
    registry.Register<DeviceNodeType>();
    registry.Register<SwapChainNodeType>();
    registry.Register<DepthBufferNodeType>();
    registry.Register<RenderPassNodeType>();
    registry.Register<FramebufferNodeType>();
    registry.Register<FrameSyncNodeType>();
    registry.Register<ShaderLibraryNodeType>();
    registry.Register<GraphicsPipelineNodeType>();
    registry.Register<DescriptorSetNodeType>();
    registry.Register<VertexBufferNodeType>();
    registry.Register<TextureLoaderNodeType>();
    registry.Register<CommandPoolNodeType>();
    registry.Register<GeometryRenderNodeType>();
    registry.Register<PresentNodeType>();
    registry.Register<LoopBridgeNodeType>();

    // Phase G nodes:
    registry.Register<ComputePipelineNodeType>();
    registry.Register<ComputeDispatchNodeType>();

    // Phase H nodes:
    registry.Register<InstanceNodeType>();
    registry.Register<CameraNodeType>();
    registry.Register<VoxelGridNodeType>();
    registry.Register<InputNodeType>();
    // Note: ConstantNodeType must be registered in application code (circular dependency)
    registry.Register<DescriptorResourceGathererNodeType>();
    registry.Register<PushConstantGathererNodeType>();
    registry.Register<DebugBufferReaderNodeType>();
}

} // namespace Vixen::RenderGraph
