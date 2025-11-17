#include "Core/GraphTopology.h"
#include "Core/VariadicTypedNode.h"
#include <algorithm>
#include <queue>

namespace Vixen::RenderGraph {

GraphTopology::GraphTopology() {
    InitializeLogger("Topology");
}

void GraphTopology::AddNode(NodeInstance* node) {
    if (node) {
        nodes.insert(node);
    }
}

void GraphTopology::RemoveNode(NodeInstance* node) {
    if (!node) return;

    // Remove all edges connected to this node
    edges.erase(
        std::remove_if(edges.begin(), edges.end(),
            [node](const GraphEdge& edge) {
                return edge.source == node || edge.target == node;
            }),
        edges.end()
    );

    nodes.erase(node);
}

void GraphTopology::AddEdge(const GraphEdge& edge) {
    if (edge.source && edge.target) {
        // Check if edge already exists
        auto it = std::find(edges.begin(), edges.end(), edge);
        if (it == edges.end()) {
            LOG_DEBUG("Adding edge: " + edge.source->GetInstanceName() +
                     " -> " + edge.target->GetInstanceName());
            edges.push_back(edge);

            // Ensure both nodes are in the graph
            nodes.insert(edge.source);
            nodes.insert(edge.target);
        } else {
            LOG_DEBUG("Edge already exists: " + edge.source->GetInstanceName() +
                     " -> " + edge.target->GetInstanceName());
        }
    }
}

void GraphTopology::RemoveEdge(const GraphEdge& edge) {
    auto it = std::find(edges.begin(), edges.end(), edge);
    if (it != edges.end()) {
        edges.erase(it);
    }
}

void GraphTopology::Clear() {
    nodes.clear();
    edges.clear();
}

bool GraphTopology::HasCycles() const {
    std::set<NodeInstance*> visited;
    std::set<NodeInstance*> recursionStack;

    for (NodeInstance* node : nodes) {
        if (visited.find(node) == visited.end()) {
            if (HasCyclesHelper(node, visited, recursionStack)) {
                return true;
            }
        }
    }

    return false;
}

bool GraphTopology::HasCyclesHelper(
    NodeInstance* node,
    std::set<NodeInstance*>& visited,
    std::set<NodeInstance*>& recursionStack
) const {
    visited.insert(node);
    recursionStack.insert(node);

    // Check all outgoing edges
    for (const auto& edge : edges) {
        if (edge.source == node) {
            NodeInstance* target = edge.target;

            // If not visited, recurse
            if (visited.find(target) == visited.end()) {
                if (HasCyclesHelper(target, visited, recursionStack)) {
                    return true;
                }
            }
            // If in recursion stack, we found a cycle
            else if (recursionStack.find(target) != recursionStack.end()) {
                return true;
            }
        }
    }

    recursionStack.erase(node);
    return false;
}

std::vector<NodeInstance*> GraphTopology::TopologicalSort() const {
    std::set<NodeInstance*> visited;
    std::vector<NodeInstance*> stack;

    // Visit all nodes
    for (NodeInstance* node : nodes) {
        if (visited.find(node) == visited.end()) {
            TopologicalSortHelper(node, visited, stack);
        }
    }

    // Reverse to get correct order
    std::reverse(stack.begin(), stack.end());

    return stack;
}

void GraphTopology::TopologicalSortHelper(
    NodeInstance* node,
    std::set<NodeInstance*>& visited,
    std::vector<NodeInstance*>& stack
) const {
    visited.insert(node);

    // Visit all outgoing nodes
    for (const auto& edge : edges) {
        if (edge.source == node) {
            NodeInstance* target = edge.target;
            if (visited.find(target) == visited.end()) {
                TopologicalSortHelper(target, visited, stack);
            }
        }
    }

    // Add to stack after visiting dependencies
    stack.push_back(node);
}

std::vector<NodeInstance*> GraphTopology::GetRootNodes() const {
    std::vector<NodeInstance*> roots;

    for (NodeInstance* node : nodes) {
        bool hasIncoming = false;
        for (const auto& edge : edges) {
            if (edge.target == node) {
                hasIncoming = true;
                break;
            }
        }

        if (!hasIncoming) {
            roots.push_back(node);
        }
    }

    return roots;
}

std::vector<NodeInstance*> GraphTopology::GetLeafNodes() const {
    std::vector<NodeInstance*> leaves;

    for (NodeInstance* node : nodes) {
        bool hasOutgoing = false;
        for (const auto& edge : edges) {
            if (edge.source == node) {
                hasOutgoing = true;
                break;
            }
        }

        if (!hasOutgoing) {
            leaves.push_back(node);
        }
    }

    return leaves;
}

std::vector<NodeInstance*> GraphTopology::GetDirectDependencies(NodeInstance* node) const {
    std::vector<NodeInstance*> dependencies;

    for (const auto& edge : edges) {
        if (edge.target == node) {
            // Check if not already in the list
            if (std::find(dependencies.begin(), dependencies.end(), edge.source) == dependencies.end()) {
                dependencies.push_back(edge.source);
            }
        }
    }

    return dependencies;
}

std::vector<NodeInstance*> GraphTopology::GetDirectDependents(NodeInstance* node) const {
    std::vector<NodeInstance*> dependents;

    for (const auto& edge : edges) {
        if (edge.source == node) {
            // Check if not already in the list
            if (std::find(dependents.begin(), dependents.end(), edge.target) == dependents.end()) {
                dependents.push_back(edge.target);
            }
        }
    }

    return dependents;
}

std::vector<NodeInstance*> GraphTopology::GetAllDependencies(NodeInstance* node) const {
    std::set<NodeInstance*> visited;
    std::vector<NodeInstance*> result;

    GetAllDependenciesHelper(node, visited, result);

    return result;
}

void GraphTopology::GetAllDependenciesHelper(
    NodeInstance* node,
    std::set<NodeInstance*>& visited,
    std::vector<NodeInstance*>& result
) const {
    if (visited.find(node) != visited.end()) {
        return;
    }

    visited.insert(node);

    // Get direct dependencies
    auto directDeps = GetDirectDependencies(node);

    for (NodeInstance* dep : directDeps) {
        result.push_back(dep);
        GetAllDependenciesHelper(dep, visited, result);
    }
}

std::vector<NodeInstance*> GraphTopology::GetAllDependents(NodeInstance* node) const {
    std::set<NodeInstance*> visited;
    std::vector<NodeInstance*> result;
    std::queue<NodeInstance*> queue;

    queue.push(node);
    visited.insert(node);

    while (!queue.empty()) {
        NodeInstance* current = queue.front();
        queue.pop();

        auto directDeps = GetDirectDependents(current);
        for (NodeInstance* dep : directDeps) {
            if (visited.find(dep) == visited.end()) {
                visited.insert(dep);
                result.push_back(dep);
                queue.push(dep);
            }
        }
    }

    return result;
}

std::vector<GraphEdge> GraphTopology::GetIncomingEdges(NodeInstance* node) const {
    std::vector<GraphEdge> incoming;

    for (const auto& edge : edges) {
        if (edge.target == node) {
            incoming.push_back(edge);
        }
    }

    return incoming;
}

std::vector<GraphEdge> GraphTopology::GetOutgoingEdges(NodeInstance* node) const {
    std::vector<GraphEdge> outgoing;

    for (const auto& edge : edges) {
        if (edge.source == node) {
            outgoing.push_back(edge);
        }
    }

    return outgoing;
}

bool GraphTopology::ValidateGraph(std::string& errorMessage) const {
    // Check for cycles
    if (HasCycles()) {
        errorMessage = "Graph contains cycles";
        return false;
    }

    // Validate all edges have valid nodes
    for (const auto& edge : edges) {
        if (!edge.source || !edge.target) {
            errorMessage = "Graph contains null node references";
            return false;
        }

        if (nodes.find(edge.source) == nodes.end()) {
            errorMessage = "Edge references source node not in graph";
            return false;
        }

        if (nodes.find(edge.target) == nodes.end()) {
            errorMessage = "Edge references target node not in graph";
            return false;
        }
    }

    // Validate node connections match their schemas
    for (NodeInstance* node : nodes) {
        auto incoming = GetIncomingEdges(node);
        auto outgoing = GetOutgoingEdges(node);

        // Validate incoming edges (input slots)
        for (const auto& edge : incoming) {
            std::string slotError;
            if (!edge.target->ValidateInputSlot(edge.targetInputIndex, slotError)) {
                errorMessage = "Node " + node->GetInstanceName() +
                             " has edge to invalid input slot: " + slotError;
                return false;
            }
        }

        // Validate outgoing edges (output slots)
        for (const auto& edge : outgoing) {
            std::string slotError;
            if (!edge.source->ValidateOutputSlot(edge.sourceOutputIndex, slotError)) {
                errorMessage = "Node " + node->GetInstanceName() +
                             " has edge from invalid output slot: " + slotError;
                return false;
            }
        }
    }

    return true;
}

bool GraphTopology::IsConnected() const {
    if (nodes.empty()) {
        return true;
    }

    // Simple connectivity check: all nodes reachable from roots
    std::set<NodeInstance*> visited;
    std::queue<NodeInstance*> queue;

    // Start from all root nodes
    auto roots = GetRootNodes();
    for (NodeInstance* root : roots) {
        queue.push(root);
        visited.insert(root);
    }

    // BFS
    while (!queue.empty()) {
        NodeInstance* current = queue.front();
        queue.pop();

        auto dependents = GetDirectDependents(current);
        for (NodeInstance* dep : dependents) {
            if (visited.find(dep) == visited.end()) {
                visited.insert(dep);
                queue.push(dep);
            }
        }
    }

    return visited.size() == nodes.size();
}

} // namespace Vixen::RenderGraph
