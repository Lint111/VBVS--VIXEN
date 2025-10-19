#pragma once

#include "Headers.h"

#include "NodeInstance.h"
#include "NodeTypeRegistry.h"
#include "GraphTopology.h"
#include "Resource.h"
#include <memory>
#include <string>
#include <vector>
#include <map>

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
     * @param primaryDevice The primary Vulkan device
     * @param registry The node type registry
     * @param mainLogger Optional logger for debug output (in debug builds)
     */
    explicit RenderGraph(
        Vixen::Vulkan::Resources::VulkanDevice* primaryDevice,
        NodeTypeRegistry* registry,
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
     * @brief Add a node to the graph with specific device
     * @param typeName The name of the node type
     * @param instanceName Unique name for this instance
     * @param device The device this node should execute on
     * @return Handle to the created node
     */
    NodeHandle AddNode(
        const std::string& typeName,
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );

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

    // ====== Validation ======

    /**
     * @brief Validate the graph
     * @param errorMessage Output error message if validation fails
     * @return true if valid, false otherwise
     */
    bool Validate(std::string& errorMessage) const;

    // ====== Device Management ======

    /**
     * @brief Get the primary device
     */
    Vixen::Vulkan::Resources::VulkanDevice* GetPrimaryDevice() const { return primaryDevice; }

    /**
     * @brief Get all devices used by the graph
     */
    const std::vector<Vixen::Vulkan::Resources::VulkanDevice*>& GetUsedDevices() const { 
        return usedDevices; 
    }

private:
    // Core components
    NodeTypeRegistry* typeRegistry;
    Vixen::Vulkan::Resources::VulkanDevice* primaryDevice;
    std::vector<Vixen::Vulkan::Resources::VulkanDevice*> usedDevices;

    #ifdef _DEBUG
    // Debug logger (non-owning pointer — application owns the logger)
    Logger* mainLogger = nullptr;
    #endif

    // Graph data
    std::vector<std::unique_ptr<NodeInstance>> instances;
    std::map<std::string, NodeHandle> nameToHandle;
    std::map<NodeTypeId, std::vector<NodeInstance*>> instancesByType;
    
    // Resources
    std::vector<std::unique_ptr<Resource>> resources;

    // Topology
    GraphTopology topology;

    // Execution
    std::vector<NodeInstance*> executionOrder;
    bool isCompiled = false;

    // Compilation phases
    void PropagateDeviceAffinity();
    void AnalyzeDependencies();
    void AllocateResources();
    void GeneratePipelines();
    void BuildExecutionOrder();

    // Helpers
    NodeHandle CreateHandle(uint32_t index);
    NodeInstance* GetInstanceInternal(NodeHandle handle);
    Resource* CreateResourceForOutput(NodeInstance* node, uint32_t outputIndex);
};

} // namespace Vixen::RenderGraph
