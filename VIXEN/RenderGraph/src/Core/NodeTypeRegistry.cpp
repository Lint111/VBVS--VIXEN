#include "Core/NodeTypeRegistry.h"
#include <algorithm>

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
    nextTypeId = 1;
}

void RegisterBuiltInNodeTypes(NodeTypeRegistry& registry) {
    // TODO: Register built-in node types
    // This will be implemented as we create specific node types
    // Examples:
    // - GeometryPassNode
    // - ShadowMapNode
    // - PostProcessNode
    // - ComputeNode
    // - DeviceTransferNode
}

} // namespace Vixen::RenderGraph
