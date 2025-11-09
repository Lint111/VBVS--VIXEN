#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace Vixen::RenderGraph {

// NodeHandle definition (lightweight, safe to include)
struct NodeHandle {
    uint32_t index = UINT32_MAX;

    bool IsValid() const { return index != UINT32_MAX; }
    bool operator==(const NodeHandle& other) const { return index == other.index; }
    bool operator!=(const NodeHandle& other) const { return index != other.index; }
    bool operator<(const NodeHandle& other) const { return index < other.index; }
};

} // namespace Vixen::RenderGraph

// VixenHash function for NodeHandle to enable use in unordered_set/unordered_map
namespace std {
    template<>
    struct hash<Vixen::RenderGraph::NodeHandle> {
        size_t operator()(const Vixen::RenderGraph::NodeHandle& handle) const {
            return std::hash<uint32_t>{}(handle.index);
        }
    };
}

namespace Vixen::RenderGraph {

/**
 * @brief Represents a single cleanup action with its dependencies
 * 
 * CleanupNodes form a dependency tree where child nodes must be
 * cleaned up before their parent dependencies.
 */
class CleanupNode {
public:
    using CleanupCallback = std::function<void()>;

    CleanupNode(NodeHandle handle, const std::string& name, CleanupCallback callback)
        : nodeHandle(handle), nodeName(name), cleanupCallback(std::move(callback)) {}

    /**
     * @brief Register a dependent cleanup that must run before this one
     * @param dependent Child node that depends on this node's resources
     */
    void AddDependent(std::shared_ptr<CleanupNode> dependent) {
        dependents.push_back(dependent);
    }

    /**
     * @brief Execute cleanup recursively: dependents first, then self
     * @param visited Set of already-visited nodes to prevent duplicate execution
     */
    void ExecuteCleanup(std::unordered_set<NodeHandle>* visited = nullptr) {
        // Use local set if not provided (top-level call)
        std::unordered_set<NodeHandle> localVisited;
        if (!visited) {
            visited = &localVisited;
        }

        // Check if already visited in this recursive traversal
        if (visited->count(nodeHandle) > 0) {
            return; // Already cleaned in this traversal
        }
        visited->insert(nodeHandle);

        // Also check the executed flag (for cases where cleanup was called multiple times)
        if (executed) {
            return; // Already cleaned up
        }

        // Clean up all dependents first (children before parents)
        for (auto& dependent : dependents) {
            if (auto dep = dependent.lock()) {
                dep->ExecuteCleanup(visited);
            }
        }

        // Now clean up this node
        if (cleanupCallback) {
            cleanupCallback();
        }

        executed = true;
    }

    const std::string& GetName() const { return nodeName; }
    NodeHandle GetHandle() const { return nodeHandle; }

    /**
     * @brief Update the cleanup callback for this node (used when a placeholder was created earlier)
     */
    void SetCallback(CleanupCallback cb) {
        cleanupCallback = std::move(cb);
    }

    /**
     * @brief Reset the executed flag to allow cleanup to run again after recompilation
     * Used when a node is recompiled and creates new resources that need cleanup
     */
    void ResetExecuted() {
        executed = false;
    }

