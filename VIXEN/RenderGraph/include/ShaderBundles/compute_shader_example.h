#pragma once

#include "Core/ResourceVariant.h"
#include <vector>

/**
 * @file compute_shader_example.h
 * @brief Shader bundle header - acts as "pseudo config file" for resource gathering
 *
 * This header defines what resources a compute shader needs. The resource
 * gatherer can use this as configuration to automatically know what to collect.
 *
 * Instead of manually specifying gatherer inputs, you reference this header
 * and the type system handles everything automatically.
 */

namespace ShaderBundles {

// ============================================================================
// COMPUTE SHADER RESOURCE REQUIREMENTS
// ============================================================================

/**
 * @brief Resource requirements for a generic compute shader
 *
 * This struct acts as a "configuration file" - by including this header,
 * a resource gatherer knows exactly what types to accept and how to
 * assemble them.
 *
 * Usage pattern:
 * ```cpp
 * #include "ShaderBundles/compute_shader_example.h"
 *
 * // Gatherer is configured by this type!
 * auto gatherer = graph.addNode<TypeConfiguredGatherer<
 *     ShaderBundles::ComputeShaderResources
 * >>();
 *
 * // Connect to fields using the header as reference
 * gatherer.field(&ComputeShaderResources::inputImages).connectFrom(imageNode["outputs"]);
 * gatherer.field(&ComputeShaderResources::uniformBuffers).connectFrom(bufferNode["buffers"]);
 * ```
 */
struct ComputeShaderResources {
    // Descriptor Set 0: Uniforms
    std::vector<VkBuffer> uniformBuffers;

    // Descriptor Set 1: Input textures
    std::vector<VkImageView> inputImages;

    // Descriptor Set 2: Output textures
    std::vector<VkImageView> outputImages;

    // Descriptor Set 3: Storage buffers
    std::vector<VkBuffer> storageBuffers;

    // Pipeline state
    VkPipeline computePipeline;
    VkPipelineLayout pipelineLayout;
};

// ============================================================================
// IMAGE PROCESSING COMPUTE SHADER
// ============================================================================

/**
 * @brief Resources for image processing compute shader
 *
 * Example: Gaussian blur, tone mapping, etc.
 */
struct ImageProcessingShader {
    // Single input image
    VkImageView inputImage;

    // Single output image
    VkImageView outputImage;

    // Parameters buffer
    VkBuffer parametersBuffer;

    // Pipeline
    VkPipeline pipeline;
};

// ============================================================================
// PARTICLE SIMULATION COMPUTE SHADER
// ============================================================================

/**
 * @brief Resources for particle simulation
 */
struct ParticleSimulationShader {
    // Particle position buffer (read/write)
    VkBuffer positionBuffer;

    // Particle velocity buffer (read/write)
    VkBuffer velocityBuffer;

    // Simulation parameters (read-only)
    VkBuffer parametersBuffer;

    // Optional force field texture
    VkImageView forceFieldTexture;

    // Compute pipeline
    VkPipeline pipeline;
};

// ============================================================================
// RAYTRACING SHADER RESOURCES
// ============================================================================

/**
 * @brief Resources for compute-based raytracer
 */
struct RaytracingShaderResources {
    // Acceleration structure (if using ray queries in compute)
    VkAccelerationStructureKHR accelerationStructure;

    // Scene textures
    std::vector<VkImageView> sceneTextures;

    // Material buffers
    std::vector<VkBuffer> materialBuffers;

    // Output image
    VkImageView outputImage;

    // Camera uniform buffer
    VkBuffer cameraBuffer;

    // Pipeline
    VkPipeline pipeline;
};

} // namespace ShaderBundles

/**
 * USAGE EXAMPLE - How to use these as "config files":
 *
 * ```cpp
 * // 1. Include the shader bundle header (your "pseudo config file")
 * #include "ShaderBundles/compute_shader_example.h"
 *
 * // 2. Create gatherer configured by the header's struct type
 * auto imageProcessor = graph.addNode<TypeConfiguredGatherer<
 *     ShaderBundles::ImageProcessingShader
 * >>();
 *
 * // 3. Connect resources - the gatherer knows what to accept from the header!
 * imageProcessor.field(&ImageProcessingShader::inputImage)
 *     .connectFrom(inputNode["texture"]);
 *
 * imageProcessor.field(&ImageProcessingShader::outputImage)
 *     .connectFrom(outputNode["renderTarget"]);
 *
 * imageProcessor.field(&ImageProcessingShader::parametersBuffer)
 *     .connectFrom(paramsNode["buffer"]);
 *
 * imageProcessor.field(&ImageProcessingShader::pipeline)
 *     .connectFrom(pipelineNode["compute"]);
 *
 * // 4. Output is the complete configured struct, ready for shader execution
 * connect(imageProcessor["assembledConfig"], dispatchNode["resources"]);
 *
 * // The shader dispatch node receives a fully-typed, validated struct
 * // matching exactly what the compute.h header specified!
 * ```
 *
 * KEY BENEFITS:
 * - Shader requirements defined in ONE place (the .h file)
 * - Type-safe: compiler validates all connections
 * - Refactoring-safe: renaming fields updates all usages
 * - No string-based lookups or runtime type checks
 * - Clear documentation of what each shader needs
 * - Can version-control shader requirements separately
 */
