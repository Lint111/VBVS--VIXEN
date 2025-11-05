#pragma once

#include "../Core/ResourceVariant.h"
#include "../Core/ResourceTypeTraits.h"
#include <tuple>
#include <type_traits>

namespace Vixen::RenderGraph {

// Forward declaration
template<typename T>
class Slot;

// ============================================================================
// SHADER BUNDLE REFLECTION SYSTEM
// ============================================================================

/**
 * @brief Macro-based reflection for shader bundle structs
 *
 * This system allows automatic extraction of field types from shader bundle
 * headers, enabling type-safe gatherer nodes with minimal boilerplate.
 *
 * Usage in shader bundle header:
 * ```cpp
 * REFLECTABLE_STRUCT_BEGIN(ComputeShaderResources)
 *     FIELD(std::vector<VkImageView>, inputImages)
 *     FIELD(VkBuffer, uniformBuffer)
 *     FIELD(VkPipeline, pipeline)
 * REFLECTABLE_STRUCT_END(ComputeShaderResources)
 * ```
 *
 * This generates:
 * - The struct definition with named fields
 * - ShaderBundleTraits specialization with field types
 * - Compile-time field count and type accessors
 */

// Declare the shader bundle struct and begin trait specialization
#define REFLECTABLE_STRUCT_BEGIN(StructName) \
    struct StructName { \

// Define a field in the shader bundle
#define FIELD(Type, Name) \
        Type Name;

// End struct definition and define traits
#define REFLECTABLE_STRUCT_END(StructName) \
    }; \
    \
    template<> \
    struct ShaderBundleTraits<StructName> { \
        using FieldTypes = std::tuple<

// Field type list separator
#define FIELD_TYPE(Type) Type,

// End the traits specialization
#define TRAITS_END() \
        >; \
        static constexpr size_t FieldCount = std::tuple_size_v<FieldTypes>; \
    };

// ============================================================================
// SHADER BUNDLE TRAITS - Type information for bundle structs
// ============================================================================

/**
 * @brief Traits for shader bundle structs
 *
 * Provides compile-time access to:
 * - Field types as a tuple
 * - Field count
 * - Individual field type access
 *
 * Specialized via REFLECTABLE_STRUCT_* macros or manually:
 * ```cpp
 * template<>
 * struct ShaderBundleTraits<MyBundle> {
 *     using FieldTypes = std::tuple<VkImage, VkBuffer, VkSampler>;
 *     static constexpr size_t FieldCount = 3;
 * };
 * ```
 */
template<typename T>
struct ShaderBundleTraits {
    // Default: no reflection info
    using FieldTypes = std::tuple<>;
    static constexpr size_t FieldCount = 0;
    static constexpr bool IsReflectable = false;
};

// Helper to check if a type has reflection info
template<typename T>
constexpr bool IsReflectableBundle =
    ShaderBundleTraits<T>::FieldCount > 0;

// Helper to get field type by index
template<typename BundleType, size_t Index>
using BundleFieldType = std::tuple_element_t<
    Index,
    typename ShaderBundleTraits<BundleType>::FieldTypes
>;

// ============================================================================
// SHADER BUNDLE GATHERER NODE
// ============================================================================

/**
 * @brief Resource gatherer configured by shader bundle header type
 *
 * This is the primary pattern for using shader bundle headers as "config files".
 * The bundle type defines what resources are needed, and the gatherer automatically
 * generates typed input slots matching the bundle's fields.
 *
 * WORKFLOW:
 * 1. Define shader bundle in header with REFLECTABLE_STRUCT_* macros
 * 2. Include the header
 * 3. Create ShaderBundleGatherer<BundleType>
 * 4. Connect inputs (order matches struct field order)
 * 5. Execute() assembles the bundle from inputs
 *
 * EXAMPLE:
 * ```cpp
 * // In ShaderBundles/compute.h:
 * REFLECTABLE_STRUCT_BEGIN(ComputeShaderResources)
 *     FIELD(std::vector<VkImageView>, inputImages)
 *     FIELD(VkBuffer, uniformBuffer)
 *     FIELD(VkPipeline, pipeline)
 * REFLECTABLE_STRUCT_END(ComputeShaderResources)
 * // Then define traits:
 * template<> struct ShaderBundleTraits<ComputeShaderResources> {
 *     using FieldTypes = std::tuple<
 *         std::vector<VkImageView>, VkBuffer, VkPipeline
 *     >;
 *     static constexpr size_t FieldCount = 3;
 * };
 *
 * // In render graph code:
 * #include "ShaderBundles/compute.h"
 *
 * ShaderBundleGatherer<ComputeShaderResources> gatherer;
 *
 * // Connect inputs (validated against bundle field types)
 * gatherer.input<0>().connectFrom(imagesSlot);     // Must be vector<VkImageView>
 * gatherer.input<1>().connectFrom(bufferSlot);     // Must be VkBuffer
 * gatherer.input<2>().connectFrom(pipelineSlot);   // Must be VkPipeline
 *
 * gatherer.execute();
 * auto& resources = gatherer.output.get(); // ComputeShaderResources
 * ```
 *
 * BENEFITS:
 * - Shader requirements in ONE header file
 * - Automatic input slot generation
 * - Compile-time type validation
 * - Minimal graph setup
 * - Type-safe output
 *
 * @tparam BundleType Shader bundle struct type (must have ShaderBundleTraits)
 */
template<typename BundleType>
class ShaderBundleGatherer {
private:
    // Extract field types from bundle
    using Traits = ShaderBundleTraits<BundleType>;
    using FieldTypes = typename Traits::FieldTypes;
    static constexpr size_t FieldCount = Traits::FieldCount;

