#pragma once

#include "Data/Core/CompileTimeResourceSystem.h"
#include "Data/Core/ResourceTypeTraits.h"
#include "Core/FieldExtractor.h"
#include <tuple>
#include <vector>
#include <array>

namespace Vixen::RenderGraph {

// Forward declaration of Slot template
// The actual implementation should be provided by the render graph framework
template<typename T>
class Slot;

// ============================================================================
// RESOURCE GATHERER NODE - Variadic template for flexible resource collection
// ============================================================================

/**
 * @brief Generic resource gatherer accepting arbitrary typed inputs
 *
 * This node solves the "resource gatherer" problem using variadic templates:
 * - Compile-time variable input count
 * - Each input can be a different type
 * - Full type safety with compile-time validation
 * - Supports scalars, vectors, arrays, and variants
 * - Works with field extraction from struct outputs
 *
 * Example usage:
 * ```cpp
 * // Gather 3 different resource types
 * auto gatherer = graph.addNode<ResourceGathererNode<
 *     VkImage,                    // Input 0: Single image
 *     std::vector<VkBuffer>,      // Input 1: Multiple buffers
 *     VkSampler                   // Input 2: Single sampler
 * >>();
 *
 * // Connect from various sources
 * connect(imageNode["output"], gatherer["input0"]);
 * connect(bufferNode["buffers"], gatherer["input1"]);
 * connect(samplerNode["sampler"], gatherer["input2"]);
 *
 * // Gathered output is vector<ResourceVariant>
 * connect(gatherer["gathered"], consumerNode["resources"]);
 * ```
 *
 * Works with field extraction:
 * ```cpp
 * auto gatherer = graph.addNode<ResourceGathererNode<
 *     std::vector<VkImageView>,
 *     VkSwapchainKHR
 * >>();
 *
 * // Extract field from struct output
 * connect(
 *     swapchainNode.output<SwapChainPublicVariables>("swapchain"),
 *     &SwapChainPublicVariables::images,  // Field extraction!
 *     gatherer.input<std::vector<VkImageView>>("input0")
 * );
 * ```
 *
 * @tparam InputTypes Types of each input slot (variadic)
 */
template<typename... InputTypes>
class ResourceGathererNode {
public:
    // ========================================================================
    // TYPE VALIDATION - Ensure all input types are valid
    // ========================================================================

    // Validate each input type at compile time
    static_assert((ResourceTypeTraits<InputTypes>::isValid && ...),
        "All input types must be registered in RESOURCE_TYPE_REGISTRY");

    // Compile-time constants
    static constexpr size_t INPUT_COUNT = sizeof...(InputTypes);

    // ========================================================================
    // INPUT SLOTS - One typed slot per input
    // ========================================================================

    /**
     * @brief Tuple of typed input slots
     *
     * Each slot is typed according to the template parameters:
     * - Slot<VkImage> for scalar inputs
     * - Slot<std::vector<VkBuffer>> for vector inputs
     * - Slot<ResourceVariant> for any-type inputs
     */
    std::tuple<Slot<InputTypes>...> inputs;

    /**
     * @brief Helper to access input slot by index
     * @tparam Index Compile-time index of input slot
     * @return Reference to the typed slot
     */
    template<size_t Index>
    auto& input() {
        static_assert(Index < INPUT_COUNT, "Input index out of range");
        return std::get<Index>(inputs);
    }

    /**
     * @brief Helper to access input slot by name (for graph connections)
     * @param name Slot name (e.g., "input0", "input1", ...)
     * @return Slot reference (needs runtime dispatch)
     */
    // Note: This would need framework-specific implementation
    // For compile-time access, use input<Index>()

    // ========================================================================
    // OUTPUT SLOTS - Gathered resources
    // ========================================================================

    /**
     * @brief Output slot containing all gathered resources
     *
     * Type is vector<PassThroughStorage> to hold heterogeneous resources
     */
    Slot<std::vector<PassThroughStorage>> gatheredResources;

    /**
     * @brief Optional: Individual outputs for each input (pass-through)
     *
     * Useful if downstream nodes need typed access to specific inputs
     */
    std::tuple<Slot<InputTypes>...> outputs;

    template<size_t Index>
    auto& output() {
        static_assert(Index < INPUT_COUNT, "Output index out of range");
        return std::get<Index>(outputs);
    }

    // ========================================================================
    // EXECUTION - Gather all inputs into output
    // ========================================================================

    /**
     * @brief Execute node: collect all inputs into gathered output
     */
    void execute() {
        std::vector<PassThroughStorage> gathered;
        gathered.reserve(INPUT_COUNT);

        // Fold expression to gather all inputs
        std::apply([&gathered](auto&... slots) {
            // For each input slot, convert to PassThroughStorage and add to vector
            (gathered.push_back(convertToStorage(slots.get())), ...);
        }, inputs);

        gatheredResources.set(std::move(gathered));

        // Optional: Set pass-through outputs
        setPassThroughOutputs(std::index_sequence_for<InputTypes...>{});
    }

private:
    // ========================================================================
    // HELPERS - Convert various types to PassThroughStorage
    // ========================================================================

