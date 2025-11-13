#include "Nodes/PushConstantGathererNode.h"
#include "Core/NodeContext.h"
#include "ShaderManagement/SpirvReflectionData.h"
#include "NodeHelpers/ValidationHelpers.h"
#include <cstring>
#include <iostream>

using namespace RenderGraph::NodeHelpers;

namespace Vixen::RenderGraph {

// ============================================================================
// NODETYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> PushConstantGathererNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<PushConstantGathererNode>(instanceName, const_cast<PushConstantGathererNodeType*>(this));
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

PushConstantGathererNode::PushConstantGathererNode(
    const std::string& instanceName,
    NodeType* nodeType
) : VariadicTypedNode<PushConstantGathererNodeConfig>(instanceName, nodeType) {
    // Initialize with minimal variadic constraints - will be expanded during pre-registration
    SetVariadicInputConstraints(0, 32); // Max 32 fields by default
}

// ============================================================================
// PRE-REGISTRATION
// ============================================================================

void PushConstantGathererNode::PreRegisterPushConstantFields(const ShaderManagement::ShaderDataBundle* shaderBundle) {
    if (!shaderBundle || !shaderBundle->reflectionData) {
        std::cout << "[PushConstantGatherer] No shader bundle or reflection data for pre-registration\n";
        return;
    }

    if (shaderBundle->reflectionData->pushConstants.empty()) {
        std::cout << "[PushConstantGatherer] No push constants in shader\n";
        return;
    }

    // Get first push constant block (usually only one)
    const auto& pcBlock = shaderBundle->reflectionData->pushConstants[0];

    // Create slots for each field
    pushConstantFields_.clear();
    for (const auto& member : pcBlock.structDef.members) {
        PushConstantFieldSlotInfo fieldInfo;
        fieldInfo.fieldName = member.name;
        fieldInfo.offset = member.offset;
        fieldInfo.size = member.type.sizeInBytes;
        fieldInfo.baseType = member.type.baseType;
        fieldInfo.vecSize = member.type.vecSize;
        fieldInfo.dynamicInputIndex = pushConstantFields_.size();

        pushConstantFields_.push_back(fieldInfo);

        std::cout << "[PushConstantGatherer] Pre-registered field: " << member.name
                  << " (offset=" << member.offset << ", size=" << member.type.sizeInBytes << ")\n";
    }

    // Set variadic constraints
    if (!pushConstantFields_.empty()) {
        SetVariadicInputConstraints(pushConstantFields_.size(), pushConstantFields_.size());
    }
}

// ============================================================================
// SETUP
// ============================================================================

void PushConstantGathererNode::SetupImpl(VariadicSetupContext& ctx) {
    // Minimal setup - main work happens in Compile
}

// ============================================================================
// COMPILE
// ============================================================================

void PushConstantGathererNode::CompileImpl(VariadicCompileContext& ctx) {
    // Get shader bundle input using context API
    auto shaderBundle = ctx.In(PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE);
    if (!shaderBundle || !shaderBundle->reflectionData) {
        return;
    }

    // Extract push constant information and allocate buffer
    pushConstantRanges_.clear();
    pushConstantData_.clear();

    if (!shaderBundle->reflectionData->pushConstants.empty()) {
        const auto& pc = shaderBundle->reflectionData->pushConstants[0];

        VkPushConstantRange range{};
        range.stageFlags = pc.stageFlags;
        range.offset = pc.offset;
        range.size = pc.size;
        pushConstantRanges_.push_back(range);

        pushConstantData_.resize(pc.size, 0);
    }

    // Output pass-through
    ctx.Out(PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE_OUT, shaderBundle);
}

// ============================================================================
// EXECUTE
// ============================================================================

void PushConstantGathererNode::ExecuteImpl(VariadicExecuteContext& ctx) {
    // Clear and refill buffer from variadic inputs
    std::fill(pushConstantData_.begin(), pushConstantData_.end(), 0);

    // Output push constant data and ranges
    ctx.Out(PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA, pushConstantData_);
    ctx.Out(PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES, pushConstantRanges_);
}

// ============================================================================
// CLEANUP
// ============================================================================

void PushConstantGathererNode::CleanupImpl(VariadicCleanupContext& ctx) {
    pushConstantFields_.clear();
    pushConstantData_.clear();
    pushConstantRanges_.clear();
}

// ============================================================================
// HELPERS
// ============================================================================

void PushConstantGathererNode::DiscoverPushConstants(VariadicCompileContext& ctx) {
    // Placeholder - discovery happens in PreRegisterPushConstantFields
}

bool PushConstantGathererNode::ValidateVariadicInputsImpl(VariadicCompileContext& ctx) {
    // Placeholder validation - always pass for now
    return true;
}

void PushConstantGathererNode::PackPushConstantData(VariadicExecuteContext& ctx) {
    // Placeholder - data packing happens in Execute
}

bool PushConstantGathererNode::ValidateFieldType(Resource* res, const PushConstantFieldSlotInfo& field) {
    return res != nullptr;
}

void PushConstantGathererNode::PackScalar(const Resource* res, uint8_t* dest, size_t size) {
    // Placeholder
}

void PushConstantGathererNode::PackVector(const Resource* res, uint8_t* dest, size_t componentCount) {
    // Placeholder
}

void PushConstantGathererNode::PackMatrix(const Resource* res, uint8_t* dest, size_t rows, size_t cols) {
    // Placeholder
}

ResourceType PushConstantGathererNode::GetResourceTypeForField(const PushConstantFieldSlotInfo& field) const {
    return ResourceType::Buffer;
}

} // namespace Vixen::RenderGraph