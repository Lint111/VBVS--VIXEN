#include "RenderGraph/NodeInstance.h"
#include "VulkanResources/VulkanDevice.h"
#include <algorithm>
#include <functional>

namespace Vixen::RenderGraph {

NodeInstance::NodeInstance(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
)
    : instanceName(instanceName)
    , nodeType(nodeType)
    , device(device)
{
    // Allocate space for inputs and outputs based on schema
    if (nodeType) {
        inputs.resize(nodeType->GetInputCount(), nullptr);
        outputs.resize(nodeType->GetOutputCount(), nullptr);
    }

#ifdef _DEBUG
    nodeLogger = std::make_unique<Logger>(instanceName);
#endif
}

NodeInstance::~NodeInstance() {
    Cleanup();
}

NodeTypeId NodeInstance::GetTypeId() const {
    return nodeType ? nodeType->GetTypeId() : 0;
}

Resource* NodeInstance::GetInput(uint32_t index) const {
    if (index < inputs.size()) {
        return inputs[index];
    }
    return nullptr;
}

Resource* NodeInstance::GetOutput(uint32_t index) const {
    if (index < outputs.size()) {
        return outputs[index];
    }
    return nullptr;
}

void NodeInstance::SetInput(uint32_t index, Resource* resource) {
    if (index < inputs.size()) {
        inputs[index] = resource;
    }
}

void NodeInstance::SetOutput(uint32_t index, Resource* resource) {
    if (index < outputs.size()) {
        outputs[index] = resource;
    }
}

void NodeInstance::SetParameter(const std::string& name, const ParameterValue& value) {
    parameters[name] = value;
    
    // Invalidate cache when parameters change
    cacheKey = 0;
}

const ParameterValue* NodeInstance::GetParameter(const std::string& name) const {
    auto it = parameters.find(name);
    if (it != parameters.end()) {
        return &it->second;
    }
    return nullptr;
}

void NodeInstance::AddDependency(NodeInstance* node) {
    if (node && !DependsOn(node)) {
        dependencies.push_back(node);
    }
}

void NodeInstance::RemoveDependency(NodeInstance* node) {
    auto it = std::find(dependencies.begin(), dependencies.end(), node);
    if (it != dependencies.end()) {
        dependencies.erase(it);
    }
}

bool NodeInstance::DependsOn(NodeInstance* node) const {
    return std::find(dependencies.begin(), dependencies.end(), node) != dependencies.end();
}

VkDescriptorSet NodeInstance::GetDescriptorSet(uint32_t index) const {
    if (index < descriptorSets.size()) {
        return descriptorSets[index];
    }
    return VK_NULL_HANDLE;
}

void NodeInstance::SetDescriptorSet(VkDescriptorSet set, uint32_t index) {
    if (index >= descriptorSets.size()) {
        descriptorSets.resize(index + 1, VK_NULL_HANDLE);
    }
    descriptorSets[index] = set;
}

void NodeInstance::UpdatePerformanceStats(uint64_t executionTimeNs, uint64_t cpuTimeNs) {
    performanceStats.executionTimeNs = executionTimeNs;
    performanceStats.cpuTimeNs = cpuTimeNs;
    performanceStats.executionCount++;
    
    // Calculate running average
    float currentMs = executionTimeNs / 1000000.0f;
    if (performanceStats.executionCount == 1) {
        performanceStats.averageExecutionTimeMs = currentMs;
    } else {
        // Exponential moving average with alpha = 0.1
        performanceStats.averageExecutionTimeMs = 
            performanceStats.averageExecutionTimeMs * 0.9f + currentMs * 0.1f;
    }
}

uint64_t NodeInstance::ComputeCacheKey() const {
    // Simple hash combining type, parameters, and resource descriptions
    // This is a simplified version - production code would use proper hashing
    
    std::hash<std::string> hasher;
    uint64_t hash = hasher(instanceName);
    
    // Hash type ID
    hash ^= static_cast<uint64_t>(GetTypeId()) << 1;
    
    // Hash parameters
    for (const auto& [name, value] : parameters) {
        hash ^= hasher(name) << 2;
        
        // Hash parameter value based on type
        std::visit([&hash](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>) {
                hash ^= std::hash<std::string>{}(val);
            } else if constexpr (std::is_arithmetic_v<T>) {
                hash ^= std::hash<T>{}(val);
            }
        }, value);
    }
    
    // Hash input resource formats
    for (const auto* input : inputs) {
        if (input) {
            if (const auto* imgDesc = input->GetImageDescription()) {
                hash ^= static_cast<uint64_t>(imgDesc->format) << 3;
                hash ^= (static_cast<uint64_t>(imgDesc->width) << 4) | 
                        (static_cast<uint64_t>(imgDesc->height) << 5);
            }
        }
    }
    
    return hash;
}

#ifdef _DEBUG
void NodeInstance::RegisterToParentLogger(Logger* parentLogger)
{
    if (parentLogger) {
        parentLogger->AddChild(nodeLogger.get());
    }
}
void NodeInstance::DeregisterFromParentLogger(Logger* parentLogger)
{
    if (parentLogger) {
        parentLogger->RemoveChild(nodeLogger.get());
    }
}
#endif

void NodeInstance::AllocateResources() {
    // Calculate input memory footprint
    inputMemoryFootprint = 0;
    for (const auto* input : inputs) {
        if (input) {
            inputMemoryFootprint += input->GetMemorySize();
        }
    }
}

void NodeInstance::DeallocateResources() {
    // Cleanup descriptor sets (if owned by this node)
    descriptorSets.clear();
    
    // Command buffers are typically owned by a pool, don't free them here
    commandBuffers.clear();
    
    // Pipeline and layout are typically shared, don't destroy here
    pipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    
    state = NodeState::Created;
}

} // namespace Vixen::RenderGraph
