#include "Nodes/PushConstantGathererNode.h"
#include "Core/ExecutionContext.h"
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

    // Base slot defined in config - no need to define here
    // Variadic slots will be added dynamically based on shader reflection

    // Set maximum number of variadic slots
    // Support up to 32 push constant fields (128 bytes / 4 bytes avg)
    SetMaxVariadicSlotCount(32);

    // Initialize with no variadic constraints (will be set during pre-registration or setup)
    SetVariadicInputConstraints(0, 0);
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
        fieldInfo.size = member.size;
        fieldInfo.baseType = member.baseType;
        fieldInfo.vecSize = member.vecSize;
        fieldInfo.dynamicInputIndex = pushConstantFields_.size();

        pushConstantFields_.push_back(fieldInfo);

        // Register variadic slot
        VariadicSlotInfo slot;
        slot.slotName = member.name;
        slot.resource = nullptr;
        slot.resourceType = GetResourceTypeForField(fieldInfo);

        RegisterVariadicSlot(slot, 0);  // Bundle 0

        std::cout << "[PushConstantGatherer] Pre-registered field: " << member.name
                  << " (offset=" << member.offset << ", size=" << member.size << ")\n";
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
    std::cout << "[PushConstantGatherer] Setup phase\n";

    // If no fields pre-registered, we'll discover them during compile
    if (pushConstantFields_.empty()) {
        std::cout << "[PushConstantGatherer] No fields pre-registered, will discover from shader\n";
    }
}

// ============================================================================
// COMPILE
// ============================================================================

void PushConstantGathererNode::CompileImpl(VariadicCompileContext& ctx) {
    std::cout << "[PushConstantGatherer] Compile phase\n";

    // Validate shader bundle input
    auto* bundleRes = GetInputResource(PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE);
    if (!bundleRes) {
        ctx.ReportError("No shader bundle connected");
        return;
    }

    auto* shaderBundle = bundleRes->As<ShaderManagement::ShaderDataBundle>();
    if (!shaderBundle || !shaderBundle->reflectionData) {
        ctx.ReportError("Shader bundle missing reflection data");
        return;
    }

    // Discover push constants from reflection if not pre-registered
    if (pushConstantFields_.empty()) {
        DiscoverPushConstants(ctx);
    }

    // Validate variadic inputs match shader requirements
    if (!ValidateVariadicInputsImpl(ctx)) {
        return;  // Error already reported
    }

    // Extract push constant ranges from reflection
    pushConstantRanges_.clear();
    if (!shaderBundle->reflectionData->pushConstants.empty()) {
        const auto& pc = shaderBundle->reflectionData->pushConstants[0];

        VkPushConstantRange range{};
        range.stageFlags = pc.stageFlags;
        range.offset = pc.offset;
        range.size = pc.size;
        pushConstantRanges_.push_back(range);

        // Allocate push constant data buffer
        pushConstantData_.resize(pc.size, 0);

        std::cout << "[PushConstantGatherer] Push constant size: " << pc.size
                  << " bytes, stage flags: " << pc.stageFlags << "\n";
    }
}

// ============================================================================
// EXECUTE
// ============================================================================

void PushConstantGathererNode::ExecuteImpl(VariadicExecuteContext& ctx) {
    // Pack push constant data from variadic inputs
    PackPushConstantData(ctx);

    // Set outputs
    SetOutputResource(PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA, pushConstantData_);
    SetOutputResource(PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES, pushConstantRanges_);

    // Pass through shader bundle
    auto* bundleRes = GetInputResource(PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE);
    if (bundleRes) {
        SetOutputResource(PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE_OUT,
                         bundleRes->As<ShaderManagement::ShaderDataBundle>());
    }
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
    auto* bundleRes = GetInputResource(PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE);
    auto* shaderBundle = bundleRes->As<ShaderManagement::ShaderDataBundle>();

    if (!shaderBundle->reflectionData->pushConstants.empty()) {
        const auto& pc = shaderBundle->reflectionData->pushConstants[0];

        // Parse struct members from reflection
        for (const auto& member : pc.structDef.members) {
            PushConstantFieldSlotInfo fieldInfo;
            fieldInfo.fieldName = member.name;
            fieldInfo.offset = member.offset;
            fieldInfo.size = member.size;
            fieldInfo.baseType = member.baseType;
            fieldInfo.vecSize = member.vecSize;
            fieldInfo.dynamicInputIndex = pushConstantFields_.size();

            pushConstantFields_.push_back(fieldInfo);

            std::cout << "[PushConstantGatherer] Discovered field: " << member.name
                      << " (offset=" << member.offset << ", size=" << member.size << ")\n";
        }

        // Update variadic constraints
        SetVariadicInputConstraints(pushConstantFields_.size(), pushConstantFields_.size());
    }
}

