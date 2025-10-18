#pragma once

#include "Resource.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

// Forward declaration
class NodeInstance;

/**
 * @brief Unique identifier for node types
 */
using NodeTypeId = uint32_t;

/**
 * @brief Pipeline type enumeration
 */
enum class PipelineType {
    Graphics,
    Compute,
    RayTracing,
    Transfer
};

/**
 * @brief Device capability flags
 */
enum class DeviceCapability : uint32_t {
    None = 0,
    Graphics = 1 << 0,
    Compute = 1 << 1,
    Transfer = 1 << 2,
    RayTracing = 1 << 3,
    GeometryShader = 1 << 4,
    TessellationShader = 1 << 5,
    MeshShader = 1 << 6,
    MultiDrawIndirect = 1 << 7,
    DepthClamp = 1 << 8,
    FillModeNonSolid = 1 << 9
};

inline DeviceCapability operator|(DeviceCapability a, DeviceCapability b) {
    return static_cast<DeviceCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline DeviceCapability operator&(DeviceCapability a, DeviceCapability b) {
    return static_cast<DeviceCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasCapability(DeviceCapability flags, DeviceCapability check) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(check)) != 0;
}

using DeviceCapabilityFlags = DeviceCapability;

/**
 * @brief Workload metrics for scheduling
 */
struct WorkloadMetrics {
    // Space complexity
    size_t estimatedMemoryFootprint = 0;  // Bytes
    
    // Time complexity (relative units)
    float estimatedComputeCost = 1.0f;    // Relative to simple pass
    float estimatedBandwidthCost = 1.0f;  // Relative to simple pass
    
    // Parallelization potential
    bool canRunInParallel = true;
    uint32_t preferredBatchSize = 1;      // For instanced operations
};

/**
 * @brief Node Type - Template/Definition for a rendering process
 * 
 * Node Types define the blueprint for rendering operations.
 * Multiple NodeInstances can be created from a single NodeType.
 */
class NodeType {
public:
    NodeType() = default;
    virtual ~NodeType() = default;

    // Type identification
    NodeTypeId GetTypeId() const { return typeId; }
    const std::string& GetTypeName() const { return typeName; }

    // Schema
    const std::vector<ResourceDescriptor>& GetInputSchema() const { return inputSchema; }
    const std::vector<ResourceDescriptor>& GetOutputSchema() const { return outputSchema; }
    size_t GetInputCount() const { return inputSchema.size(); }
    size_t GetOutputCount() const { return outputSchema.size(); }

    // Requirements
    DeviceCapabilityFlags GetRequiredCapabilities() const { return requiredCapabilities; }
    PipelineType GetPipelineType() const { return pipelineType; }

    // Instancing
    bool SupportsInstancing() const { return supportsInstancing; }
    uint32_t GetMaxInstances() const { return maxInstances; }

    // Workload metrics
    const WorkloadMetrics& GetWorkloadMetrics() const { return workloadMetrics; }

    // Factory method
    virtual std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const = 0;

    // Validation
    virtual bool ValidateInputs(const std::vector<Resource*>& inputs) const;
    virtual bool ValidateOutputs(const std::vector<Resource*>& outputs) const;

protected:
    // To be set by derived classes
    NodeTypeId typeId = 0;
    std::string typeName;
    std::vector<ResourceDescriptor> inputSchema;
    std::vector<ResourceDescriptor> outputSchema;
    DeviceCapabilityFlags requiredCapabilities = DeviceCapability::None;
    PipelineType pipelineType = PipelineType::Graphics;
    bool supportsInstancing = true;
    uint32_t maxInstances = 0; // 0 = unlimited
    WorkloadMetrics workloadMetrics;
};

/**
 * @brief Helper class for building node types
 */
class NodeTypeBuilder {
public:
    NodeTypeBuilder& SetTypeId(NodeTypeId id) {
        typeId = id;
        return *this;
    }

    NodeTypeBuilder& SetTypeName(const std::string& name) {
        typeName = name;
        return *this;
    }

    NodeTypeBuilder& AddInput(const ResourceDescriptor& input) {
        inputSchema.push_back(input);
        return *this;
    }

    NodeTypeBuilder& AddOutput(const ResourceDescriptor& output) {
        outputSchema.push_back(output);
        return *this;
    }

    NodeTypeBuilder& SetPipelineType(PipelineType type) {
        pipelineType = type;
        return *this;
    }

    NodeTypeBuilder& SetRequiredCapabilities(DeviceCapabilityFlags caps) {
        requiredCapabilities = caps;
        return *this;
    }

    NodeTypeBuilder& SetSupportsInstancing(bool supports) {
        supportsInstancing = supports;
        return *this;
    }

    NodeTypeBuilder& SetMaxInstances(uint32_t max) {
        maxInstances = max;
        return *this;
    }

    NodeTypeBuilder& SetWorkloadMetrics(const WorkloadMetrics& metrics) {
        workloadMetrics = metrics;
        return *this;
    }

    // Getters for derived classes to use
    NodeTypeId GetTypeId() const { return typeId; }
    const std::string& GetTypeName() const { return typeName; }
    const std::vector<ResourceDescriptor>& GetInputSchema() const { return inputSchema; }
    const std::vector<ResourceDescriptor>& GetOutputSchema() const { return outputSchema; }
    PipelineType GetPipelineType() const { return pipelineType; }
    DeviceCapabilityFlags GetRequiredCapabilities() const { return requiredCapabilities; }
    bool GetSupportsInstancing() const { return supportsInstancing; }
    uint32_t GetMaxInstances() const { return maxInstances; }
    const WorkloadMetrics& GetWorkloadMetrics() const { return workloadMetrics; }

private:
    NodeTypeId typeId = 0;
    std::string typeName;
    std::vector<ResourceDescriptor> inputSchema;
    std::vector<ResourceDescriptor> outputSchema;
    PipelineType pipelineType = PipelineType::Graphics;
    DeviceCapabilityFlags requiredCapabilities = DeviceCapability::None;
    bool supportsInstancing = true;
    uint32_t maxInstances = 0;
    WorkloadMetrics workloadMetrics;
};

} // namespace Vixen::RenderGraph
