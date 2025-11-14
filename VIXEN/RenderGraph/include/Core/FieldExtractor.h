#pragma once

#include "Data/Core/ResourceTypeTraits.h"
#include "Data/Core/ResourceV3.h"  // Needed for ResourceTypeTraits implementation
#include <type_traits>

namespace Vixen::RenderGraph {

// ============================================================================
// FIELD EXTRACTION - Type-safe struct member access for slot connections
// ============================================================================

/**
 * @brief Extracts a specific field from a struct type for slot connections
 *
 * Enables ergonomic connections from struct outputs to scalar/array inputs:
 *
 * Example:
 * ```cpp
 * struct SwapChainPublicVariables {
 *     std::vector<VkImageView> images;
 *     VkSwapchainKHR swapchain;
 * };
 *
 * // Connect struct field to slot (type-safe, compile-time validated)
 * auto extractor = FieldExtractor(&SwapChainPublicVariables::images);
 * connect(swapchainOutput, extractor, imageViewInput);
 * ```
 *
 * Benefits:
 * - Type-safe: Compiler validates member exists and type matches
 * - Zero runtime overhead: All resolved at compile time
 * - No string lookups: Uses pointer-to-member (compile-time constant)
 * - Ergonomic: Clear, readable syntax
 *
 * @tparam StructType The struct containing the field
 * @tparam FieldType The type of the field being extracted
 */
template<typename StructType, typename FieldType>
class FieldExtractor {
public:
    using Struct = StructType;
    using Field = FieldType;
    using MemberPointer = FieldType StructType::*;

    /**
     * @brief Construct field extractor from pointer-to-member
     * @param member Pointer to struct member (e.g., &MyStruct::myField)
     */
    constexpr explicit FieldExtractor(MemberPointer member)
        : member_(member) {}

    /**
     * @brief Extract field from struct instance
     * @param structInstance The struct instance to extract from
     * @return Reference to the field
     */
    constexpr const FieldType& extract(const StructType& structInstance) const {
        return structInstance.*member_;
    }

    /**
     * @brief Extract field from struct instance (non-const)
     * @param structInstance The struct instance to extract from
     * @return Reference to the field
     */
    constexpr FieldType& extract(StructType& structInstance) const {
        return structInstance.*member_;
    }

