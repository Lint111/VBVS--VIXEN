#pragma once

#include "SpirvReflectionData.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <glm/glm.hpp>

namespace ShaderManagement {

/**
 * @brief Type-safe resource value extractor using visitor pattern with macro-based registry
 *
 * Single source of truth for supported SPIRV types and their extractors.
 * Uses macro registry pattern similar to ResourceVariant in RenderGraph.
 *
 * Usage:
 *   auto extractor = ResourceExtractor::GetExtractor(fieldType);
 *   if (extractor) {
 *       extractor(resourceHandle, destBuffer, fieldSize);
 *   }
 */

// ============================================================================
// SINGLE SOURCE OF TRUTH: SPIRV EXTRACTOR TYPE REGISTRY
// ============================================================================

/**
 * @brief Master list of supported SPIRV types for push constants
 *
 * Format: SPIRV_EXTRACTOR_TYPE(BaseType, VecSize, CppType, ExtractorFn)
 * - BaseType: SpirvTypeInfo::BaseType enum value
 * - VecSize: Vector component count (1 for scalar, 2-4 for vectors)
 * - CppType: C++ type that represents this (e.g., float, glm::vec3)
 * - ExtractorFn: Implementation name (will be generated from CppType)
 *
 * To add a new type, add ONE line here.
 * Auto-generates extractor implementations.
 */
#define SPIRV_EXTRACTOR_REGISTRY \
    SPIRV_EXTRACTOR_TYPE(Float, 1, float, ExtractFloat) \
    SPIRV_EXTRACTOR_TYPE(Float, 2, glm::vec2, ExtractVec2F) \
    SPIRV_EXTRACTOR_TYPE(Float, 3, glm::vec3, ExtractVec3F) \
    SPIRV_EXTRACTOR_TYPE(Float, 4, glm::vec4, ExtractVec4F) \
    SPIRV_EXTRACTOR_TYPE(Int, 1, int32_t, ExtractInt) \
    SPIRV_EXTRACTOR_TYPE(Int, 2, glm::ivec2, ExtractVec2I) \
    SPIRV_EXTRACTOR_TYPE(Int, 3, glm::ivec3, ExtractVec3I) \
    SPIRV_EXTRACTOR_TYPE(Int, 4, glm::ivec4, ExtractVec4I) \
    SPIRV_EXTRACTOR_TYPE(UInt, 1, uint32_t, ExtractUInt) \
    SPIRV_EXTRACTOR_TYPE(UInt, 2, glm::uvec2, ExtractVec2U) \
    SPIRV_EXTRACTOR_TYPE(UInt, 3, glm::uvec3, ExtractVec3U) \
    SPIRV_EXTRACTOR_TYPE(UInt, 4, glm::uvec4, ExtractVec4U) \
    SPIRV_EXTRACTOR_TYPE(Double, 1, double, ExtractDouble) \
    SPIRV_EXTRACTOR_TYPE(Matrix, 2, glm::mat2, ExtractMat2F) \
    SPIRV_EXTRACTOR_TYPE(Matrix, 3, glm::mat3, ExtractMat3F) \
    SPIRV_EXTRACTOR_TYPE(Matrix, 4, glm::mat4, ExtractMat4F)

class ResourceExtractor {
public:
    /**
     * @brief Extract function signature
     *
     * @param resourceHandle - The resource handle (void pointer to typed value)
     * @param dest - Destination buffer to write extracted value
     * @param destSize - Size of destination buffer in bytes
     * @return Number of bytes written, 0 on error
     */
    using ExtractFn = std::function<size_t(void* resourceHandle, uint8_t* dest, size_t destSize)>;

    /**
     * @brief Get extractor for a SPIRV type
     *
     * Returns a type-safe extraction function for the given type.
     * Returns nullptr if type is not registered.
     *
     * @param typeInfo - SPIRV type information (baseType, vecSize, width, etc.)
     * @return Extraction function, or nullptr if unsupported
     */
    static ExtractFn GetExtractor(const SpirvTypeInfo& typeInfo);

    /**
     * @brief Get extractor by base type and vector size
     *
     * Convenience overload for common cases.
     *
     * @param baseType - SPIRV base type (Float, Int, UInt, etc.)
     * @param vecSize - Vector component count (1 for scalar, 2-4 for vectors)
     * @return Extraction function, or nullptr if unsupported
     */
    static ExtractFn GetExtractor(SpirvTypeInfo::BaseType baseType, uint32_t vecSize = 1);

    /**
     * @brief Extract typed value from resource and write to buffer
     *
     * Convenience function combining GetExtractor() and invocation.
     *
     * @param typeInfo - SPIRV type information
     * @param resourceHandle - Resource handle to extract from
     * @param dest - Destination buffer
     * @param destSize - Destination buffer size in bytes
     * @return Number of bytes written, 0 on error
     */
    static size_t Extract(
        const SpirvTypeInfo& typeInfo,
        void* resourceHandle,
        uint8_t* dest,
        size_t destSize
    );

    /**
     * @brief Simplified extract - just zero-fills destination
     *
     * For now, while we refine the resource system integration,
     * we use a simpler approach that fills with zeros.
     * Future: integrate with actual resource type extraction.
     *
     * @param typeInfo - SPIRV type information (used to determine size)
     * @param dest - Destination buffer
     * @param destSize - Destination buffer size in bytes
     * @return Number of bytes written
     */
    static size_t ExtractZero(
        const SpirvTypeInfo& typeInfo,
        uint8_t* dest,
        size_t destSize
    );

private:
    ResourceExtractor() = delete;

    // Extractor function declarations auto-generated from macro registry
    // Pattern: static size_t ExtractorName(void* handle, uint8_t* dest, size_t size);
    #define SPIRV_EXTRACTOR_TYPE(BaseType, VecSize, CppType, FnName) \
        friend size_t FnName(void* handle, uint8_t* dest, size_t size);
    SPIRV_EXTRACTOR_REGISTRY
    #undef SPIRV_EXTRACTOR_TYPE

    // Helper: get size in bytes for type
    static size_t GetTypeSize(const SpirvTypeInfo& typeInfo);
};

} // namespace ShaderManagement
