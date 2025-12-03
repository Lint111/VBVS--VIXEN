#pragma once

#include "NodeInstance.h"
#include "ILoggable.h"
#include <vector>
#include <set>
#include <map>

namespace Vixen::RenderGraph {

/**
 * @brief Graph edge representing a connection between nodes
 */
struct GraphEdge {
    NodeInstance* source = nullptr;
    uint32_t sourceOutputIndex = 0;
    NodeInstance* target = nullptr;
    uint32_t targetInputIndex = 0;

    bool operator==(const GraphEdge& other) const {
        return source == other.source &&
               sourceOutputIndex == other.sourceOutputIndex &&
               target == other.target &&
               targetInputIndex == other.targetInputIndex;
    }
};

/**
 * @brief Graph topology analysis and manipulation
 *
 * Handles dependency analysis, cycle detection, and topological sorting
 * of the render graph.
 */
class GraphTopology : public ILoggable {
public:
    GraphTopology();
    ~GraphTopology() = default;

    // Graph construction
    void AddNode(NodeInstance* node);
    void RemoveNode(NodeInstance* node);
    void AddEdge(const GraphEdge& edge);
    void RemoveEdge(const GraphEdge& edge);
    void Clear();

    // Analysis
    bool HasCycles() const;
    std::vector<NodeInstance*> TopologicalSort() const;
    std::vector<NodeInstance*> GetRootNodes() const;
    std::vector<NodeInstance*> GetLeafNodes() const;

    // Dependencies
    std::vector<NodeInstance*> GetDirectDependencies(NodeInstance* node) const;
    std::vector<NodeInstance*> GetDirectDependents(NodeInstance* node) const;
    std::vector<NodeInstance*> GetAllDependencies(NodeInstance* node) const;
    std::vector<NodeInstance*> GetAllDependents(NodeInstance* node) const;

    // Edges
    const std::vector<GraphEdge>& GetEdges() const { return edges; }
    std::vector<GraphEdge> GetIncomingEdges(NodeInstance* node) const;
    std::vector<GraphEdge> GetOutgoingEdges(NodeInstance* node) const;

    // Nodes
    const std::set<NodeInstance*>& GetNodes() const { return nodes; }
    size_t GetNodeCount() const { return nodes.size(); }
    size_t GetEdgeCount() const { return edges.size(); }

    // Validation
    bool ValidateGraph(std::string& errorMessage) const;
    bool IsConnected() const;

private:
    std::set<NodeInstance*> nodes;
    std::vector<GraphEdge> edges;

    // Helper methods for cycle detection
    bool HasCyclesHelper(
        NodeInstance* node,
        std::set<NodeInstance*>& visited,
        std::set<NodeInstance*>& recursionStack
    ) const;

    // Helper for topological sort
    void TopologicalSortHelper(
        NodeInstance* node,
        std::set<NodeInstance*>& visited,
        std::vector<NodeInstance*>& stack
    ) const;

    // Helper for transitive closure
    void GetAllDependenciesHelper(
        NodeInstance* node,
        std::set<NodeInstance*>& visited,
        std::vector<NodeInstance*>& result
    ) const;
};

} // namespace Vixen::RenderGraph
