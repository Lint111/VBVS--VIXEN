#pragma once

#include "ResourceVariant.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "Data/BasicDataTypes.h"
#include "Data/ParameterDataTypes.h"
#include "Data/LayerAndExtensionRequirementData.h"

namespace Vixen::RenderGraph {

// Forward declaration
class NodeInstance;

using ParameterBundle = std::vector<ParameterDefinition>;
using Schema = std::vector<ResourceDescriptor>;

using FeatureSet = std::vector<Feature>;


/**
 * @brief Node Type - Template/Definition for a rendering process
 * 
 * Node Types define the blueprint for rendering operations.
 * Multiple NodeInstances can be created from a single NodeType.
 */
class NodeType {
public:
    NodeType(const std::string& typeName);
    virtual ~NodeType() = default;

    // Identity and metadata
    const std::string& GetTypeName() const { return typeName; }
    const NodeTypeId GetTypeId() const { return typeId; }
    const std::string& GetDescription() const { return description; }
    void SetDescription(const std::string& desc) { description = desc; }

    //Category and organization
    const std::string& GetCategory() const { return category; }
    void SetCategory(const std::string& cat) { category = cat; }

    uint32_t GetVersion() const { return version; }
    void SetVersion(uint32_t ver) { version = ver; }

    // Type definition access
    const Schema& GetInputSchema() const { return inputSchema; }
    const Schema& GetOutputSchema() const { return outputSchema; }
    const ParameterBundle& GetParameterBundle() const { return parameterBundle; }

    void SetInputSchema(const Schema& schema);
    void SetOutputSchema(const Schema& schema);
    void SetParameterBundle(const ParameterBundle& params);

    // Slot information
    size_t GetInputCount() const { return inputSchema.size(); }
    size_t GetOutputCount() const { return outputSchema.size(); }
    size_t GetParameterCount() const { return parameterBundle.size(); }

    const ResourceDescriptor* GetInputDescriptor(uint32_t slotIndex) const;
    const ResourceDescriptor* GetOutputDescriptor(uint32_t slotIndex) const;
    
    const ParameterDefinition* GetParameterDefinition(const std::string& name) const;
    const ParameterDefinition* GetParameterDefinition(uint32_t index) const;
    const ParameterDefinition* GetParameterDefinition(ParamType type) const;

    // Type-level validation
    bool CanConnectOutputToInput(uint32_t outputSlot, const NodeType& targetNodeType, uint32_t inputSlot) const;
    bool ValidateParameterTypes(const std::unordered_map<std::string, ParamTypeValue>& params) const;
    bool ValidateRequiredParameters(const std::unordered_map<std::string, ParamTypeValue>& params) const;
    

    // Resource type compatibility check
    bool ConsumeResourceType(ResourceType resourceType) const;
    bool ProduceResourceType(ResourceType resourceType) const;

    // Requirements
    DeviceCapabilityFlags GetRequiredCapabilities() const { return requiredCapabilities; }
    PipelineType GetPipelineType() const { return pipelineType; }
    NodeFeatureProfile GetFeatureProfile() const { return featureProfile; }

    // Instancing
    bool SupportsInstancing() const { return supportsInstancing; }
    uint32_t GetMaxInstances() const { return maxInstances; }
    virtual std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const = 0;

    // performance hints (for graph compilation/scheduling)
    const WorkloadMetrics& GetWorkloadMetrics() const { return workloadMetrics; }
    bool GetAllowInputArrays() const { return allowInputArrays; }


    // Validation
    virtual bool ValidateInputs(const std::vector<Resource*>& inputs) const;
    virtual bool ValidateOutputs(const std::vector<Resource*>& outputs) const;

protected:
    // Identity
    NodeTypeId typeId = 0;
    std::string typeName = "UnnamedNodeType";
    std::string description = "No description provided.";
    std::string category = "Uncategorized";
    uint32_t version = 1;
    uint32_t maxInstances = 0; // 0 = unlimited

    // Execution requirements
    DeviceCapabilityFlags requiredCapabilities = DeviceCapability::None;
    PipelineType pipelineType = PipelineType::None;
    NodeFeatureProfile featureProfile;


    // Type definitions
    Schema inputSchema;
    Schema outputSchema;
    ParameterBundle parameterBundle;

#ifdef _DEBUG
    // Performance hints
    WorkloadMetrics workloadMetrics;
#endif

    // Node-level flag: allow array-shaped inputs (IA<I>) for nodes that can process arrays
    bool allowInputArrays = false;
    bool supportsInstancing = true;

};

} // namespace Vixen::RenderGraph
