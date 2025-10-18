#pragma once

#include "../NodeType.h"
#include "../NodeInstance.h"
#include "VulkanShader.h"

namespace Vixen::RenderGraph {

/**
 * @brief Shader loading and compilation node
 * 
 * Responsibilities:
 * - Load shader files (GLSL or SPIR-V)
 * - Compile GLSL to SPIR-V (if AUTO_COMPILE_GLSL_TO_SPV defined)
 * - Create VkShaderModule objects
 * - Store shader stage create info for pipeline creation
 * 
 * Inputs: None (shader paths are parameters)
 * Outputs:
 *   [0] Vertex shader stage info (opaque)
 *   [1] Fragment shader stage info (opaque)
 * 
 * Parameters:
 *   - vertexShaderPath: std::string - Path to vertex shader
 *   - fragmentShaderPath: std::string - Path to fragment shader
 *   - autoCompile: bool - Compile from GLSL (vs load .spv) [default: true if AUTO_COMPILE_GLSL_TO_SPV]
 */
class ShaderNode : public NodeInstance {
public:
    ShaderNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );

    virtual ~ShaderNode();

    // NodeInstance interface
    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // Access shader stage info for pipeline creation
    const VkPipelineShaderStageCreateInfo* GetShaderStages() const { return shaderStages; }
    uint32_t GetStageCount() const { return stageCount; }

private:
    VkShaderModule vertexShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragmentShaderModule = VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo shaderStages[2];
    uint32_t stageCount = 0;

    void* ReadShaderFile(const char* filename, size_t* fileSize);
    void CreateShaderModule(const uint32_t* code, size_t codeSize, VkShaderModule* shaderModule);
    
#ifdef AUTO_COMPILE_GLSL_TO_SPV
    bool CompileGLSLToSPV(
        VkShaderStageFlagBits shaderType,
        const char* glslSource,
        std::vector<uint32_t>& spirv
    );
#endif
};

/**
 * @brief Type definition for ShaderNode
 */
class ShaderNodeType : public NodeType {
public:
    ShaderNodeType();

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

} // namespace Vixen::RenderGraph
