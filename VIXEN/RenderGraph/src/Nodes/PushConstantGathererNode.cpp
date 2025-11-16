#include "Nodes/PushConstantGathererNode.h"
#include "Core/NodeContext.h"
#include "Core/NodeLogging.h"
#include "ShaderManagement/SpirvReflectionData.h"
#include "ShaderManagement/ResourceExtractor.h"
#include "NodeHelpers/ValidationHelpers.h"
#include "VulkanDevice.h"  // For device limits validation
#include <cstring>
#include <iostream>

using namespace RenderGraph::NodeHelpers;

namespace Vixen::RenderGraph {

// ============================================================================
// VISITOR PATTERN FOR TYPE-SAFE VALUE EXTRACTION
// ============================================================================

/**
 * @brief Visitor that extracts resource variant values and packs into buffer
 *
 * This visitor handles all registered types in ResourceVariant and uses
 * ResourceExtractor for type-safe packing.
 */
class PushConstantPackVisitor {
public:
    PushConstantPackVisitor(
        const ShaderManagement::SpirvTypeInfo& typeInfo,
        uint8_t* dest,
        size_t destSize
    ) : typeInfo(typeInfo), dest(dest), destSize(destSize), bytesWritten(0) {}

    // Generic handler for all types
    template<typename T>
    void operator()(const T& value) {
        ShaderManagement::SpirvTypeInfo info{};
        info.baseType = typeInfo.baseType;
        info.vecSize = typeInfo.vecSize;
        info.sizeInBytes = typeInfo.sizeInBytes;

        // Cast away const to get pointer for extraction
        T* ptr = const_cast<T*>(&value);
        bytesWritten = ShaderManagement::ResourceExtractor::Extract(info, ptr, dest, destSize);
    }

    // Specialization for monostate (empty variant)
    void operator()(const std::monostate&) {
        // Fill with zeros for unset values
        bytesWritten = ShaderManagement::ResourceExtractor::ExtractZero(typeInfo, dest, destSize);
    }

    size_t GetBytesWritten() const { return bytesWritten; }

private:
    const ShaderManagement::SpirvTypeInfo& typeInfo;
    uint8_t* dest;
    size_t destSize;
    size_t bytesWritten;
};

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
    // Initialize with default variadic constraints from type definition
    auto* pcNodeType = static_cast<PushConstantGathererNodeType*>(nodeType);
    SetVariadicInputConstraints(
        pcNodeType->GetDefaultMinVariadicInputs(),
        pcNodeType->GetDefaultMaxVariadicInputs()
    );
}

// ============================================================================
// PRE-REGISTRATION
// ============================================================================

