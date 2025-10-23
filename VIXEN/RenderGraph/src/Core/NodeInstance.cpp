#include "Core/NodeInstance.h"
#include "VulkanResources/VulkanDevice.h"
#include <algorithm>
#include <functional>

namespace Vixen::RenderGraph {

NodeInstance::NodeInstance(
    const std::string& instanceName,
    NodeType* nodeType
)
    : instanceName(instanceName)
    , nodeType(nodeType)
{
    // Allocate space for inputs and outputs based on schema
    // Each slot is a vector (size 1 for scalar, size N for array)
    if (nodeType) {
        inputs.resize(nodeType->GetInputCount());
        outputs.resize(nodeType->GetOutputCount());

        // Initialize each slot with empty vector (will be populated during connection/execution)
        for (auto& slot : inputs) {
            slot = {};
        }
        for (auto& slot : outputs) {
            slot = {};
        }

        // Initialize node-level behavior flags from type
        allowInputArrays = nodeType->GetAllowInputArrays();
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

Resource* NodeInstance::GetInput(uint32_t slotIndex, uint32_t arrayIndex) const {
    if (slotIndex < inputs.size() && arrayIndex < inputs[slotIndex].size()) {
        return inputs[slotIndex][arrayIndex];
    }
    return nullptr;
}

Resource* NodeInstance::GetOutput(uint32_t slotIndex, uint32_t arrayIndex) const {
    if (slotIndex < outputs.size() && arrayIndex < outputs[slotIndex].size()) {
        return outputs[slotIndex][arrayIndex];
    }
    return nullptr;
}

void NodeInstance::SetInput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource) {
    if (slotIndex < inputs.size()) {
        if (arrayIndex >= inputs[slotIndex].size()) {
            inputs[slotIndex].resize(arrayIndex + 1, nullptr);
        }
        inputs[slotIndex][arrayIndex] = resource;
    }
}

void NodeInstance::SetOutput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource) {
    if (slotIndex < outputs.size()) {
        if (arrayIndex >= outputs[slotIndex].size()) {
            outputs[slotIndex].resize(arrayIndex + 1, nullptr);
        }
        outputs[slotIndex][arrayIndex] = resource;
    }
}

size_t NodeInstance::GetInputCount(uint32_t slotIndex) const {
    if (slotIndex < inputs.size()) {
        return inputs[slotIndex].size();
    }
    return 0;
}

size_t NodeInstance::GetOutputCount(uint32_t slotIndex) const {
    if (slotIndex < outputs.size()) {
        return outputs[slotIndex].size();
    }
    return 0;
}

void NodeInstance::SetParameter(const std::string& name, const ParamTypeValue& value) {
    parameters[name] = value;
    
    // Invalidate cache when parameters change
    cacheKey = 0;
}

const ParamTypeValue* NodeInstance::GetParameter(const std::string& name) const {
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
    
    // Hash input resource formats (iterate through 2D vector)
    for (const auto& inputSlot : inputs) {
        for (const auto* input : inputSlot) {
            if (input) {
                if (const auto* imgDesc = input->GetImageDescription()) {
                    hash ^= static_cast<uint64_t>(imgDesc->format) << 3;
                    hash ^= (static_cast<uint64_t>(imgDesc->width) << 4) |
                            (static_cast<uint64_t>(imgDesc->height) << 5);
                }
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
    // Calculate input memory footprint (iterate through 2D vector)
    inputMemoryFootprint = 0;
    for (const auto& inputSlot : inputs) {
        for (const auto* input : inputSlot) {
            if (input) {
                inputMemoryFootprint += input->GetMemorySize();
            }
        }
    }
}

void NodeInstance::DeallocateResources() {
    // Resources are now managed via the graph's resource system
    // Nothing to deallocate here
    state = NodeState::Created;
}

} // namespace Vixen::RenderGraph
