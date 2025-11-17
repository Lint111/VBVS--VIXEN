#pragma once

#include "ShaderDataBundle.h"
#include "ShaderStage.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstdint>

namespace ShaderManagement {

/**
 * @brief Validation result for device capability checking
 */
struct DeviceValidationResult {
    bool compatible = true;                ///< True if shader is compatible with device
    std::vector<std::string> errors;       ///< List of capability errors
    std::vector<std::string> warnings;     ///< List of capability warnings
    std::vector<std::string> missingFeatures;     ///< Required features not supported
    std::vector<std::string> missingExtensions;   ///< Required extensions not enabled

    /**
     * @brief Check if validation passed without errors
     */
    bool IsValid() const { return compatible && errors.empty(); }

    /**
     * @brief Get formatted error message
     */
    std::string GetErrorMessage() const {
        std::string msg;
        for (const auto& error : errors) {
            msg += "ERROR: " + error + "\n";
        }
        for (const auto& warning : warnings) {
            msg += "WARNING: " + warning + "\n";
        }
        return msg;
    }
};

/**
 * @brief Vulkan device capability validator
 *
 * Validates whether a compiled shader bundle can run on a given Vulkan device.
 * Checks:
 * - Required shader stages support
 * - Pipeline type capabilities
 * - Descriptor set limits
 * - Push constant limits
 * - Required features (mesh shaders, ray tracing, etc.)
 * - Required extensions
 *
 * Usage:
 * @code
 * VulkanDeviceValidator validator(physicalDevice);
 *
 * // Validate shader bundle
 * auto result = validator.ValidateBundle(bundle);
 * if (!result.IsValid()) {
 *     std::cerr << "Shader not compatible: " << result.GetErrorMessage();
 * }
 *
 * // Check specific features
 * if (!validator.SupportsGeometryShaders()) {
 *     // Device doesn't support geometry shaders
 * }
 * @endcode
 */
class VulkanDeviceValidator {
public:
    /**
     * @brief Construct validator for a Vulkan physical device
     *
     * @param physicalDevice Vulkan physical device to validate against
     */
    explicit VulkanDeviceValidator(VkPhysicalDevice physicalDevice)
        : physicalDevice_(physicalDevice)
    {
        QueryDeviceCapabilities();
    }

    /**
     * @brief Validate shader bundle against device capabilities
     *
     * Checks if all shader stages, descriptor layouts, and features required
     * by the bundle are supported by the device.
     *
     * @param bundle Shader data bundle to validate
     * @return Validation result with errors/warnings
     */
    DeviceValidationResult ValidateBundle(const ShaderDataBundle& bundle) const {
        DeviceValidationResult result;

        // Validate pipeline type support
        ValidatePipelineType(bundle.program.pipelineType, result);

        // Validate shader stages
        for (const auto& stage : bundle.program.stages) {
            ValidateShaderStage(stage.stage, result);
        }

        // Validate descriptor set layouts
        if (bundle.reflectionData) {
            ValidateDescriptorSets(*bundle.reflectionData, result);
            ValidatePushConstants(*bundle.reflectionData, result);
            ValidateVertexInputs(*bundle.reflectionData, result);
        }

        // Validate SPIRV size
        ValidateSpirvSize(bundle, result);

        result.compatible = result.errors.empty();
        return result;
    }

    // ===== Feature Queries =====

    bool SupportsGeometryShaders() const {
        return features_.geometryShader == VK_TRUE;
    }

    bool SupportsTessellationShaders() const {
        return features_.tessellationShader == VK_TRUE;
    }

    bool SupportsComputeShaders() const {
        return limits_.maxComputeWorkGroupCount[0] > 0;
    }

    bool SupportsMeshShaders() const {
        return meshShaderFeatures_.meshShader == VK_TRUE;
    }

    bool SupportsTaskShaders() const {
        return meshShaderFeatures_.taskShader == VK_TRUE;
    }

