#include "ShaderManagement/ResourceExtractor.h"
#include <glm/glm.hpp>
#include <array>
#include <utility>

namespace ShaderManagement {

// ============================================================================
// MACRO-BASED TYPE REGISTRY
// ============================================================================
// Maps SPIRV type combinations to their extractor functions via macro expansion.
// Pattern follows ResourceVariant: single source of truth for types.

// Helper macro to avoid specifying BaseType prefix twice
#define SPIRV_TYPE_REF(T) SpirvTypeInfo::BaseType::T

// Lookup table: (baseType enum value, vecSize) -> ExtractFn
// Built from macro registry to maintain single source of truth

struct TypeKey {
    uint32_t baseType;
    uint32_t vecSize;
};

// Forward declarations for all extractor functions
#define SPIRV_EXTRACTOR_TYPE(BaseType, VecSize, CppType, FnName) \
    static size_t FnName(void* handle, uint8_t* dest, size_t size);
SPIRV_EXTRACTOR_REGISTRY
#undef SPIRV_EXTRACTOR_TYPE

// Static lookup table initialized from macro registry
static const std::array<std::pair<TypeKey, ResourceExtractor::ExtractFn>, 16> EXTRACTOR_MAP{{
    #define SPIRV_EXTRACTOR_TYPE(BaseType, VecSize, CppType, FnName) \
        std::make_pair(TypeKey{static_cast<uint32_t>(SPIRV_TYPE_REF(BaseType)), VecSize}, &FnName),
    SPIRV_EXTRACTOR_REGISTRY
    #undef SPIRV_EXTRACTOR_TYPE
}};

// ============================================================================
// PUBLIC API
// ============================================================================

ResourceExtractor::ExtractFn ResourceExtractor::GetExtractor(const SpirvTypeInfo& typeInfo) {
    return GetExtractor(typeInfo.baseType, typeInfo.vecSize);
}

ResourceExtractor::ExtractFn ResourceExtractor::GetExtractor(
    SpirvTypeInfo::BaseType baseType,
    uint32_t vecSize
) {
    // Search registry for matching (baseType, vecSize) pair
    auto baseTypeVal = static_cast<uint32_t>(baseType);
    
    for (const auto& entry : EXTRACTOR_MAP) {
        if (entry.first.baseType == baseTypeVal &&
            entry.first.vecSize == vecSize) {
            return entry.second;
        }
    }
    
    return nullptr;
}

size_t ResourceExtractor::Extract(
    const SpirvTypeInfo& typeInfo,
    void* resourceHandle,
    uint8_t* dest,
    size_t destSize
) {
    auto extractor = GetExtractor(typeInfo);
    if (!extractor) {
        return 0;  // Unsupported type
    }

    return extractor(resourceHandle, dest, destSize);
}

size_t ResourceExtractor::ExtractZero(
    const SpirvTypeInfo& typeInfo,
    uint8_t* dest,
    size_t destSize
) {
    size_t writeSize = GetTypeSize(typeInfo);
    if (writeSize == 0 || writeSize > destSize) {
        return 0;
    }
    std::memset(dest, 0, writeSize);
    return writeSize;
}

// ============================================================================
// AUTO-GENERATED EXTRACTOR IMPLEMENTATIONS
// ============================================================================
// All extractor functions generated from SPIRV_EXTRACTOR_REGISTRY macro.
// Each follows pattern: cast handle to typed pointer, memcpy to dest, return size.

#define SPIRV_EXTRACTOR_TYPE(BaseType, VecSize, CppType, FnName) \
    size_t FnName(void* handle, uint8_t* dest, size_t size) { \
        if (!handle || !dest) return 0; \
        if (size < sizeof(CppType)) return 0; \
        auto* typedValue = static_cast<CppType*>(handle); \
        std::memcpy(dest, typedValue, sizeof(CppType)); \
        return sizeof(CppType); \
    }

SPIRV_EXTRACTOR_REGISTRY

#undef SPIRV_EXTRACTOR_TYPE

// ============================================================================
// HELPER: Type size calculation
// ============================================================================

size_t ResourceExtractor::GetTypeSize(const SpirvTypeInfo& typeInfo) {
    switch (typeInfo.baseType) {
        case SpirvTypeInfo::BaseType::Float:
            switch (typeInfo.vecSize) {
                case 1: return sizeof(float);
                case 2: return sizeof(glm::vec2);
                case 3: return sizeof(glm::vec3);
                case 4: return sizeof(glm::vec4);
            }
            break;

        case SpirvTypeInfo::BaseType::Double:
            switch (typeInfo.vecSize) {
                case 1: return sizeof(double);
                case 2: return sizeof(glm::dvec2);
                case 3: return sizeof(glm::dvec3);
                case 4: return sizeof(glm::dvec4);
            }
            break;

        case SpirvTypeInfo::BaseType::Int:
            switch (typeInfo.vecSize) {
                case 1: return sizeof(int32_t);
                case 2: return sizeof(glm::ivec2);
                case 3: return sizeof(glm::ivec3);
                case 4: return sizeof(glm::ivec4);
            }
            break;

        case SpirvTypeInfo::BaseType::UInt:
            switch (typeInfo.vecSize) {
                case 1: return sizeof(uint32_t);
                case 2: return sizeof(glm::uvec2);
                case 3: return sizeof(glm::uvec3);
                case 4: return sizeof(glm::uvec4);
            }
            break;

        case SpirvTypeInfo::BaseType::Matrix:
            switch (typeInfo.vecSize) {
                case 2: return sizeof(glm::mat2);
                case 3: return sizeof(glm::mat3);
                case 4: return sizeof(glm::mat4);
            }
            break;

        default:
            break;
    }

    return 0; // Unsupported type
}

} // namespace ShaderManagement