bool PushConstantGathererNode::ValidateVariadicInputsImpl(VariadicCompileContext& ctx) {
    // Validate each field has correct input type
    for (size_t i = 0; i < pushConstantFields_.size(); ++i) {
        const auto& field = pushConstantFields_[i];

        // Get variadic input for this field
        auto* resource = ctx.GetVariadicInput(0, i);  // Bundle 0
        if (!resource) {
            ctx.ReportError("Missing input for push constant field: " + field.fieldName);
            return false;
        }

        // Validate type compatibility
        if (!ValidateFieldType(resource, field)) {
            ctx.ReportError("Type mismatch for field: " + field.fieldName +
                          " (expected type compatible with " + std::to_string(field.size) + " bytes)");
            return false;
        }
    }

    std::cout << "[PushConstantGatherer] All " << pushConstantFields_.size()
              << " field inputs validated successfully\n";
    return true;
}

void PushConstantGathererNode::PackPushConstantData(VariadicExecuteContext& ctx) {
    // Clear buffer
    std::fill(pushConstantData_.begin(), pushConstantData_.end(), 0);

    // Pack each field
    for (size_t i = 0; i < pushConstantFields_.size(); ++i) {
        const auto& field = pushConstantFields_[i];
        auto* resource = ctx.GetVariadicInput(0, i);

        if (!resource) continue;

        uint8_t* dest = pushConstantData_.data() + field.offset;

        // Pack based on type
        if (field.vecSize == 1) {
            // Scalar
            PackScalar(resource, dest, field.size);
        } else if (field.vecSize > 1) {
            // Vector
            PackVector(resource, dest, field.vecSize);
        }
        // Matrix packing would go here if needed
    }
}

bool PushConstantGathererNode::ValidateFieldType(Resource* res, const PushConstantFieldSlotInfo& field) {
    if (!res) return false;

    // Get expected resource type for field
    ResourceType expectedType = GetResourceTypeForField(field);

    // Check if resource type matches
    return res->GetType() == expectedType;
}

void PushConstantGathererNode::PackScalar(const Resource* res, uint8_t* dest, size_t size) {
    // Extract scalar value based on resource type and copy to destination
    // This would need proper resource casting based on actual resource types
    // For now, just copy raw data
    if (size == 4) {
        float value = 0.0f;  // Get from resource
        std::memcpy(dest, &value, sizeof(float));
    } else if (size == 4) {
        uint32_t value = 0;  // Get from resource
        std::memcpy(dest, &value, sizeof(uint32_t));
    }
}

void PushConstantGathererNode::PackVector(const Resource* res, uint8_t* dest, size_t componentCount) {
    // Extract vector components and copy to destination
    // This would need proper resource casting based on actual resource types
    size_t totalSize = componentCount * sizeof(float);
    std::vector<float> values(componentCount, 0.0f);  // Get from resource
    std::memcpy(dest, values.data(), totalSize);
}

void PushConstantGathererNode::PackMatrix(const Resource* res, uint8_t* dest, size_t rows, size_t cols) {
    // Extract matrix elements and copy to destination
    // This would need proper resource casting based on actual resource types
    size_t totalSize = rows * cols * sizeof(float);
    std::vector<float> values(rows * cols, 0.0f);  // Get from resource
    std::memcpy(dest, values.data(), totalSize);
}

ResourceType PushConstantGathererNode::GetResourceTypeForField(const PushConstantFieldSlotInfo& field) const {
    // Map SPIRV types to resource types
    if (field.baseType == ShaderManagement::SpirvType::Float) {
        if (field.vecSize == 1) return ResourceType::Float;
        else if (field.vecSize == 2) return ResourceType::Vec2;
        else if (field.vecSize == 3) return ResourceType::Vec3;
        else if (field.vecSize == 4) return ResourceType::Vec4;
    } else if (field.baseType == ShaderManagement::SpirvType::Uint32) {
        return ResourceType::Uint;
    } else if (field.baseType == ShaderManagement::SpirvType::Int32) {
        return ResourceType::Int;
    }

    // Default to generic buffer
    return ResourceType::Buffer;
}

} // namespace Vixen::RenderGraph