    bool SupportsRayTracing() const {
        return rayTracingFeatures_.rayTracingPipeline == VK_TRUE;
    }

    uint32_t GetMaxDescriptorSets() const {
        return limits_.maxBoundDescriptorSets;
    }

    uint32_t GetMaxPushConstantsSize() const {
        return limits_.maxPushConstantsSize;
    }

    uint32_t GetMaxVertexInputAttributes() const {
        return limits_.maxVertexInputAttributes;
    }

    const VkPhysicalDeviceLimits& GetLimits() const {
        return limits_;
    }

private:
    void QueryDeviceCapabilities() {
        // Query basic features and limits
        VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

        // Chain mesh shader features if available
        meshShaderFeatures_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        meshShaderFeatures_.pNext = nullptr;
        features2.pNext = &meshShaderFeatures_;

        // Chain ray tracing features
        rayTracingFeatures_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        rayTracingFeatures_.pNext = features2.pNext;
        features2.pNext = &rayTracingFeatures_;

        vkGetPhysicalDeviceFeatures2(physicalDevice_, &features2);
        features_ = features2.features;

        // Query device properties
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevice_, &props);
        limits_ = props.limits;
    }

    void ValidatePipelineType(PipelineTypeConstraint pipelineType, DeviceValidationResult& result) const {
        switch (pipelineType) {
            case PipelineTypeConstraint::Graphics:
                // All devices support graphics pipelines
                break;

            case PipelineTypeConstraint::Compute:
                if (!SupportsComputeShaders()) {
                    result.errors.push_back("Device does not support compute shaders");
                }
                break;

            case PipelineTypeConstraint::Mesh:
                if (!SupportsMeshShaders()) {
                    result.errors.push_back("Device does not support mesh shaders");
                    result.missingFeatures.push_back("VK_EXT_mesh_shader");
                }
                break;

            case PipelineTypeConstraint::RayTracing:
                if (!SupportsRayTracing()) {
                    result.errors.push_back("Device does not support ray tracing");
                    result.missingFeatures.push_back("VK_KHR_ray_tracing_pipeline");
                }
                break;

            case PipelineTypeConstraint::Any:
                // No specific requirements
                break;
        }
    }

    void ValidateShaderStage(ShaderStage stage, DeviceValidationResult& result) const {
        switch (stage) {
            case ShaderStage::Geometry:
                if (!SupportsGeometryShaders()) {
                    result.errors.push_back("Device does not support geometry shaders");
                    result.missingFeatures.push_back("geometryShader");
                }
                break;

            case ShaderStage::TessellationControl:
            case ShaderStage::TessellationEvaluation:
                if (!SupportsTessellationShaders()) {
                    result.errors.push_back("Device does not support tessellation shaders");
                    result.missingFeatures.push_back("tessellationShader");
                }
                break;

            case ShaderStage::Mesh:
                if (!SupportsMeshShaders()) {
                    result.errors.push_back("Device does not support mesh shaders");
                    result.missingFeatures.push_back("VK_EXT_mesh_shader");
                }
                break;

            case ShaderStage::Task:
                if (!SupportsTaskShaders()) {
                    result.errors.push_back("Device does not support task shaders");
                    result.missingFeatures.push_back("VK_EXT_mesh_shader (taskShader)");
                }
                break;

            case ShaderStage::RayGen:
            case ShaderStage::Miss:
            case ShaderStage::ClosestHit:
            case ShaderStage::AnyHit:
            case ShaderStage::Intersection:
            case ShaderStage::Callable:
                if (!SupportsRayTracing()) {
                    result.errors.push_back("Device does not support ray tracing shaders");
                    result.missingFeatures.push_back("VK_KHR_ray_tracing_pipeline");
                }
                break;

            default:
                // Vertex, Fragment, Compute are universally supported
                break;
        }
    }