    // Compile-time validation
    static_assert(FieldCount > 0,
        "BundleType must have ShaderBundleTraits specialization. "
        "Use REFLECTABLE_STRUCT_* macros or manually specialize ShaderBundleTraits.");

    // Validate all field types are registered
    template<typename... Types>
    static constexpr bool AllTypesValid(std::tuple<Types...>) {
        return (ResourceTypeTraits<Types>::isValid && ...);
    }

    static_assert(AllTypesValid(FieldTypes{}),
        "All bundle field types must be registered in RESOURCE_TYPE_REGISTRY");

    // ========================================================================
    // HELPER - Convert tuple of types to tuple of Slot<Types>
    // ========================================================================

    template<typename Tuple>
    struct InputSlotsFromTuple;

    template<typename... Types>
    struct InputSlotsFromTuple<std::tuple<Types...>> {
        using type = std::tuple<Slot<Types>...>;
    };

public:
    // ========================================================================
    // INPUT SLOTS - Generated from bundle field types
    // ========================================================================

    /**
     * @brief Tuple of typed input slots matching bundle fields
     *
     * Each slot corresponds to a field in the bundle struct, in order.
     * Type safety is enforced at compile time.
     */
    using InputSlots = typename InputSlotsFromTuple<FieldTypes>::type;
    InputSlots inputs;

    /**
     * @brief Access input slot by index
     * @tparam Index Field index in bundle (0-based)
     * @return Typed slot reference
     */
    template<size_t Index>
    auto& input() {
        static_assert(Index < FieldCount, "Input index out of range");
        return std::get<Index>(inputs);
    }

    // ========================================================================
    // OUTPUT SLOT - Assembled bundle
    // ========================================================================

    /**
     * @brief Output slot containing assembled bundle struct
     *
     * After execute(), this contains the BundleType with all fields
     * populated from the input slots.
     */
    Slot<BundleType> output;

    // ========================================================================
    // EXECUTION - Assemble bundle from inputs
    // ========================================================================

    /**
     * @brief Gather all inputs and assemble into bundle struct
     *
     * Reads each input slot and assigns to corresponding bundle field.
     */
    void execute() {
        BundleType bundle;
        assembleBundleFields(bundle, std::make_index_sequence<FieldCount>{});
        output.set(std::move(bundle));
    }

private:
    // ========================================================================
    // HELPERS - Field assembly
    // ========================================================================

