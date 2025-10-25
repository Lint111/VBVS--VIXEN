#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

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

    CleanupNode(const std::string& name, CleanupCallback callback)
        : nodeName(name), cleanupCallback(std::move(callback)) {}

    /**
     * @brief Register a dependent cleanup that must run before this one
     * @param dependent Child node that depends on this node's resources
     */
    void AddDependent(std::shared_ptr<CleanupNode> dependent) {
        dependents.push_back(dependent);
    }

    /**
     * @brief Execute cleanup recursively: dependents first, then self
     */
    void ExecuteCleanup() {
        if (executed) {
            return; // Already cleaned up
        }

        // Clean up all dependents first (children before parents)
        for (auto& dependent : dependents) {
            if (auto dep = dependent.lock()) {
                dep->ExecuteCleanup();
            }
        }

        // Now clean up this node
        if (cleanupCallback) {
            cleanupCallback();
        }

        executed = true;
    }

    const std::string& GetName() const { return nodeName; }

private:
    std::string nodeName;
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
     * @param name Identifier for debugging
     * @param callback Cleanup function to execute
     * @param dependencyNames Names of nodes this cleanup depends on
     * @return Shared pointer to created CleanupNode
     */
    std::shared_ptr<CleanupNode> Register(
        const std::string& name,
        CleanupNode::CleanupCallback callback,
        const std::vector<std::string>& dependencyNames = {})
    {
        auto node = std::make_shared<CleanupNode>(name, std::move(callback));
        nodes[name] = node;

        // Link to dependencies
        for (const auto& depName : dependencyNames) {
            auto it = nodes.find(depName);
            if (it != nodes.end()) {
                // This node depends on depName, so depName must clean up AFTER this node
                // Therefore, this node is a dependent of depName
                it->second->AddDependent(node);
            }
        }

        return node;
    }

    /**
     * @brief Execute cleanup starting from root nodes (no dependencies)
     * Cleans up all registered nodes in dependency order
     */
    void ExecuteAll() {
        // Find root nodes (nodes with no dependencies registered as dependents)
        for (auto& [name, node] : nodes) {
            node->ExecuteCleanup();
        }
        nodes.clear();
    }

    /**
     * @brief Execute cleanup starting from a specific node
     * Only cleans up the specified node and its dependents
     * @param name Name of the cleanup node to start from
     */
    void ExecuteFrom(const std::string& name) {
        auto it = nodes.find(name);
        if (it != nodes.end()) {
            it->second->ExecuteCleanup();
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

private:
    std::unordered_map<std::string, std::shared_ptr<CleanupNode>> nodes;
};

} // namespace Vixen::RenderGraph
