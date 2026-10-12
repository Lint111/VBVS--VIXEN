#pragma once

#include "NodeType.h"
#include <string>
#include <map>
#include <memory>
#include <vector>
#include <typeindex>

namespace Vixen::RenderGraph {

class NodeTypeRegistry;
template<typename T, typename... Args>
bool RegisterSubGraphNodeType(NodeTypeRegistry& registry, Args&&... args);

/**
 * @brief Registry for all available node types
 *
 * Central repository that stores all registered NodeType templates.
 * Allows lookup by ID, name, or C++ type and provides node creation.
 */
class NodeTypeRegistry {
public:
    NodeTypeRegistry() = default;
    ~NodeTypeRegistry() = default;

    // ========================================================================
    // Type-based registration (zero boilerplate - preferred API)
    // ========================================================================

    /**
     * @brief Register a node type using its C++ type as the key
     * @tparam T The NodeType-derived class to register
     * @tparam Args Constructor arguments for T
     * @return true if registration succeeded
     */
    template<typename T, typename... Args>
    bool Register(Args&&... args) {
        if constexpr (std::is_base_of_v<NodeType, T>) {
            return RegisterNodeTypeFor<T>(std::make_unique<T>(std::forward<Args>(args)...));
        } else if constexpr (std::is_base_of_v<SubGraphDefinition, T>) {
            static_assert(sizeof...(Args) == 0,
                          "Registered SubGraphType declarations are default-constructed");
            return RegisterSubGraphNodeType<T>(*this);
        } else {
            static_assert(std::is_base_of_v<NodeType, T> ||
                          std::is_base_of_v<SubGraphDefinition, T>,
                          "T must derive from NodeType or SubGraphDefinition");
        }
    }

    /** @brief Store an adapter under the C++ declaration type it represents. */
    template<typename T>
    bool RegisterNodeTypeFor(std::unique_ptr<NodeType> nodeType) {
        const auto typeIndex = std::type_index(typeid(T));
        if (typeIndexToId.contains(typeIndex)) return false;
        const NodeTypeId id = nodeType->GetTypeId();
        if (!RegisterNodeType(std::move(nodeType))) return false;
        typeIndexToId[typeIndex] = id;
        return true;
    }

    /**
     * @brief Get a node type by its C++ type
     * @tparam T The NodeType-derived class to retrieve
     * @return Pointer to the node type, or nullptr if not found
     */
    template<typename T>
    auto Get() const {
        static_assert(std::is_base_of_v<NodeType, T> ||
                      std::is_base_of_v<SubGraphDefinition, T>,
                      "T must derive from NodeType or SubGraphDefinition");
        auto it = typeIndexToId.find(std::type_index(typeid(T)));
        if (it != typeIndexToId.end()) {
            if constexpr (std::is_base_of_v<NodeType, T>) {
                return static_cast<T*>(GetNodeType(it->second));
            } else {
                return GetNodeType(it->second);
            }
        }
        if constexpr (std::is_base_of_v<NodeType, T>) return static_cast<T*>(nullptr);
        else return static_cast<NodeType*>(nullptr);
    }

    /**
     * @brief Check if a node type is registered by its C++ type
     * @tparam T The NodeType-derived class to check
     */
    template<typename T>
    bool Has() const {
        static_assert(std::is_base_of_v<NodeType, T> ||
                      std::is_base_of_v<SubGraphDefinition, T>,
                      "T must derive from NodeType or SubGraphDefinition");
        return typeIndexToId.contains(std::type_index(typeid(T)));
    }

    /**
     * @brief Unregister a node type by its C++ type
     * @tparam T The NodeType-derived class to unregister
     */
    template<typename T>
    bool Unregister() {
        static_assert(std::is_base_of_v<NodeType, T> ||
                      std::is_base_of_v<SubGraphDefinition, T>,
                      "T must derive from NodeType or SubGraphDefinition");
        auto typeIndex = std::type_index(typeid(T));
        auto it = typeIndexToId.find(typeIndex);
        if (it != typeIndexToId.end()) {
            NodeTypeId id = it->second;
            typeIndexToId.erase(it);
            return UnregisterNodeType(id);
        }
        return false;
    }

    // ========================================================================
    // Legacy API (string/ID based - maintained for compatibility)
    // ========================================================================

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
    std::map<std::type_index, NodeTypeId> typeIndexToId;  // Type-based lookup
    NodeTypeId nextTypeId = 1;
};

} // namespace Vixen::RenderGraph
