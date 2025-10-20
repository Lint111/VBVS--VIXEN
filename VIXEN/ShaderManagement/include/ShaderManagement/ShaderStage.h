#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace ShaderManagement {

/**
 * @brief Shader stage enumeration
 *
 * Maps directly to Vulkan shader stage flags for easy conversion.
 * Pure enum - no Vulkan device operations.
 */
enum class ShaderStage : uint32_t {
    Vertex          = VK_SHADER_STAGE_VERTEX_BIT,
    Fragment        = VK_SHADER_STAGE_FRAGMENT_BIT,
    Compute         = VK_SHADER_STAGE_COMPUTE_BIT,
    Geometry        = VK_SHADER_STAGE_GEOMETRY_BIT,
    TessControl     = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
    TessEval        = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
    Mesh            = VK_SHADER_STAGE_MESH_BIT_EXT,
    Task            = VK_SHADER_STAGE_TASK_BIT_EXT,
    RayGen          = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
    Miss            = VK_SHADER_STAGE_MISS_BIT_KHR,
    ClosestHit      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
    AnyHit          = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
    Intersection    = VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
    Callable        = VK_SHADER_STAGE_CALLABLE_BIT_KHR,
};

/**
 * @brief Convert ShaderStage to Vulkan flag bits
 */
inline constexpr VkShaderStageFlagBits ToVulkanStage(ShaderStage stage) {
    return static_cast<VkShaderStageFlagBits>(stage);
}

/**
 * @brief Pipeline type constraints
 *
 * Defines which shader stages are required/optional for each pipeline type.
 */
enum class PipelineTypeConstraint : uint8_t {
    Graphics,   // vertex+fragment required, geometry/tess optional
    Mesh,       // mesh+fragment required, task optional
    Compute,    // compute stage only
    RayTracing, // raygen+miss+closesthit required, anyhit/intersection optional
};

/**
 * @brief Get human-readable name for shader stage (debugging)
 */
inline const char* ShaderStageName(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:       return "Vertex";
        case ShaderStage::Fragment:     return "Fragment";
        case ShaderStage::Compute:      return "Compute";
        case ShaderStage::Geometry:     return "Geometry";
        case ShaderStage::TessControl:  return "TessellationControl";
        case ShaderStage::TessEval:     return "TessellationEvaluation";
        case ShaderStage::Mesh:         return "Mesh";
        case ShaderStage::Task:         return "Task";
        case ShaderStage::RayGen:       return "RayGeneration";
        case ShaderStage::Miss:         return "Miss";
        case ShaderStage::ClosestHit:   return "ClosestHit";
        case ShaderStage::AnyHit:       return "AnyHit";
        case ShaderStage::Intersection: return "Intersection";
        case ShaderStage::Callable:     return "Callable";
        default:                        return "Unknown";
    }
}

/**
 * @brief Get human-readable name for pipeline type (debugging)
 */
inline const char* PipelineTypeName(PipelineTypeConstraint type) {
    switch (type) {
        case PipelineTypeConstraint::Graphics:   return "Graphics";
        case PipelineTypeConstraint::Mesh:       return "Mesh";
        case PipelineTypeConstraint::Compute:    return "Compute";
        case PipelineTypeConstraint::RayTracing: return "RayTracing";
        default:                                 return "Unknown";
    }
}

} // namespace ShaderManagement