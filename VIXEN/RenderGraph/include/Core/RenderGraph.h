#pragma once

#include "Headers.h"

#include "NodeInstance.h"
#include "NodeTypeRegistry.h"
#include "GraphTopology.h"
#include "ResourceVariant.h"
#include "CleanupStack.h"
#include "ResourceDependencyTracker.h"
#include "GraphMessages.h"
#include "EventBus/MessageBus.h"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

/**
 * @brief Handle for referencing nodes in the graph
 */
struct NodeHandle {
    uint32_t index = UINT32_MAX;
    
    bool IsValid() const { return index != UINT32_MAX; }
    bool operator==(const NodeHandle& other) const { return index == other.index; }
    bool operator!=(const NodeHandle& other) const { return index != other.index; }
};

/**
 * @brief Main Render Graph class
 * 
 * Orchestrates the entire render graph system:
 * - Graph construction
 * - Compilation and optimization
 * - Resource management
 * - Execution
 */
class RenderGraph {
public:
    /**
     * @brief Construct a new Render Graph
     * @param registry The node type registry
     * @param messageBus Event bus for graph events (optional)
     * @param mainLogger Optional logger for debug output (in debug builds)
     */
    explicit RenderGraph(
        NodeTypeRegistry* registry,
        EventBus::MessageBus* messageBus = nullptr,
        Logger* mainLogger = nullptr
    );

    ~RenderGraph();

    // Prevent copying
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // ====== Graph Building ======

    /**
     * @brief Add a node to the graph
     * @param typeName The name of the node type
     * @param instanceName Unique name for this instance
     * @return Handle to the created node
     */
    NodeHandle AddNode(const std::string& typeName, const std::string& instanceName);

    /**
     * @brief Add a node using type ID
     */
    NodeHandle AddNode(NodeTypeId typeId, const std::string& instanceName);

    /**
     * @brief Connect two nodes (resource dependency)
     * @param from Source node handle
     * @param outputIdx Output index from source
     * @param to Target node handle
     * @param inputIdx Input index to target
     */
    void ConnectNodes(
        NodeHandle from, 
        uint32_t outputIdx,
        NodeHandle to, 
        uint32_t inputIdx
    );

    /**
     * @brief Remove a node from the graph
     */
    void RemoveNode(NodeHandle handle);

    /**
     * @brief Clear the entire graph
     */
    void Clear();

    // ====== Compilation ======

    /**
     * @brief Compile the graph
     * 
     * Performs:
     * - Dependency analysis
     * - Resource allocation
     * - Pipeline creation
     * - Command buffer generation
     */
    void Compile();

    /**
     * @brief Check if graph is compiled
     */
    bool IsCompiled() const { return isCompiled; }

    // ====== Execution ======

    /**
     * @brief Execute the render graph (low-level)
     * @param commandBuffer Command buffer to record into
     *
     * NOTE: This is a low-level method for recording into an external command buffer.
     * For full frame rendering, use RenderFrame() instead.
     */
    void Execute(VkCommandBuffer commandBuffer);

    /**
     * @brief Render a complete frame
     *
     * High-level method that handles the full render loop:
     * - Acquires swapchain image
     * - Allocates/records command buffer
     * - Submits with semaphores
     * - Presents to swapchain
     *
     * @return VkResult from presentation (VK_SUCCESS, VK_ERROR_OUT_OF_DATE_KHR, etc.)
     */
    VkResult RenderFrame();

    // ====== Query ======

    /**
     * @brief Get a node instance by handle
     */
    NodeInstance* GetInstance(NodeHandle handle);
    const NodeInstance* GetInstance(NodeHandle handle) const;

    /**
     * @brief Get a node instance by name
     */
    NodeInstance* GetInstanceByName(const std::string& name);
    const NodeInstance* GetInstanceByName(const std::string& name) const;

    /**
     * @brief Get all instances of a specific type
     */
    std::vector<NodeInstance*> GetInstancesOfType(NodeTypeId typeId) const;

    /**
     * @brief Get instance count of a specific type
     */
    uint32_t GetInstanceCount(NodeTypeId typeId) const;

    /**
     * @brief Get total node count
     */
    size_t GetNodeCount() const { return instances.size(); }