    /**
     * @brief Recursively collect all dependent node handles
     * @param outHandles Set to populate with dependent handles (prevents duplicates)
     */
    void CollectDependentHandles(std::unordered_set<NodeHandle>& outHandles) const {
        for (const auto& weakDep : dependents) {
            if (auto dep = weakDep.lock()) {
                // Add this dependent
                outHandles.insert(dep->GetHandle());
                // Recursively collect its dependents
                dep->CollectDependentHandles(outHandles);
            }
        }
    }

private:
    NodeHandle nodeHandle;
    std::string nodeName; // Keep for debugging
    CleanupCallback cleanupCallback;
    std::vector<std::weak_ptr<CleanupNode>> dependents;
    bool executed = false;
};

/**
 * @brief Manages dependency-aware cleanup for RenderGraph resources
 * 
 * The CleanupStack ensures that Vulkan resources are destroyed in the
 * correct order - child objects before their parent dependencies.
 * 
 * Example:
 *   DeviceNode creates VkDevice
 *   SwapChainNode uses VkDevice, creates VkSwapchainKHR and VkSemaphores
 *   
 *   Cleanup order:
 *   1. SwapChainNode destroys VkSemaphores, VkSwapchainKHR
 *   2. DeviceNode destroys VkDevice
 */
class CleanupStack {
public:
    /**
     * @brief Register a cleanup action with optional dependencies
     * @param handle Handle to the node instance
     * @param name Identifier for debugging
     * @param callback Cleanup function to execute
     * @param dependencyHandles Handles of nodes this cleanup depends on
     * @return Shared pointer to created CleanupNode
     */
    std::shared_ptr<CleanupNode> Register(
        NodeHandle handle,
        const std::string& name,
        CleanupNode::CleanupCallback callback,
        const std::vector<NodeHandle>& dependencyHandles = {})
    {
        // If a node with this handle already exists (placeholder), update its callback
        auto itExisting = nodes.find(handle);
        std::shared_ptr<CleanupNode> node;
        if (itExisting != nodes.end()) {
            node = itExisting->second;
            // Update placeholder callback
            node->SetCallback(std::move(callback));
        } else {
            node = std::make_shared<CleanupNode>(handle, name, std::move(callback));
            nodes[handle] = node;
        }

        // Link to dependencies. If a dependency isn't registered yet, create a placeholder
        // node so that dependents can be linked regardless of registration order.
        for (const auto& depHandle : dependencyHandles) {
            auto it = nodes.find(depHandle);
            if (it == nodes.end()) {
                // Create placeholder with empty callback
                auto placeholder = std::make_shared<CleanupNode>(depHandle, "", []() {});
                nodes[depHandle] = placeholder;
                it = nodes.find(depHandle);
            }

            // This node depends on depHandle, so depHandle must clean up AFTER this node
            // Therefore, this node is a dependent of depHandle
            it->second->AddDependent(node);
        }

        return node;
    }

    /**
     * @brief Execute cleanup starting from root nodes (no dependencies)
     * Cleans up all registered nodes in dependency order
     */
    void ExecuteAll() {
        // Execute cleanup for all nodes (duplicate execution prevented by visited tracking)
        for (auto& [handle, node] : nodes) {
            node->ExecuteCleanup();
        }
        nodes.clear();
    }

    /**
     * @brief Execute cleanup starting from a specific node
     * Only cleans up the specified node and its dependents
     * @param handle Handle of the cleanup node to start from
     */
    void ExecuteFrom(NodeHandle handle) {
        auto it = nodes.find(handle);
        if (it != nodes.end()) {
            it->second->ExecuteCleanup();
        }
    }

    /**
     * @brief Reset the executed flag for a node to allow cleanup to run again
     * Call this when a node is recompiled and needs its cleanup to run again
     * @param handle Handle of the node to reset
     */
    void ResetExecuted(NodeHandle handle) {
        auto it = nodes.find(handle);
        if (it != nodes.end()) {
            it->second->ResetExecuted();
        }
    }

    /**
     * @brief Clear all registered cleanup actions without executing them
     * WARNING: Only use if manual cleanup was performed
     */
    void Clear() {
        nodes.clear();
    }

    /**
     * @brief Get number of registered cleanup actions
     */
    size_t GetNodeCount() const {
        return nodes.size();
    }

    /**
     * @brief Get all nodes that depend on the specified node (recursively)
     *
     * Returns all downstream dependents in the cleanup graph.
     * Since cleanup goes from dependents -> providers,
     * this returns nodes that would be cleaned BEFORE the specified node.
     *
     * @param handle Handle of the node to query dependents for
     * @return Set of dependent node handles (empty if node not found)
     */
    std::unordered_set<NodeHandle> GetAllDependents(NodeHandle handle) const {
        std::unordered_set<NodeHandle> dependents;
        auto it = nodes.find(handle);
        if (it != nodes.end()) {
            it->second->CollectDependentHandles(dependents);
        }
        return dependents;
    }

private:
    std::unordered_map<NodeHandle, std::shared_ptr<CleanupNode>> nodes;
};

} // namespace Vixen::RenderGraph