    /**
     * @brief Get the pointer-to-member
     */
    constexpr MemberPointer getMember() const {
        return member_;
    }

private:
    MemberPointer member_;
};

// ============================================================================
// DEDUCTION GUIDE - Automatic template parameter deduction
// ============================================================================

/**
 * @brief Deduction guide for automatic type deduction
 *
 * Enables simplified syntax:
 * ```cpp
 * auto extractor = FieldExtractor(&MyStruct::myField);
 * // Instead of:
 * // FieldExtractor<MyStruct, FieldType> extractor(&MyStruct::myField);
 * ```
 */
template<typename StructType, typename FieldType>
FieldExtractor(FieldType StructType::*) -> FieldExtractor<StructType, FieldType>;

// ============================================================================
// HELPER FUNCTION - Create field extractor with minimal syntax
// ============================================================================

/**
 * @brief Create field extractor with minimal syntax
 *
 * Usage:
 * ```cpp
 * auto extractor = Field(&SwapChainPublicVariables::images);
 * ```
 *
 * @tparam StructType Automatically deduced from pointer-to-member
 * @tparam FieldType Automatically deduced from pointer-to-member
 * @param member Pointer to struct member
 * @return FieldExtractor for the specified member
 */
template<typename StructType, typename FieldType>
constexpr auto Field(FieldType StructType::* member) {
    return FieldExtractor<StructType, FieldType>(member);
}

// ============================================================================
// TYPE TRAITS - Compile-time introspection
// ============================================================================

/**
 * @brief Check if type is a FieldExtractor
 */
template<typename T>
struct IsFieldExtractor : std::false_type {};

template<typename StructType, typename FieldType>
struct IsFieldExtractor<FieldExtractor<StructType, FieldType>> : std::true_type {};

// Handle const-qualified FieldExtractor
template<typename StructType, typename FieldType>
struct IsFieldExtractor<const FieldExtractor<StructType, FieldType>> : std::true_type {};

template<typename T>
inline constexpr bool IsFieldExtractor_v = IsFieldExtractor<std::remove_cv_t<T>>::value;

/**
 * @brief Extract struct type from FieldExtractor
 */
template<typename T>
struct ExtractorStructType;

template<typename StructType, typename FieldType>
struct ExtractorStructType<FieldExtractor<StructType, FieldType>> {
    using Type = StructType;
};

// Handle const-qualified FieldExtractor
template<typename StructType, typename FieldType>
struct ExtractorStructType<const FieldExtractor<StructType, FieldType>> {
    using Type = StructType;
};

template<typename T>
using ExtractorStructType_t = typename ExtractorStructType<std::remove_cv_t<T>>::Type;

/**
 * @brief Extract field type from FieldExtractor
 */
template<typename T>
struct ExtractorFieldType;

template<typename StructType, typename FieldType>
struct ExtractorFieldType<FieldExtractor<StructType, FieldType>> {
    using Type = FieldType;
};

// Handle const-qualified FieldExtractor
template<typename StructType, typename FieldType>
struct ExtractorFieldType<const FieldExtractor<StructType, FieldType>> {
    using Type = FieldType;
};

template<typename T>
using ExtractorFieldType_t = typename ExtractorFieldType<std::remove_cv_t<T>>::Type;

// ============================================================================
// VALIDATION - Compile-time checks for field extraction
// ============================================================================

/**
 * @brief Validate that field extraction is compatible with slot connection
 *
 * Checks:
 * 1. Field type is registered (valid resource type)
 * 2. Field type matches target slot type (or is compatible container)
 *
 * Note: Struct type does NOT need to be registered - any struct can serve as
 * a container for registered field types.
 *
 * @tparam Extractor FieldExtractor type
 * @tparam TargetSlotType Type of the target slot
 */
template<typename Extractor, typename TargetSlotType>
struct ValidateFieldExtraction {
    using StructType = ExtractorStructType_t<Extractor>;
    using FieldType = ExtractorFieldType_t<Extractor>;

    // Validate field type is registered (struct type can be any user-defined type)
    static_assert(ResourceTypeTraits<FieldType>::isValid,
        "Field type must be registered in RESOURCE_TYPE_REGISTRY");

    // Check if field type matches target slot type (exact or compatible)
    static constexpr bool isCompatible =
        std::is_same_v<FieldType, TargetSlotType> ||
        std::is_convertible_v<FieldType, TargetSlotType>;

    static_assert(isCompatible,
        "Field type must match or be convertible to target slot type");

    static constexpr bool value = true;
};

// ============================================================================
// USAGE EXAMPLE
// ============================================================================

/*
Example usage in render graph:

struct SwapChainPublicVariables {
    std::vector<VkImageView> images;
    VkSwapchainKHR swapchain;
    VkFormat format;
};

// Option 1: Explicit field extractor
auto imageExtractor = FieldExtractor(&SwapChainPublicVariables::images);
connect(swapchainNode.output<SwapChainPublicVariables>("swapchain"),
        imageExtractor,
        targetNode.input<std::vector<VkImageView>>("images"));

// Option 2: Helper function
connect(swapchainNode.output<SwapChainPublicVariables>("swapchain"),
        Field(&SwapChainPublicVariables::images),
        targetNode.input<std::vector<VkImageView>>("images"));

// Option 3: Inline (most concise)
connect(swapchainNode["swapchain"],
        &SwapChainPublicVariables::images,  // Direct pointer-to-member
        targetNode["images"]);

Benefits:
- Compile-time type checking
- No runtime string lookups
- No extra memory overhead
- Refactoring-safe (renaming fields updates automatically)
- IDE autocomplete support
*/

} // namespace Vixen::RenderGraph
