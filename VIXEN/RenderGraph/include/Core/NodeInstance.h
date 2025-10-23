#pragma once

#include "ResourceVariant.h"
#include "NodeType.h"
#include "Data/ParameterDataTypes.h"
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <memory>
#include "Logger.h"

// Forward declare Logger to avoid circular dependency
class Logger;

namespace Vixen::RenderGraph {

// Forward declarations
class RenderGraph;

/**
 * @brief Connection point for graph edges
 */
struct NodeConnection {
    NodeInstance* sourceNode = nullptr;
    uint32_t sourceOutputIndex = 0;
    NodeInstance* targetNode = nullptr;
    uint32_t targetInputIndex = 0;
};

/**
 * @brief Node Instance - Concrete instantiation of a NodeType
 * 
 * Represents a specific usage of a rendering operation in the graph.
 * Multiple instances can be created from the same NodeType.
 */
class NodeInstance {
    // Allow RenderGraph to access protected resource methods for graph wiring
    friend class RenderGraph;

public:
    NodeInstance(
        const std::string& instanceName,
        NodeType* nodeType
    );

    virtual ~NodeInstance();

    // Prevent copying
    NodeInstance(const NodeInstance&) = delete;
    NodeInstance& operator=(const NodeInstance&) = delete;

    // Identity
    const std::string& GetInstanceName() const { return instanceName; }
    NodeType* GetNodeType() const { return nodeType; }
    NodeTypeId GetTypeId() const;

    // Device affinity
    Vixen::Vulkan::Resources::VulkanDevice* GetDevice() const { return device; }
    uint32_t GetDeviceIndex() const { return deviceIndex; }
    void SetDeviceIndex(uint32_t index) { deviceIndex = index; }

    // Node arrayable flag
    bool AllowsInputArrays() const { return allowInputArrays; }
    void SetAllowInputArrays(bool allow) { allowInputArrays = allow; }

    // Resources (slot-based access)
    const std::vector<std::vector<Resource*>>& GetInputs() const { return inputs; }
    const std::vector<std::vector<Resource*>>& GetOutputs() const { return outputs; }

    // Get array size for a slot
    size_t GetInputCount(uint32_t slotIndex) const;
    size_t GetOutputCount(uint32_t slotIndex) const;

    // Parameters
    void SetParameter(const std::string& name, const ParamTypeValue& value);
    const ParamTypeValue* GetParameter(const std::string& name) const;
    template<typename T>
    T GetParameterValue(const std::string& name, const T& defaultValue = T{}) const;

    // Dependencies
    const std::vector<NodeInstance*>& GetDependencies() const { return dependencies; }
    void AddDependency(NodeInstance* node);
    void RemoveDependency(NodeInstance* node);
    bool DependsOn(NodeInstance* node) const;

    // State
    NodeState GetState() const { return state; }
    void SetState(NodeState newState) { state = newState; }

    // Execution order (set during compilation)
    uint32_t GetExecutionOrder() const { return executionOrder; }
    void SetExecutionOrder(uint32_t order) { executionOrder = order; }

    // Workload metrics
    size_t GetInputMemoryFootprint() const { return inputMemoryFootprint; }
    void SetInputMemoryFootprint(size_t size) { inputMemoryFootprint = size; }
    const PerformanceStats& GetPerformanceStats() const { return performanceStats; }
    void UpdatePerformanceStats(uint64_t executionTimeNs, uint64_t cpuTimeNs);

    // Caching
    uint64_t GetCacheKey() const { return cacheKey; }
    void SetCacheKey(uint64_t key) { cacheKey = key; }
    uint64_t ComputeCacheKey() const;

    // Logger Registration
    #ifdef _DEBUG
    void RegisterToParentLogger(Logger* parentLogger);
    void DeregisterFromParentLogger(Logger* parentLogger);
    #endif

    // Virtual methods for derived classes to implement
    virtual void Setup() {}
    virtual void Compile() {}
    virtual void Execute(VkCommandBuffer commandBuffer) = 0;
    virtual void Cleanup() {}

protected:
    // Low-level resource accessors (internal use by RenderGraph and TypedNodeInstance)
    // Node implementations should use In() and Out() from TypedNodeInstance instead
    Resource* GetInput(uint32_t slotIndex, uint32_t arrayIndex = 0) const;
    Resource* GetOutput(uint32_t slotIndex, uint32_t arrayIndex = 0) const;
    void SetInput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource);
    void SetOutput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource);

    // Instance identification
    std::string instanceName;
    NodeType* nodeType;
    

    // Device affinity
    Vixen::Vulkan::Resources::VulkanDevice* device;
    uint32_t deviceIndex = 0;

    // Node-level behavior flags
    // When true the node will accept either single inputs or array-shaped inputs
    // (IA<I>) and should handle producing scalar or array outputs accordingly.
    // Default false to preserve existing behavior.
    bool allowInputArrays = false;

    // Resources (each slot is a vector: scalar = size 1, array = size N)
    std::vector<std::vector<Resource*>> inputs;
    std::vector<std::vector<Resource*>> outputs;

    // Instance-specific parameters
    std::map<std::string, ParamTypeValue> parameters;

    // Execution state
    NodeState state = NodeState::Created;
    std::vector<NodeInstance*> dependencies;
    uint32_t executionOrder = 0;

    // Metrics
    size_t inputMemoryFootprint = 0;
    PerformanceStats performanceStats;

    // Caching
    uint64_t cacheKey = 0;

#ifdef _DEBUG
    // Debug-only hierarchical logger (zero overhead in release builds)
    std::unique_ptr<Logger> nodeLogger;
#endif

    // Helper methods
    void AllocateResources();
    void DeallocateResources();
};

// Template implementation
template<typename T>
T NodeInstance::GetParameterValue(const std::string& name, const T& defaultValue) const {
    auto it = parameters.find(name);
    if (it == parameters.end()) {
        return defaultValue;
    }
    
    if (auto* value = std::get_if<T>(&it->second)) {
        return *value;
    }
    
    return defaultValue;
}

} // namespace Vixen::RenderGraph