    /**
     * @brief Get execution order (after compilation)
     */
    const std::vector<NodeInstance*>& GetExecutionOrder() const { return executionOrder; }

    /**
     * @brief Get the graph topology
     */
    const GraphTopology& GetTopology() const { return topology; }

    // ====== Cleanup Management ======

    /**
     * @brief Get the cleanup stack for registering cleanup callbacks
     * Nodes should register their cleanup during Compile()
     */
    CleanupStack& GetCleanupStack() { return cleanupStack; }

    /**
     * @brief Get the resource dependency tracker
     * Used internally to build automatic cleanup dependencies
     */
    ResourceDependencyTracker& GetDependencyTracker() { return dependencyTracker; }

    /**
     * @brief Execute all cleanup callbacks in dependency order
     * Called during graph destruction or manual cleanup
     */
    void ExecuteCleanup();

    /**
     * @brief Execute partial cleanup starting from a specific node
     * 
     * Recursively cleans the node and its dependencies (moving backwards
     * toward producers). Only cleans dependencies if no other nodes use them
     * (reference count becomes zero).
     * 
     * @param rootNodeName Name of the node to start cleanup from
     * @return Number of nodes cleaned
     */
    size_t CleanupSubgraph(const std::string& rootNodeName);

    /**
     * @brief Cleanup nodes matching a tag
     * @param tag Tag to match (e.g., "shadow-maps")
     * @return Number of nodes cleaned
     */
    size_t CleanupByTag(const std::string& tag);

    /**
     * @brief Cleanup all nodes of a specific type
     * @param typeName Node type name (e.g., "GeometryPass")
     * @return Number of nodes cleaned
     */
    size_t CleanupByType(const std::string& typeName);

    /**
     * @brief Preview which nodes would be cleaned (dry-run)
     * 
     * @param rootNodeName Name of the node to analyze
     * @return Vector of node names that would be cleaned
     */
    std::vector<std::string> GetCleanupScope(const std::string& rootNodeName) const;

    // ====== Validation ======

    /**
     * @brief Validate the graph
     * @param errorMessage Output error message if validation fails
     * @return true if valid, false otherwise
     */
    bool Validate(std::string& errorMessage) const;

    
private:
    // Core components
    NodeTypeRegistry* typeRegistry;
    EventBus::MessageBus* messageBus = nullptr;  // Non-owning pointer
    EventBus::SubscriptionID cleanupEventSubscription = 0;
    // Vixen::Vulkan::Resources::VulkanDevice* primaryDevice;  // Removed - nodes access device directly

    #ifdef _DEBUG
    // Debug logger (non-owning pointer — application owns the logger)
    Logger* mainLogger = nullptr;
    #endif

    // Graph data
    std::vector<std::unique_ptr<NodeInstance>> instances;
    std::map<std::string, NodeHandle> nameToHandle;
    std::map<NodeTypeId, std::vector<NodeInstance*>> instancesByType;
    
    // Resources (lifetime management only - nodes are the logical containers)
    // This vector owns all Resource objects created by the graph. Nodes hold raw
    // pointers to these resources via their inputs/outputs vectors. This centralized
    // ownership enables future optimizations like memory aliasing and resource pooling.
    std::vector<std::unique_ptr<Resource>> resources;

    // Topology
    GraphTopology topology;

    // Execution
    std::vector<NodeInstance*> executionOrder;
    bool isCompiled = false;

    // Cleanup management
    CleanupStack cleanupStack;
    ResourceDependencyTracker dependencyTracker;
    std::unordered_map<NodeInstance*, size_t> dependentCounts;  // Reference counting for partial cleanup

    // Compilation phases
    void AnalyzeDependencies();
    void AllocateResources();
    void GeneratePipelines();
    void BuildExecutionOrder();

    // Cleanup helpers
    void ComputeDependentCounts();
    void RecursiveCleanup(NodeInstance* node, std::set<NodeInstance*>& cleaned);
    void HandleCleanupRequest(const CleanupRequestedMessage& msg);

    // Helpers
    NodeHandle CreateHandle(uint32_t index);
    NodeInstance* GetInstanceInternal(NodeHandle handle);
    Resource* CreateResourceForOutput(NodeInstance* node, uint32_t outputIndex);
};

} // namespace Vixen::RenderGraph