    /**
     * @brief Convert input value to PassThroughStorage
     *
     * Handles all registered types by storing them directly
     */
    template<typename T>
    static PassThroughStorage convertToStorage(const T& value) {
        PassThroughStorage storage;
        storage.Set(value, ValueTag<T>{});
        return storage;
    }

    /**
     * @brief Set pass-through outputs (copy inputs to outputs)
     */
    template<size_t... Indices>
    void setPassThroughOutputs(std::index_sequence<Indices...>) {
        // For each index, copy input to output
        ((std::get<Indices>(outputs).set(std::get<Indices>(inputs).get())), ...);
    }
};

// ============================================================================
// SPECIALIZED GATHERERS - Common use cases
// ============================================================================

/**
 * @brief Homogeneous gatherer - all inputs same type
 *
 * Convenience wrapper for gathering N inputs of the same type
 *
 * Example:
 * ```cpp
 * auto gatherer = graph.addNode<HomogeneousGatherer<VkImage, 5>>();
 * // Accepts 5 VkImage inputs, outputs vector<VkImage>
 * ```
 */
template<typename T, size_t Count>
class HomogeneousGatherer {
    static_assert(ResourceTypeTraits<T>::isValid, "Type must be registered");

public:
    static constexpr size_t INPUT_COUNT = Count;

    // Array of input slots
    std::array<Slot<T>, Count> inputs;

    // Output as vector
    Slot<std::vector<T>> gatheredResources;

    void execute() {
        std::vector<T> gathered;
        gathered.reserve(Count);

        for (size_t i = 0; i < Count; ++i) {
            gathered.push_back(inputs[i].get());
        }

        gatheredResources.set(std::move(gathered));
    }
};

/**
 * @brief Universal gatherer - accepts ResourceVariant for maximum flexibility
 *
 * Example:
 * ```cpp
 * auto gatherer = graph.addNode<UniversalGatherer<10>>();
 * // Each input accepts ANY registered type
 * ```
 */
template<size_t Count>
class UniversalGatherer {
public:
    static constexpr size_t INPUT_COUNT = Count;

    std::array<Slot<PassThroughStorage>, Count> inputs;
    Slot<std::vector<PassThroughStorage>> gatheredResources;

    void execute() {
        std::vector<PassThroughStorage> gathered;
        gathered.reserve(Count);

        for (size_t i = 0; i < Count; ++i) {
            gathered.push_back(inputs[i].get());
        }

        gatheredResources.set(std::move(gathered));
    }
};

/**
 * @brief Categorized gatherer - separates resources by type
 *
 * Outputs a structured collection with resources categorized by type
 *
 * Example:
 * ```cpp
 * auto gatherer = graph.addNode<CategorizedGatherer<
 *     VkImage, VkBuffer, VkSampler
 * >>();
 *
 * // Extract specific categories downstream
 * connect(gatherer["output"], &CategorizedOutput::images, imageProcessor["images"]);
 * ```
 */
template<typename... InputTypes>
class CategorizedGatherer {
    static_assert((ResourceTypeTraits<InputTypes>::isValid && ...),
        "All types must be registered");

public:
    // Define output structure
    struct CategorizedOutput {
        std::tuple<std::vector<InputTypes>...> categories;

        // Helper accessors
        template<size_t Index>
        auto& category() { return std::get<Index>(categories); }
    };

    std::tuple<Slot<InputTypes>...> inputs;
    Slot<CategorizedOutput> output;

    void execute() {
        CategorizedOutput categorized;

        // Each input goes to its corresponding category
        categorizeInputs(categorized, std::index_sequence_for<InputTypes...>{});

        output.set(std::move(categorized));
    }

private:
    template<size_t... Indices>
    void categorizeInputs(CategorizedOutput& out, std::index_sequence<Indices...>) {
        // For each input, add to its category vector
        ((std::get<Indices>(out.categories).push_back(
            std::get<Indices>(inputs).get()
        )), ...);
    }
};

// ============================================================================
// TYPE ALIASES - Common gatherer patterns
// ============================================================================

// Gather images
template<size_t Count>
using ImageGatherer = HomogeneousGatherer<VkImage, Count>;

// Gather buffers
template<size_t Count>
using BufferGatherer = HomogeneousGatherer<VkBuffer, Count>;

// Gather any texture-related resources
using TextureResources = std::variant<VkImage, VkImageView, VkSampler>;
template<size_t Count>
using TextureGatherer = HomogeneousGatherer<TextureResources, Count>;

// Mixed resource gatherer (common types)
using MixedResourceGatherer = ResourceGathererNode<
    VkImage,
    VkBuffer,
    VkImageView,
    VkSampler
>;

} // namespace Vixen::RenderGraph
