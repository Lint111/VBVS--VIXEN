#pragma once

#include "ResourceGathererNode.h"
#include "Core/FieldExtractor.h"
#include <tuple>
#include <type_traits>

namespace Vixen::RenderGraph {

// ============================================================================
// CONFIGURED RESOURCE GATHERER - Uses shader bundle struct as config
// ============================================================================

/**
 * @brief Resource gatherer configured by shader bundle struct
 *
 * Instead of manually specifying input types, this gatherer reads a shader
 * bundle struct (e.g., from compute.h) and automatically creates the right
 * input slots.
 *
 * Example shader bundle struct (compute.h):
 * ```cpp
 * struct ComputeShaderResources {
 *     std::vector<VkImageView> inputImages;
 *     std::vector<VkBuffer> uniformBuffers;
 *     VkImageView outputImage;
 *     VkPipeline pipeline;
 * };
 * ```
 *
 * Usage:
 * ```cpp
 * // Gatherer automatically creates slots for all members!
 * auto gatherer = graph.addNode<ConfiguredGatherer<ComputeShaderResources>>();
 *
 * // Connect to named inputs (matches struct member names)
 * connect(imageNode["output"], gatherer["inputImages"]);
 * connect(bufferNode["buffers"], gatherer["uniformBuffers"]);
 * connect(pipelineNode["pipeline"], gatherer["pipeline"]);
 *
 * // Output is the complete configured struct
 * connect(gatherer["resources"], computeNode.input<ComputeShaderResources>("resources"));
 * ```
 *
 * @tparam ConfigStruct The shader bundle struct defining resource requirements
 */
template<typename ConfigStruct>
class ConfiguredGatherer {
public:
    // ========================================================================
    // CONFIGURATION - Extract types from struct
    // ========================================================================

    using Config = ConfigStruct;

    // ========================================================================
    // INPUT SLOTS - Dynamically created from struct members
    // ========================================================================

    /**
     * @brief Storage for input values before assembling into output struct
     *
     * In a real implementation, you'd use reflection or macro magic to create
     * individual named slots. For now, we demonstrate the concept with the
     * struct as a single input that gets assembled from parts.
     */

    // Individual field inputs (would be generated via reflection/macros)
    // For demonstration, we'll show manual approach and the automatic approach

    /**
     * @brief Set an input field by pointer-to-member
     *
     * This allows setting struct fields directly:
     * ```cpp
     * gatherer.setField(&ComputeShaderResources::inputImages, myImages);
     * ```
     */
    template<typename FieldType>
    void setField(FieldType ConfigStruct::* member, const FieldType& value) {
        output_.*member = value;
        output_.isSet = true;
    }

    template<typename FieldType>
    void setField(FieldType ConfigStruct::* member, FieldType&& value) {
        output_.*member = std::move(value);
        output_.isSet = true;
    }

    // ========================================================================
    // OUTPUT - Assembled configuration struct
    // ========================================================================

    /**
     * @brief Output slot containing the fully assembled config struct
     */
    Slot<ConfigStruct> configuredResources;

