#pragma once

#include "NodeType.h"
#include <string>
#include <map>
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Registry for all available node types
 * 
 * Central repository that stores all registered NodeType templates.
 * Allows lookup by ID or name and provides node creation.
 */
class NodeTypeRegistry {
public:
    NodeTypeRegistry() = default;
    ~NodeTypeRegistry() = default;

    // Registration
    bool RegisterNodeType(std::unique_ptr<NodeType> nodeType);
    bool UnregisterNodeType(NodeTypeId typeId);
    bool UnregisterNodeType(const std::string& typeName);

    // Lookup
    NodeType* GetNodeType(NodeTypeId typeId) const;
    NodeType* GetNodeType(const std::string& typeName) const;
    bool HasNodeType(NodeTypeId typeId) const;
    bool HasNodeType(const std::string& typeName) const;

    // Query
    std::vector<NodeType*> GetAllNodeTypes() const;
    std::vector<NodeType*> GetNodeTypesByPipeline(PipelineType pipelineType) const;
    std::vector<NodeType*> GetNodeTypesWithCapability(DeviceCapability capability) const;
    size_t GetNodeTypeCount() const { return nodeTypesById.size(); }

    // Clear
    void Clear();

private:
    std::map<NodeTypeId, std::unique_ptr<NodeType>> nodeTypesById;
    std::map<std::string, NodeTypeId> nameToId;
    NodeTypeId nextTypeId = 1;
};

/**
 * @brief Helper function to register built-in node types
 */
void RegisterBuiltInNodeTypes(NodeTypeRegistry& registry);

} // namespace Vixen::RenderGraph