void PushConstantGathererNode::PreRegisterPushConstantFields(const std::shared_ptr<ShaderManagement::ShaderDataBundle>& shaderBundle) {
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

    // Set variadic constraints - fields are OPTIONAL
    // Min = 0 (all fields can use defaults), Max = field count
    if (!pushConstantFields_.empty()) {
        SetVariadicInputConstraints(0, pushConstantFields_.size());
        std::cout << "[PushConstantGatherer] Variadic constraints: min=0, max=" << pushConstantFields_.size()
                  << " (fields are optional, missing fields use zero defaults)\n";
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

        // Validate against device limits
        auto* device = this->GetDevice();
        if (device && device->gpu) {
            uint32_t maxPushConstantsSize = device->gpuProperties.limits.maxPushConstantsSize;

            if (pc.size > maxPushConstantsSize) {
                NODE_LOG_ERROR("[PushConstantGathererNode::Compile] Push constant size " +
                             std::to_string(pc.size) + " bytes exceeds device limit " +
                             std::to_string(maxPushConstantsSize) + " bytes");
                throw std::runtime_error("Push constant size " + std::to_string(pc.size) +
                                       " bytes exceeds device limit " + std::to_string(maxPushConstantsSize) + " bytes");
            }

            // Log usage statistics
            float usagePercent = (static_cast<float>(pc.size) / maxPushConstantsSize) * 100.0f;
            uint32_t remaining = maxPushConstantsSize - pc.size;
            NODE_LOG_INFO("[PushConstantGathererNode::Compile] Push constant usage: " +
                         std::to_string(pc.size) + "/" + std::to_string(maxPushConstantsSize) +
                         " bytes (" + std::to_string(static_cast<int>(usagePercent)) + "%, " +
                         std::to_string(remaining) + " bytes remaining)");
        }

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

    // Update variadic constraints - fields are OPTIONAL
    // Min = 0 (all fields can use defaults), Max = field count
    if (!pushConstantFields_.empty()) {
        SetVariadicInputConstraints(0, pushConstantFields_.size());
    }
}

bool PushConstantGathererNode::ValidateVariadicInputsImpl(VariadicCompileContext& ctx) {
    // Variadic inputs are OPTIONAL - allow partial connections
    // Missing fields will use default values (zero-initialized)
    size_t variadicCount = ctx.InVariadicCount();

    NODE_LOG_INFO("[PushConstantGathererNode::Validate] Connected " + std::to_string(variadicCount) +
                 " of " + std::to_string(pushConstantFields_.size()) + " push constant fields");

    // Validate each connected input
    for (size_t i = 0; i < variadicCount; ++i) {
        auto* resource = ctx.InVariadicResource(i);

        // Check if corresponding field exists
        if (i < pushConstantFields_.size()) {
            if (!ValidateFieldType(resource, pushConstantFields_[i])) {
                NODE_LOG_ERROR("[PushConstantGathererNode::Validate] Type mismatch for field " +
                             pushConstantFields_[i].fieldName);
            }
        } else {
            NODE_LOG_ERROR("[PushConstantGathererNode::Validate] Variadic input " + std::to_string(i) +
                         " has no corresponding field definition");
        }
    }

    return true;
}

void PushConstantGathererNode::PackPushConstantData(VariadicExecuteContext& ctx) {
    // Initialize entire buffer with zeros (default values for all fields)
    std::fill(pushConstantData_.begin(), pushConstantData_.end(), 0);

    size_t variadicCount = ctx.InVariadicCount();

    // Pack ALL fields from shader reflection, using connected inputs or defaults
    for (size_t fieldIdx = 0; fieldIdx < pushConstantFields_.size(); ++fieldIdx) {
        const auto& field = pushConstantFields_[fieldIdx];
        uint8_t* dest = pushConstantData_.data() + field.offset;

        // Check if this field has a connected input
        auto* resource = (fieldIdx < variadicCount) ? ctx.InVariadicResource(fieldIdx) : nullptr;

        if (resource) {
            // Pack connected field value
            ShaderManagement::SpirvTypeInfo typeInfo{};
            typeInfo.baseType = field.baseType;
            typeInfo.vecSize = field.vecSize;
            typeInfo.sizeInBytes = field.size;

            // Extract value from resource based on SPIRV type
            // ResourceV3 stores typed values - extract based on base type
            size_t bytesWritten = 0;

            // Determine which type to extract based on SPIRV type info
            switch (typeInfo.baseType) {
                case ShaderManagement::SpirvTypeInfo::BaseType::Float: {
                    if (typeInfo.vecSize == 1) {
                        float value = resource->GetHandle<float>();
                        std::memcpy(dest, &value, sizeof(float));
                        bytesWritten = sizeof(float);
                    }
                    // TODO: Add vec2, vec3, vec4 support
                    break;
                }
                case ShaderManagement::SpirvTypeInfo::BaseType::Int: {
                    if (typeInfo.vecSize == 1) {
                        int32_t value = resource->GetHandle<int32_t>();
                        std::memcpy(dest, &value, sizeof(int32_t));
                        bytesWritten = sizeof(int32_t);
                    }
                    break;
                }
                case ShaderManagement::SpirvTypeInfo::BaseType::UInt: {
                    if (typeInfo.vecSize == 1) {
                        uint32_t value = resource->GetHandle<uint32_t>();
                        std::memcpy(dest, &value, sizeof(uint32_t));
                        bytesWritten = sizeof(uint32_t);
                    }
                    break;
                }
                default:
                    // Unsupported type - zero fill
                    std::fill(dest, dest + field.size, 0);
                    bytesWritten = field.size;
                    break;
            }

            NODE_LOG_DEBUG("[PushConstantGathererNode::Pack] Field '" + field.fieldName +
                          "' at offset " + std::to_string(field.offset) + " (connected, " +
                          std::to_string(bytesWritten) + " bytes written)");
        } else {
            // Leave as zero-initialized (already done by std::fill)
            NODE_LOG_DEBUG("[PushConstantGathererNode::Pack] Field '" + field.fieldName +
                          "' at offset " + std::to_string(field.offset) + " (default: zero)");
        }
    }

    NODE_LOG_INFO("[PushConstantGathererNode::Pack] Packed " + std::to_string(pushConstantData_.size()) +
                 " bytes with " + std::to_string(variadicCount) + "/" + std::to_string(pushConstantFields_.size()) +
                 " fields connected");
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