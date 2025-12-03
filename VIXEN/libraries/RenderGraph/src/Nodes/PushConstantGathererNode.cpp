#include "Nodes/PushConstantGathererNode.h"
#include "Core/NodeContext.h"
#include "Core/NodeLogging.h"
#include "SpirvReflectionData.h"
#include "ResourceExtractor.h"
#include "NodeHelpers/ValidationHelpers.h"
#include "VulkanDevice.h"  // For device limits validation
#include "Data/InputState.h"    // For InputState pointer field extraction
#include <cstring>
#include <iostream>
#include <unordered_map>

using namespace RenderGraph::NodeHelpers;

namespace Vixen::RenderGraph {

// ============================================================================
// HELPER: GENERIC RESOURCE EXTRACTION VIA TEMPLATE DISPATCH
// ============================================================================

/**
 * @brief Template function that extracts any type T from Resource
 */
template<typename T>
static size_t ExtractResourceAs(
    Resource* resource,
    const ShaderManagement::SpirvTypeInfo& typeInfo,
    uint8_t* dest,
    size_t destSize
) {
    T value = resource->GetHandle<T>();
    return ShaderManagement::ResourceExtractor::Extract(typeInfo, &value, dest, destSize);
}

/**
 * @brief Extract typed value from Resource using SPIRV type information
 *
 * Uses hash map with compact 3D key (baseType, dim1, dim2) for O(1) lookup.
 * Handles scalars, vectors, and matrices with arbitrary dimensions.
 */
static size_t ExtractTypedResource(
    Resource* resource,
    const ShaderManagement::SpirvTypeInfo& typeInfo,
    uint8_t* dest,
    size_t destSize
) {
    using BaseType = ShaderManagement::SpirvTypeInfo::BaseType;

    // Compact 4D key: baseType (uint16) + dim1 (uint16) + dim2 (uint16) + dim3 (uint16)
    // Packed into single uint64_t for fast hashing
    struct TypeKey {
        uint64_t packed;

        TypeKey(BaseType baseType, uint16_t dim1, uint16_t dim2, uint16_t dim3)
            : packed((static_cast<uint64_t>(baseType) << 48) |
                     (static_cast<uint64_t>(dim1) << 32) |
                     (static_cast<uint64_t>(dim2) << 16) |
                     static_cast<uint64_t>(dim3)) {}

        bool operator==(const TypeKey& other) const {
            return packed == other.packed;
        }
    };

    struct TypeKeyHash {
        size_t operator()(const TypeKey& key) const {
            return std::hash<uint64_t>{}(key.packed);
        }
    };

    // Hash map: (baseType, dim1, dim2, dim3) -> extraction function
    // For scalars/vectors: dim1=vecSize, dim2=arraySize, dim3=unused
    // For matrices: dim1=columns, dim2=rows, dim3=arraySize
    using ExtractFn = size_t(*)(Resource*, const ShaderManagement::SpirvTypeInfo&, uint8_t*, size_t);
    static const std::unordered_map<TypeKey, ExtractFn, TypeKeyHash> typeDispatch{
        // Float scalars and vectors (arraySize=0 for non-array, dim3=0)
        {TypeKey{BaseType::Float, 1, 0, 0}, &ExtractResourceAs<float>},
        {TypeKey{BaseType::Float, 2, 0, 0}, &ExtractResourceAs<glm::vec2>},
        {TypeKey{BaseType::Float, 3, 0, 0}, &ExtractResourceAs<glm::vec3>},
        {TypeKey{BaseType::Float, 4, 0, 0}, &ExtractResourceAs<glm::vec4>},
        // Int scalars and vectors
        {TypeKey{BaseType::Int, 1, 0, 0}, &ExtractResourceAs<int32_t>},
        {TypeKey{BaseType::Int, 2, 0, 0}, &ExtractResourceAs<glm::ivec2>},
        {TypeKey{BaseType::Int, 3, 0, 0}, &ExtractResourceAs<glm::ivec3>},
        {TypeKey{BaseType::Int, 4, 0, 0}, &ExtractResourceAs<glm::ivec4>},
        // UInt scalars and vectors
        {TypeKey{BaseType::UInt, 1, 0, 0}, &ExtractResourceAs<uint32_t>},
        {TypeKey{BaseType::UInt, 2, 0, 0}, &ExtractResourceAs<glm::uvec2>},
        {TypeKey{BaseType::UInt, 3, 0, 0}, &ExtractResourceAs<glm::uvec3>},
        {TypeKey{BaseType::UInt, 4, 0, 0}, &ExtractResourceAs<glm::uvec4>},
        // Double scalar
        {TypeKey{BaseType::Double, 1, 0, 0}, &ExtractResourceAs<double>},
        // Float matrices (columns x rows, arraySize=0)
        {TypeKey{BaseType::Matrix, 2, 2, 0}, &ExtractResourceAs<glm::mat2>},    // mat2 = 2x2
        {TypeKey{BaseType::Matrix, 3, 3, 0}, &ExtractResourceAs<glm::mat3>},    // mat3 = 3x3
        {TypeKey{BaseType::Matrix, 4, 4, 0}, &ExtractResourceAs<glm::mat4>},    // mat4 = 4x4
        {TypeKey{BaseType::Matrix, 2, 3, 0}, &ExtractResourceAs<glm::mat2x3>},  // mat2x3
        {TypeKey{BaseType::Matrix, 2, 4, 0}, &ExtractResourceAs<glm::mat2x4>},  // mat2x4
        {TypeKey{BaseType::Matrix, 3, 2, 0}, &ExtractResourceAs<glm::mat3x2>},  // mat3x2
        {TypeKey{BaseType::Matrix, 3, 4, 0}, &ExtractResourceAs<glm::mat3x4>},  // mat3x4
        {TypeKey{BaseType::Matrix, 4, 2, 0}, &ExtractResourceAs<glm::mat4x2>},  // mat4x2
        {TypeKey{BaseType::Matrix, 4, 3, 0}, &ExtractResourceAs<glm::mat4x3>},  // mat4x3
    };

    // Build lookup key: (baseType, dim1, dim2, dim3)
    TypeKey key = (typeInfo.baseType == BaseType::Matrix)
        ? TypeKey{typeInfo.baseType, static_cast<uint16_t>(typeInfo.columns), static_cast<uint16_t>(typeInfo.rows), static_cast<uint16_t>(typeInfo.arraySize)}
        : TypeKey{typeInfo.baseType, static_cast<uint16_t>(typeInfo.vecSize), static_cast<uint16_t>(typeInfo.arraySize), 0};

    // O(1) hash lookup
    auto it = typeDispatch.find(key);
    if (it != typeDispatch.end()) {
        return it->second(resource, typeInfo, dest, destSize);
    }

    // Log lookup failure for debugging
    std::cerr << "[ExtractTypedResource] Lookup FAILED: baseType=" << static_cast<int>(typeInfo.baseType)
              << ", vecSize=" << typeInfo.vecSize << ", arraySize=" << typeInfo.arraySize
              << ", key=(baseType=" << static_cast<int>(typeInfo.baseType)
              << ", dim1=" << (typeInfo.baseType == BaseType::Matrix ? typeInfo.columns : typeInfo.vecSize)
              << ", dim2=" << (typeInfo.baseType == BaseType::Matrix ? typeInfo.rows : static_cast<uint32_t>(typeInfo.arraySize))
              << ", dim3=" << (typeInfo.baseType == BaseType::Matrix ? typeInfo.arraySize : 0) << ")\n";

    return 0;  // Unsupported type combination
}

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