    /**
     * @brief Execute: Assemble all inputs into output config struct
     */
    void execute() {
        if (output_.isSet) {
            configuredResources.set(std::move(output_.data));
        }
    }

private:
    struct OutputStorage {
        ConfigStruct data;
        bool isSet = false;
    } output_;
};

// ============================================================================
// NAMED FIELD GATHERER - Reference shader bundle + field name
// ============================================================================

/**
 * @brief Gatherer that references shader bundle struct and field names
 *
 * This pattern lets you use shader bundle headers as "sudo config files"
 * where the struct defines what resources are needed.
 *
 * Example shader bundle (compute_shader.h):
 * ```cpp
 * namespace ShaderBundles {
 *     struct ComputePass {
 *         std::vector<VkImageView> inputTextures;
 *         std::vector<VkBuffer> uniformBuffers;
 *         VkImageView outputTexture;
 *     };
 * }
 * ```
 *
 * Usage:
 * ```cpp
 * // Gatherer knows to collect fields from ComputePass struct
 * auto gatherer = graph.addNode<ShaderBundleGatherer<ShaderBundles::ComputePass>>();
 *
 * // Connect specific fields using field extractors
 * gatherer.connectField(
 *     &ShaderBundles::ComputePass::inputTextures,
 *     sourceNode["textures"]
 * );
 *
 * gatherer.connectField(
 *     &ShaderBundles::ComputePass::uniformBuffers,
 *     uniformNode["buffers"]
 * );
 *
 * // Output is the complete bundle ready for shader
 * connect(gatherer["bundle"], computeNode["shaderResources"]);
 * ```
 */
template<typename BundleStruct>
class ShaderBundleGatherer {
public:
    using Bundle = BundleStruct;

    /**
     * @brief Connect a source to a specific bundle field
     *
     * @tparam FieldType The type of the field
     * @param field Pointer-to-member identifying the field
     * @param inputSlot The input slot to read from
     */
    template<typename FieldType>
    void connectField(FieldType Bundle::* field, Slot<FieldType>& inputSlot) {
        // Store the connection for execution
        // In real implementation, this would register the connection in the graph
        fieldInputs_.push_back([this, field, &inputSlot]() {
            bundle_.*field = inputSlot.get();
        });
    }

    /**
     * @brief Output slot containing the assembled bundle
     */
    Slot<Bundle> assembledBundle;

    /**
     * @brief Execute: Gather all fields into the bundle
     */
    void execute() {
        // Execute all field connections
        for (auto& conn : fieldInputs_) {
            conn();
        }

        assembledBundle.set(bundle_);
    }

private:
    Bundle bundle_;
    std::vector<std::function<void()>> fieldInputs_;
};

// ============================================================================
// REFLECTION-BASED GATHERER - Automatic slot generation
// ============================================================================

/**
 * @brief Gatherer that automatically generates slots from struct reflection
 *
 * This is the "ultimate" solution - given a shader bundle struct, it:
 * 1. Automatically creates input slots for each member
 * 2. Names the slots after the member names
 * 3. Assembles them into the output struct
 *
 * Requires reflection or macro generation. Here's the concept:
 *
 * ```cpp
 * // Define shader requirements in header (e.g., compute.h)
 * SHADER_BUNDLE(ComputeShader) {
 *     FIELD(std::vector<VkImageView>, inputImages)
 *     FIELD(std::vector<VkBuffer>, uniformBuffers)
 *     FIELD(VkImageView, outputImage)
 * };
 *
 * // Gatherer auto-generates:
 * // - input slot "inputImages" of type vector<VkImageView>
 * // - input slot "uniformBuffers" of type vector<VkBuffer>
 * // - input slot "outputImage" of type VkImageView
 * auto gatherer = graph.addNode<ReflectedGatherer<ComputeShader>>();
 *
 * // Connect by name (matches FIELD names)
 * connect(imageArray["outputs"], gatherer["inputImages"]);
 * connect(bufferSet["buffers"], gatherer["uniformBuffers"]);
 * connect(outputNode["image"], gatherer["outputImage"]);
 * ```
 */

// Macro for defining reflectable shader bundles
#define SHADER_BUNDLE_BEGIN(Name) \
    struct Name { \
        static constexpr const char* bundle_name = #Name;

#define SHADER_BUNDLE_FIELD(Type, Name) \
    Type Name; \
    static constexpr const char* field_##Name##_name = #Name;

#define SHADER_BUNDLE_END() };

// Example usage in compute.h:
/*
SHADER_BUNDLE_BEGIN(ComputeShaderBundle)
    SHADER_BUNDLE_FIELD(std::vector<VkImageView>, inputImages)
    SHADER_BUNDLE_FIELD(std::vector<VkBuffer>, uniformBuffers)
    SHADER_BUNDLE_FIELD(VkImageView, outputImage)
SHADER_BUNDLE_END()
*/

/**
 * @brief Helper to check if a struct is a shader bundle (has bundle_name)
 */
template<typename T, typename = void>
struct IsShaderBundle : std::false_type {};

template<typename T>
struct IsShaderBundle<T, std::void_t<decltype(T::bundle_name)>> : std::true_type {};

template<typename T>
inline constexpr bool IsShaderBundle_v = IsShaderBundle<T>::value;

// ============================================================================
// TYPE-DRIVEN GATHERER - Extracts config from struct type itself
// ============================================================================

/**
 * @brief Gatherer that uses struct type as configuration
 *
 * This pattern treats the shader bundle struct as a "type-level configuration":
 * - The struct members define what inputs are needed
 * - The types are automatically validated
 * - The output is the assembled struct
 *
 * Real-world example with Phase G compute shaders:
 * ```cpp
 * // In compute_shader_reflection.h (generated from SPIR-V):
 * struct ComputeShaderDescriptors {
 *     std::vector<VkBuffer> uniformBuffers;      // Set 0, bindings 0-N
 *     std::vector<VkImageView> sampledImages;    // Set 1, bindings 0-M
 *     std::vector<VkImageView> storageImages;    // Set 2, bindings 0-K
 * };
 *
 * // Gatherer configured by this type:
 * auto gatherer = graph.addNode<TypeConfiguredGatherer<ComputeShaderDescriptors>>();
 *
 * // The gatherer knows it needs:
 * // - Input for uniformBuffers (vector<VkBuffer>)
 * // - Input for sampledImages (vector<VkImageView>)
 * // - Input for storageImages (vector<VkImageView>)
 *
 * // Connect from upstream nodes:
 * gatherer.field(&ComputeShaderDescriptors::uniformBuffers).connectFrom(bufferArray);
 * gatherer.field(&ComputeShaderDescriptors::sampledImages).connectFrom(textureArray);
 * gatherer.field(&ComputeShaderDescriptors::storageImages).connectFrom(outputImages);
 *
 * // Output is ready-to-use descriptor struct:
 * connect(gatherer["descriptors"], computeDispatchNode["resources"]);
 * ```
 */
template<typename ConfigStruct>
class TypeConfiguredGatherer {
public:
    using Config = ConfigStruct;