    void ValidateDescriptorSets(const SpirvReflectionData& reflection, DeviceValidationResult& result) const {
        // Check number of descriptor sets
        if (reflection.descriptorSets.size() > GetMaxDescriptorSets()) {
            result.errors.push_back("Shader uses " + std::to_string(reflection.descriptorSets.size()) +
                " descriptor sets, but device only supports " + std::to_string(GetMaxDescriptorSets()));
        }

        // Check descriptor counts per set
        for (const auto& [setIdx, bindings] : reflection.descriptorSets) {
            // Check max descriptors per set (varies by type)
            uint32_t uniformBufferCount = 0;
            uint32_t storageBufferCount = 0;
            uint32_t samplerCount = 0;
            uint32_t sampledImageCount = 0;
            uint32_t storageImageCount = 0;

            for (const auto& binding : bindings) {
                switch (binding.descriptorType) {
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                        uniformBufferCount += binding.descriptorCount;
                        break;
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                        storageBufferCount += binding.descriptorCount;
                        break;
                    case VK_DESCRIPTOR_TYPE_SAMPLER:
                        samplerCount += binding.descriptorCount;
                        break;
                    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                        sampledImageCount += binding.descriptorCount;
                        break;
                    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                        storageImageCount += binding.descriptorCount;
                        break;
                    default:
                        break;
                }
            }

            // Validate against device limits
            if (uniformBufferCount > limits_.maxDescriptorSetUniformBuffers) {
                result.errors.push_back("Set " + std::to_string(setIdx) + " uses " +
                    std::to_string(uniformBufferCount) + " uniform buffers, device limit is " +
                    std::to_string(limits_.maxDescriptorSetUniformBuffers));
            }

            if (storageBufferCount > limits_.maxDescriptorSetStorageBuffers) {
                result.errors.push_back("Set " + std::to_string(setIdx) + " uses " +
                    std::to_string(storageBufferCount) + " storage buffers, device limit is " +
                    std::to_string(limits_.maxDescriptorSetStorageBuffers));
            }

            if (sampledImageCount > limits_.maxDescriptorSetSampledImages) {
                result.errors.push_back("Set " + std::to_string(setIdx) + " uses " +
                    std::to_string(sampledImageCount) + " sampled images, device limit is " +
                    std::to_string(limits_.maxDescriptorSetSampledImages));
            }
        }
    }

    void ValidatePushConstants(const SpirvReflectionData& reflection, DeviceValidationResult& result) const {
        for (const auto& pc : reflection.pushConstants) {
            if (pc.size > GetMaxPushConstantsSize()) {
                result.errors.push_back("Push constant '" + pc.name + "' is " +
                    std::to_string(pc.size) + " bytes, device limit is " +
                    std::to_string(GetMaxPushConstantsSize()) + " bytes");
            }
        }
    }

    void ValidateVertexInputs(const SpirvReflectionData& reflection, DeviceValidationResult& result) const {
        if (reflection.vertexInputs.size() > GetMaxVertexInputAttributes()) {
            result.errors.push_back("Shader uses " + std::to_string(reflection.vertexInputs.size()) +
                " vertex input attributes, device limit is " +
                std::to_string(GetMaxVertexInputAttributes()));
        }
    }

    void ValidateSpirvSize(const ShaderDataBundle& bundle, DeviceValidationResult& result) const {
        // Check total SPIRV size (warn if excessively large)
        size_t totalSpirvBytes = 0;
        for (const auto& stage : bundle.program.stages) {
            totalSpirvBytes += stage.spirvCode.size() * sizeof(uint32_t);
        }

        // Warn if SPIRV exceeds 10 MB (unusually large)
        if (totalSpirvBytes > 10 * 1024 * 1024) {
            result.warnings.push_back("Total SPIRV size is very large (" +
                std::to_string(totalSpirvBytes / (1024 * 1024)) + " MB). " +
                "This may impact loading times and memory usage.");
        }
    }

    VkPhysicalDevice physicalDevice_;
    VkPhysicalDeviceFeatures features_{};
    VkPhysicalDeviceLimits limits_{};
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures_{};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures_{};
};

} // namespace ShaderManagement
