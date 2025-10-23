#include "Core/NodeType.h"
#include "Core/NodeInstance.h"

namespace Vixen::RenderGraph {

NodeTypeId GenerateTypeId() {
    static NodeTypeId currentId = 0;
    return ++currentId;
}

Vixen::RenderGraph::NodeType::NodeType(const std::string &typeName) :
    typeName(typeName)
{
    if(typeName.empty()) {
        throw std::invalid_argument("NodeType name cannot be empty");
    }


    typeId = GenerateTypeId();
}

void NodeType::AddInputDescriptor(const ResourceDescriptor& descriptor) {
    inputSchema.push_back(descriptor.clone());
}
void NodeType::AddOutputDescriptor(const ResourceDescriptor& descriptor) {
    outputSchema.push_back(descriptor.clone());
}

void NodeType::AddParameterDefinition(const ParameterDefinition& parameter) {
    for (const auto& existingParam : parameterBundle) {
        if (existingParam.name == parameter.name) {
            throw std::invalid_argument("Parameter with name '" + parameter.name + "' already exists in NodeType '" + typeName + "'");
        }
    }
    parameterBundle.push_back(parameter.clone());
}

void NodeType::SetInputSchema(const Schema& schema) {
    inputSchema = schema.clone();
}

void NodeType::SetOutputSchema(const Schema& schema) {
    outputSchema = schema.clone();
}

void NodeType::SetParameterBundle(const ParameterBundle& params) {
    parameterBundle = params.clone();
}

void NodeType::SetParameterBundle(const ParameterBundle& params) {
    std::unordered_set<std::string> paramNames;
    for (const auto& param : params) {
        if (paramNames.find(param.name) != paramNames.end()) {
            throw std::invalid_argument("Duplicate parameter name '" + param.name + "' in NodeType '" + typeName + "'");
        }
        paramNames.insert(param.name);
    }
    parameterBundle = params;
}

const ResourceDescriptor* NodeType::GetInputDescriptor(uint32_t slotIndex) const {
    if (slotIndex >= inputSchema.size()) {
        return nullptr;
    }
    return &inputSchema[slotIndex];
}
const ResourceDescriptor* NodeType::GetOutputDescriptor(uint32_t slotIndex) const {
    if (slotIndex >= outputSchema.size()) {
        return nullptr;
    }
    return &outputSchema[slotIndex];
}
const ParameterDefinition* NodeType::GetParameterDefinition(const std::string& name) const {
    for (const auto& param : parameterBundle) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}
const ParameterDefinition* NodeType::GetParameterDefinition(uint32_t index) const {
    if (index >= parameterBundle.size()) {
        return nullptr;
    }
    return &parameterBundle[index];
}
const ParameterDefinition* NodeType::GetParameterDefinition(ParamType type) const {
    for (const auto& param : parameterBundle) {
        if (param.type == type) {
            return &param;
        }
    }
    return nullptr;
}

bool NodeType::CanConnectOutputToInput(uint32_t outputSlot, const NodeType& targetNodeType, uint32_t inputSlot) const {
    if (outputSlot >= outputSchema.size() || inputSlot >= targetNodeType.inputSchema.size()) {
        return false;
    }

    const auto& outputDesc = outputSchema[outputSlot];
    const auto& inputDesc = targetNodeType.inputSchema[inputSlot];

    // Basic type compatibility check
    if (outputDesc.type != inputDesc.type) {
        return false;
    }

    // Cant connect transient output to persistent input
    if(outputDesc.lifetime == ResourceLifetime::Transient &&
       inputDesc.lifetime == ResourceLifetime::Persistent) {
        return false;
    }
    return true;
}

bool NodeType::ValidateParameterTypes(const std::unordered_map<std::string, ParamTypeValue>& params) const {
    for (const auto& [name, value] : params) {
        const auto* paramDef = GetParameterDefinition(name);
        if (!paramDef) {
            return false; // Unknown parameter
        }
        if (!paramDef->ValidValue(value)) {
            return false; // Type mismatch
        }
    }
    return true;
}

bool NodeType::ValidateRequiredParameters(const std::unordered_map<std::string, ParamTypeValue>& params) const {
    for (const auto& paramDef : parameterBundle) {
        if (paramDef.required) {
            if (params.find(paramDef.name) == params.end()) {
                return false; // Missing required parameter
            }
        }
    }
    return true;
}

bool NodeType::ConsumeResourceType(ResourceType resourceType) const {
    for (const auto& inputDesc : inputSchema) {
        if (inputDesc.type == resourceType) {
            return true;
        }
    }
    return false;
}

bool NodeType::ProduceResourceType(ResourceType resourceType) const {
    for (const auto& outputDesc : outputSchema) {
        if (outputDesc.type == resourceType) {
            return true;
        }
    }
    return false;
}

} // namespace Vixen::RenderGraph
