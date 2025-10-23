#pragma once

#include "Core/ResourceConfig.h"
#include <ShaderManagement/ShaderProgram.h>

namespace Vixen::RenderGraph {

/**
 * @brief Shader program descriptor with Vulkan objects
 *
 * This is the RenderGraph-side version that holds VkShaderModule.
 * ShaderManagement library returns SPIRV, this converts it to Vulkan objects.
 */
struct ShaderProgramDescriptor {
    uint32_t programId;
    std::string name;
    ShaderManagement::PipelineTypeConstraint pipelineType;

    // Vulkan shader modules (created from SPIRV)
    struct CompiledStage {
        ShaderManagement::ShaderStage stage;
        VkShaderModule module = VK_NULL_HANDLE;
        std::string entryPoint;
        uint64_t generation = 0;
    };

    std::vector<CompiledStage> stages;

    // Cached Vulkan stage create infos
    std::vector<VkPipelineShaderStageCreateInfo> vkStageInfos;

    // Generation tracking
    uint64_t generation = 0;

    /**
     * @brief Get shader module for specific stage
     */
    VkShaderModule GetModule(ShaderManagement::ShaderStage stage) const {
        auto it = std::find_if(stages.begin(), stages.end(),
            [stage](const auto& s) { return s.stage == stage; });
        return (it != stages.end()) ? it->module : VK_NULL_HANDLE;
    }

    /**
     * @brief Get cached Vulkan stage infos (for pipeline creation)
     */
    const std::vector<VkPipelineShaderStageCreateInfo>& GetVkStageInfos() const {
        return vkStageInfos;
    }

    /**
     * @brief Rebuild cached Vulkan stage infos
     */
    void RebuildVkStageInfos() {
        vkStageInfos.clear();
        for (const auto& stage : stages) {
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.pNext = nullptr;
            stageInfo.stage = ShaderManagement::ToVulkanStage(stage.stage);
            stageInfo.module = stage.module;
            stageInfo.pName = stage.entryPoint.c_str();
            stageInfo.pSpecializationInfo = nullptr;
            stageInfo.flags = 0;

            vkStageInfos.push_back(stageInfo);
        }
    }
};

/**
 * @brief Pure constexpr resource configuration for ShaderLibraryNode
 *
 * Inputs: None (programs registered via API)
 *
 * Outputs:
 * - SHADER_PROGRAMS (ShaderProgramDescriptor*[]) - Array of program descriptors
 *
 * No parameters - programs registered via RegisterProgram() API
 */
CONSTEXPR_NODE_CONFIG(ShaderLibraryNodeConfig, 0, 1, false) {
    // ===== OUTPUTS (1) =====
    // Array of shader program descriptors
    CONSTEXPR_OUTPUT(SHADER_PROGRAMS, ShaderProgramDescriptor*, 0, false);

    ShaderLibraryNodeConfig() {
        // Output: array of pointers to program descriptors
        INIT_OUTPUT_DESC(SHADER_PROGRAMS, "shader_programs",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handles
        );
    }

    // Compile-time validations
    static_assert(SHADER_PROGRAMS_Slot::index == 0, "SHADER_PROGRAMS must be at index 0");
    static_assert(!SHADER_PROGRAMS_Slot::nullable, "SHADER_PROGRAMS is required");

    // Type validation
    static_assert(std::is_same_v<SHADER_PROGRAMS_Slot::Type, ShaderProgramDescriptor*>);
};

// Global compile-time validations
static_assert(ShaderLibraryNodeConfig::INPUT_COUNT == 0);
static_assert(ShaderLibraryNodeConfig::OUTPUT_COUNT == 1);

} // namespace Vixen::RenderGraph