    // Validate that all fields are valid resource types
    // (In real implementation, you'd use reflection to iterate all members)

    /**
     * @brief Field accessor for connection
     *
     * Returns a wrapper that allows connecting to a specific field:
     * ```cpp
     * gatherer.field(&Struct::member).connectFrom(sourceSlot);
     * ```
     */
    template<typename FieldType>
    class FieldConnector {
    public:
        FieldConnector(TypeConfiguredGatherer* gatherer, FieldType Config::* member)
            : gatherer_(gatherer), member_(member) {}

        void connectFrom(const FieldType& source) {
            gatherer_->output_.*member_ = source;
        }

        void connectFrom(Slot<FieldType>& sourceSlot) {
            gatherer_->connections_.push_back([this, &sourceSlot]() {
                gatherer_->output_.*member_ = sourceSlot.get();
            });
        }

    private:
        TypeConfiguredGatherer* gatherer_;
        FieldType Config::* member_;
    };

    template<typename FieldType>
    FieldConnector<FieldType> field(FieldType Config::* member) {
        return FieldConnector<FieldType>(this, member);
    }

    /**
     * @brief Output: assembled configuration struct
     */
    Slot<Config> assembledConfig;

    /**
     * @brief Execute: gather all connected fields
     */
    void execute() {
        // Execute all field connections
        for (auto& conn : connections_) {
            conn();
        }

        assembledConfig.set(output_);
    }

private:
    Config output_;
    std::vector<std::function<void()>> connections_;
};

} // namespace Vixen::RenderGraph
