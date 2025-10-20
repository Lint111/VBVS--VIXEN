#pragma once
#include "Headers.h"

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

const int MAX_SHADER_STAGES = 2;

class VulkanShader {
    public:
    VulkanShader() {
        memset(shaderStages, 0, sizeof(shaderStages));
    }
    ~VulkanShader() {}

    // Use .spv and build shader modules directly
    void BuildShaderModuleWithSPV(uint32_t* vertShaderText, size_t vertexSPVSize,
                                  uint32_t* fragShaderText, size_t fragSPVSize, Vixen::Vulkan::Resources::VulkanDevice* deviceObj);

    // Kill the shader when not required
    void DestroyShader(Vixen::Vulkan::Resources::VulkanDevice* deviceObj);

    
    #ifdef AUTO_COMPILE_GLSL_TO_SPV
    // Convert GLSL text to SPV binary
    bool GLSLtoSPV(const VkShaderStageFlagBits shaderType,
                   const char* pShaderText,
                   std::vector<unsigned int>& spirv);


    void BuildShader(const char* vertShaderText, const char* fragShaderText, Vixen::Vulkan::Resources::VulkanDevice* deviceObj);

    EShLanguage GetLanguage(const VkShaderStageFlagBits shaderType);

    void InitializeResources(TBuiltInResource &Resources);

    #endif // AUTO_COMPILE_GLSL_TO_SPV

    VkPipelineShaderStageCreateInfo shaderStages[MAX_SHADER_STAGES];

    bool initialized = false;
    uint32_t stagesCount = 0;
    
};