    /**
     * @brief Assign input slot values to bundle fields
     *
     * Uses pointer-to-member to access bundle fields and assign from slots.
     */
    template<size_t... Indices>
    void assembleBundleFields(BundleType& bundle, std::index_sequence<Indices...>) {
        // For each field index, get input and assign to bundle field
        (assignField<Indices>(bundle), ...);
    }

    /**
     * @brief Assign a single field from input slot
     *
     * This uses the field assignment helper to set bundle field values.
     * The actual field access requires knowing field order, which matches
     * the order in ShaderBundleTraits::FieldTypes.
     *
     * Note: Without full reflection, we can't get field pointers automatically.
     * Users must ensure field order matches between struct definition and traits.
     */
    template<size_t Index>
    void assignField(BundleType& bundle) {
        // Get the input value
        using FieldType = BundleFieldType<BundleType, Index>;
        const FieldType& value = std::get<Index>(inputs).get();

        // Assign to bundle field
        // This requires the bundle to be aggregate-initializable or have
        // field accessors. For now, we use structured binding approach.
        assignFieldAtIndex<Index>(bundle, value);
    }

    /**
     * @brief Assign field value at specific index
     *
     * This is a helper that handles field assignment. It relies on the
     * bundle struct being aggregate-type or having proper field ordering.
     *
     * The implementation uses a tuple-like access pattern.
     */
    template<size_t Index, typename FieldType>
    void assignFieldAtIndex(BundleType& bundle, const FieldType& value) {
        // Convert bundle to tuple of references for indexed access
        auto refs = bundleToTuple(bundle);
        std::get<Index>(refs) = value;
    }

    /**
     * @brief Convert bundle struct to tuple of field references
     *
     * This uses structured bindings (C++17) to decompose the struct.
     * Requires the bundle to be an aggregate type with public fields.
     *
     * NOTE: This is specialized per bundle field count via template magic.
     * The actual implementation uses compile-time dispatch based on field count.
     */
    auto bundleToTuple(BundleType& bundle) {
        return bundleToTupleHelper(bundle, std::make_index_sequence<FieldCount>{});
    }

    /**
     * @brief Helper for bundle-to-tuple conversion
     *
     * Uses structured bindings to decompose bundle into tuple of references.
     * This requires C++17 and the bundle to be an aggregate.
     */
    template<size_t... Indices>
    auto bundleToTupleHelper(BundleType& bundle, std::index_sequence<Indices...>) {
        // Use structured binding specialization based on field count
        if constexpr (FieldCount == 1) {
            auto& [f0] = bundle;
            return std::tie(f0);
        }
        else if constexpr (FieldCount == 2) {
            auto& [f0, f1] = bundle;
            return std::tie(f0, f1);
        }
        else if constexpr (FieldCount == 3) {
            auto& [f0, f1, f2] = bundle;
            return std::tie(f0, f1, f2);
        }
        else if constexpr (FieldCount == 4) {
            auto& [f0, f1, f2, f3] = bundle;
            return std::tie(f0, f1, f2, f3);
        }
        else if constexpr (FieldCount == 5) {
            auto& [f0, f1, f2, f3, f4] = bundle;
            return std::tie(f0, f1, f2, f3, f4);
        }
        else if constexpr (FieldCount == 6) {
            auto& [f0, f1, f2, f3, f4, f5] = bundle;
            return std::tie(f0, f1, f2, f3, f4, f5);
        }
        else if constexpr (FieldCount == 7) {
            auto& [f0, f1, f2, f3, f4, f5, f6] = bundle;
            return std::tie(f0, f1, f2, f3, f4, f5, f6);
        }
        else if constexpr (FieldCount == 8) {
            auto& [f0, f1, f2, f3, f4, f5, f6, f7] = bundle;
            return std::tie(f0, f1, f2, f3, f4, f5, f6, f7);
        }
        else {
            static_assert(FieldCount <= 8,
                "ShaderBundleGatherer currently supports up to 8 fields. "
                "Extend bundleToTupleHelper for more fields.");
        }
    }
};

// ============================================================================
// CONVENIENCE HELPERS
// ============================================================================

/**
 * @brief Type alias for cleaner usage
 */
template<typename BundleType>
using Gatherer = ShaderBundleGatherer<BundleType>;

} // namespace Vixen::RenderGraph