    NODE_LOG_INFO("[PushConstantGathererNode::Pack] Starting pack: " + std::to_string(variadicCount) +
                 " variadic inputs, " + std::to_string(pushConstantFields_.size()) + " fields");

    // Pack connected variadic inputs into their corresponding fields
    // Variadic inputs are indexed by BINDING (connection order), not field order
    for (size_t variadicIdx = 0; variadicIdx < variadicCount; ++variadicIdx) {
        auto* resource = ctx.InVariadicResource(variadicIdx);
        const auto* slotInfo = ctx.InVariadicSlot(variadicIdx);

        NODE_LOG_INFO("[PushConstantGathererNode::Pack] Variadic[" + std::to_string(variadicIdx) + "] = " +
                     (resource ? "CONNECTED" : "NULL") +
                     (slotInfo && slotInfo->hasFieldExtraction ? " (field extraction)" : ""));

        if (!resource) continue;

        // Find which field this variadic input corresponds to
        // VariadicIdx matches the BINDING index used in ConnectVariadic
        if (variadicIdx >= pushConstantFields_.size()) {
            NODE_LOG_WARNING("[PushConstantGathererNode::Pack] Variadic input " +
                           std::to_string(variadicIdx) + " exceeds field count");
            continue;
        }

        const auto& field = pushConstantFields_[variadicIdx];
        uint8_t* dest = pushConstantData_.data() + field.offset;


        //Todo: Make the code universal by using the new resource structure 
        // Handle field extraction case
        if (slotInfo && slotInfo->hasFieldExtraction) {
            // Resource points to the struct, field is at resource + fieldOffset
            // Try multiple struct pointer types since Resource doesn't expose raw pointer
            const uint8_t* structPtr = nullptr;

            // Try CameraData* first (most common for push constants)
            if (auto* cameraDataPtr = resource->GetHandle<const CameraData*>()) {
                structPtr = reinterpret_cast<const uint8_t*>(cameraDataPtr);
            }
            // Try InputState* (for debugMode field)
            else if (auto* inputStatePtr = resource->GetHandle<const InputState*>()) {
                structPtr = reinterpret_cast<const uint8_t*>(inputStatePtr);
            }
            // Try non-const versions
            else if (auto* cameraDataPtrMut = resource->GetHandle<CameraData*>()) {
                structPtr = reinterpret_cast<const uint8_t*>(cameraDataPtrMut);
            }
            else if (auto* inputStatePtrMut = resource->GetHandle<InputState*>()) {
                structPtr = reinterpret_cast<const uint8_t*>(inputStatePtrMut);
            }

            if (!structPtr) {
                NODE_LOG_WARNING("[PushConstantGathererNode::Pack] Field extraction failed for field '" +
                                field.fieldName + "': null struct pointer (tried CameraData*, InputState*)");
                continue;
            }

            // Calculate field address
            const uint8_t* fieldPtr = structPtr + slotInfo->fieldOffset;

            // Direct memcpy from field to dest
            std::memcpy(dest, fieldPtr, field.size);

            NODE_LOG_DEBUG("[PushConstantGathererNode::Pack] Field '" + field.fieldName +
                          "' at offset " + std::to_string(field.offset) + " (field extraction, " +
                          std::to_string(field.size) + " bytes copied)");
        } else {
            // Pack connected field value
            ShaderManagement::SpirvTypeInfo typeInfo{};
            typeInfo.baseType = field.baseType;
            typeInfo.vecSize = field.vecSize;
            typeInfo.arraySize = 0;  // Push constants don't use arrays
            typeInfo.sizeInBytes = field.size;

            // Extract value using helper function (handles all SPIRV types)
            size_t bytesWritten = ExtractTypedResource(resource, typeInfo, dest, field.size);

            // If extraction failed, log warning and zero-fill
            if (bytesWritten == 0) {
                NODE_LOG_WARNING("[PushConstantGathererNode::Pack] Failed to extract field '" +
                                field.fieldName + "' (baseType=" + std::to_string(static_cast<int>(typeInfo.baseType)) +
                                ", vecSize=" + std::to_string(typeInfo.vecSize) + "). Zero-filling.");
                std::fill(dest, dest + field.size, 0);
                bytesWritten = field.size;
            }

            NODE_LOG_DEBUG("[PushConstantGathererNode::Pack] Field '" + field.fieldName +
                          "' at offset " + std::to_string(field.offset) + " (connected, " +
                          std::to_string(bytesWritten) + " bytes written)");
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