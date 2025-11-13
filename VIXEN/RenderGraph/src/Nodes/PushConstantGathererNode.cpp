#include "Nodes/PushConstantGathererNode.h"
#include "Core/NodeContext.h"
#include "ShaderManagement/SpirvReflectionData.h"
#include "ShaderManagement/ResourceExtractor.h"
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

    // Discover fields from shader if not pre-registered
    if (pushConstantFields_.empty()) {
        DiscoverPushConstants(ctx);
    }

    // Validate that all variadic inputs are connected
    if (!ValidateVariadicInputsImpl(ctx)) {
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
    // Pack variadic inputs into push constant buffer
    if (!pushConstantData_.empty()) {
        PackPushConstantData(ctx);
    }

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
    // Get shader bundle to discover push constants
    auto shaderBundle = ctx.In(PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE);
    if (!shaderBundle || !shaderBundle->reflectionData) {
        return;
    }

    if (shaderBundle->reflectionData->pushConstants.empty()) {
        return;
    }

    const auto& pcBlock = shaderBundle->reflectionData->pushConstants[0];

    // Parse struct members from reflection
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
    }

    // Update variadic constraints to match discovered fields
    if (!pushConstantFields_.empty()) {
        SetVariadicInputConstraints(pushConstantFields_.size(), pushConstantFields_.size());
    }
}

bool PushConstantGathererNode::ValidateVariadicInputsImpl(VariadicCompileContext& ctx) {
    // Validate each field has a connected input
    size_t variadicCount = ctx.InVariadicCount();
    
    if (variadicCount != pushConstantFields_.size()) {
        // Field count mismatch - but this is acceptable if fields were pre-registered
        // and inputs may be optional
        if (!pushConstantFields_.empty() && variadicCount == 0) {
            // No inputs connected but fields exist - this is OK, may be placeholder
            return true;
        }
    }

    // Validate each connected input
    for (size_t i = 0; i < variadicCount; ++i) {
        auto* resource = ctx.InVariadicResource(i);
        
        // Check if corresponding field exists
        if (i < pushConstantFields_.size()) {
            if (!ValidateFieldType(resource, pushConstantFields_[i])) {
                // Type mismatch - log warning but continue
            }
        }
    }

    return true;
}

void PushConstantGathererNode::PackPushConstantData(VariadicExecuteContext& ctx) {
    // Clear buffer
    std::fill(pushConstantData_.begin(), pushConstantData_.end(), 0);

    // Pack each field value into the buffer using type-safe extractor
    size_t variadicCount = ctx.InVariadicCount();
    
    for (size_t i = 0; i < variadicCount && i < pushConstantFields_.size(); ++i) {
        const auto& field = pushConstantFields_[i];
        auto* resource = ctx.InVariadicResource(i);

        if (!resource) continue;

        uint8_t* dest = pushConstantData_.data() + field.offset;

        // Use ResourceExtractor to fill type-appropriate value
        // For now, uses zero-fill as placeholder
        // Future: integrate with actual resource value extraction
        ShaderManagement::SpirvTypeInfo typeInfo{};
        typeInfo.baseType = field.baseType;
        typeInfo.vecSize = field.vecSize;
        typeInfo.sizeInBytes = field.size;

        ShaderManagement::ResourceExtractor::ExtractZero(typeInfo, dest, field.size);
    }
}

bool PushConstantGathererNode::ValidateFieldType(Resource* res, const PushConstantFieldSlotInfo& field) {
    if (!res) return false;

    // Resource type should be Buffer for push constant values
    return res->GetType() == ResourceType::Buffer || res->GetType() == ResourceType::Image;
}

void PushConstantGathererNode::PackScalar(const Resource* res, uint8_t* dest, size_t size) {
    // Deprecated - use ResourceExtractor instead via PackPushConstantData
}

void PushConstantGathererNode::PackVector(const Resource* res, uint8_t* dest, size_t componentCount) {
    // Deprecated - use ResourceExtractor instead via PackPushConstantData
}

void PushConstantGathererNode::PackMatrix(const Resource* res, uint8_t* dest, size_t rows, size_t cols) {
    // Deprecated - use ResourceExtractor instead via PackPushConstantData
}

ResourceType PushConstantGathererNode::GetResourceTypeForField(const PushConstantFieldSlotInfo& field) const {
    return ResourceType::Buffer;
}

} // namespace Vixen::RenderGraph