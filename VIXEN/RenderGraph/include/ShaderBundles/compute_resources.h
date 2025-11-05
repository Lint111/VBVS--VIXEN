#pragma once

#include "../Nodes/ShaderBundleGatherer.h"
#include <vulkan/vulkan.h>
#include <vector>

namespace Vixen::RenderGraph::ShaderBundles {

// ============================================================================
// COMPUTE SHADER RESOURCES
// ============================================================================

/**
 * @brief Resource requirements for a compute shader
 *
 * This struct acts as a "config file" defining what resources the shader needs.
 * The ShaderBundleGatherer will automatically create input slots for each field.
 */
struct ComputeShaderResources {
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkImageView> inputImages;
    std::vector<VkImageView> outputImages;
    VkPipeline computePipeline;
    VkPipelineLayout pipelineLayout;
};

// ============================================================================
// IMAGE PROCESSING SHADER
// ============================================================================

/**
 * @brief Resources for image processing shader
 */
struct ImageProcessingShader {
    VkImageView inputImage;
    VkImageView outputImage;
    VkBuffer parametersBuffer;
    VkPipeline pipeline;
};

// ============================================================================
// PARTICLE SIMULATION SHADER
// ============================================================================

/**
 * @brief Resources for particle simulation compute shader
 */
struct ParticleSimulationShader {
    VkBuffer positionBuffer;
    VkBuffer velocityBuffer;
    VkImageView forceFieldTexture;
    VkBuffer uniformBuffer;
    VkPipeline computePipeline;
};

// ============================================================================
// RAYTRACING SHADER RESOURCES
// ============================================================================

/**
 * @brief Resources for raytracing shader
 */
struct RaytracingShaderResources {
    VkBuffer accelerationStructure;
    std::vector<VkBuffer> vertexBuffers;
    std::vector<VkBuffer> indexBuffers;
    VkImageView outputImage;
    VkPipeline raytracingPipeline;
};

} // namespace Vixen::RenderGraph::ShaderBundles

// ============================================================================
// SHADER BUNDLE TRAITS SPECIALIZATIONS
// These must be in the Vixen::RenderGraph namespace
// ============================================================================

namespace Vixen::RenderGraph {

// Trait specialization for ComputeShaderResources
template<>
struct ShaderBundleTraits<ShaderBundles::ComputeShaderResources> {
    using FieldTypes = std::tuple<
        std::vector<VkBuffer>,      // uniformBuffers
        std::vector<VkImageView>,   // inputImages
        std::vector<VkImageView>,   // outputImages
        VkPipeline,                 // computePipeline
        VkPipelineLayout            // pipelineLayout
    >;
    static constexpr size_t FieldCount = 5;
    static constexpr bool IsReflectable = true;
};

// Trait specialization for ImageProcessingShader
template<>
struct ShaderBundleTraits<ShaderBundles::ImageProcessingShader> {
    using FieldTypes = std::tuple<
        VkImageView,    // inputImage
        VkImageView,    // outputImage
        VkBuffer,       // parametersBuffer
        VkPipeline      // pipeline
    >;
    static constexpr size_t FieldCount = 4;
    static constexpr bool IsReflectable = true;
};

// Trait specialization for ParticleSimulationShader
template<>
struct ShaderBundleTraits<ShaderBundles::ParticleSimulationShader> {
    using FieldTypes = std::tuple<
        VkBuffer,       // positionBuffer
        VkBuffer,       // velocityBuffer
        VkImageView,    // forceFieldTexture
        VkBuffer,       // uniformBuffer
        VkPipeline      // computePipeline
    >;
    static constexpr size_t FieldCount = 5;
    static constexpr bool IsReflectable = true;
};

// Trait specialization for RaytracingShaderResources
template<>
struct ShaderBundleTraits<ShaderBundles::RaytracingShaderResources> {
    using FieldTypes = std::tuple<
        VkBuffer,                   // accelerationStructure
        std::vector<VkBuffer>,      // vertexBuffers
        std::vector<VkBuffer>,      // indexBuffers
        VkImageView,                // outputImage
        VkPipeline                  // raytracingPipeline
    >;
    static constexpr size_t FieldCount = 5;
    static constexpr bool IsReflectable = true;
};

} // namespace Vixen::RenderGraph

// ============================================================================
// USAGE EXAMPLE
// ============================================================================

/*

// Step 1: Include the shader bundle header (the "config file")
#include "ShaderBundles/compute_resources.h"

using namespace Vixen::RenderGraph;
using namespace Vixen::RenderGraph::ShaderBundles;

// Step 2: Create gatherer parameterized by bundle type
ShaderBundleGatherer<ComputeShaderResources> gatherer;

// The gatherer automatically creates typed input slots:
// - input<0>(): Slot<std::vector<VkBuffer>>       for uniformBuffers
// - input<1>(): Slot<std::vector<VkImageView>>    for inputImages
// - input<2>(): Slot<std::vector<VkImageView>>    for outputImages
// - input<3>(): Slot<VkPipeline>                  for computePipeline
// - input<4>(): Slot<VkPipelineLayout>            for pipelineLayout

// Step 3: Connect inputs (order matches field order in struct)
gatherer.input<0>().connectFrom(uniformBufferSlot);
gatherer.input<1>().connectFrom(inputImagesSlot);
gatherer.input<2>().connectFrom(outputImagesSlot);
gatherer.input<3>().connectFrom(pipelineSlot);
gatherer.input<4>().connectFrom(layoutSlot);

// Step 4: Execute to assemble the bundle
gatherer.execute();

// Step 5: Get the assembled bundle (fully typed!)
auto& resources = gatherer.output.get();
// Type is ComputeShaderResources with all fields populated

// Pass to compute node
connect(gatherer["output"], computeNode.input<ComputeShaderResources>("resources"));

// BENEFITS:
// ✅ Shader requirements defined in ONE header file
// ✅ Automatic input slot generation from bundle type
// ✅ Compile-time type validation (wrong type = compile error)
// ✅ Minimal graph setup (just connect inputs)
// ✅ Type-safe output (no casting or runtime checks)
// ✅ Refactoring-safe (rename field = update everywhere)
// ✅ IDE autocomplete support
// ✅ Version control for shader interfaces

*/
