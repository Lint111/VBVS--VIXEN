#include "RenderGraph/Nodes/ShaderNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "wrapper.h"
#include <fstream>
#include <iostream>

#ifdef AUTO_COMPILE_GLSL_TO_SPV
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#endif

namespace Vixen::RenderGraph {

// ====== ShaderNodeType ======

ShaderNodeType::ShaderNodeType() {
    typeId = 106; // Unique ID
    typeName = "Shader";
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0;

    // No inputs - shaders loaded from files

    // Outputs are opaque (shader stage info stored internally)
    // Pipeline node will access them through ShaderNode interface

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 1024 * 100; // ~100KB for shaders
    workloadMetrics.estimatedComputeCost = 0.3f; // Compilation is moderately expensive
    workloadMetrics.estimatedBandwidthCost = 0.1f;
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> ShaderNodeType::CreateInstance(
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
) const {
    return std::make_unique<ShaderNode>(
        instanceName,
        const_cast<ShaderNodeType*>(this),
        device
    );
}

// ====== ShaderNode ======

ShaderNode::ShaderNode(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
)
    : NodeInstance(instanceName, nodeType, device)
{
    memset(shaderStages, 0, sizeof(shaderStages));
}

ShaderNode::~ShaderNode() {
    Cleanup();
}

void ShaderNode::Setup() {
#ifdef AUTO_COMPILE_GLSL_TO_SPV
    // Initialize glslang (only once per process)
    static bool glslangInitialized = false;
    if (!glslangInitialized) {
        glslang::InitializeProcess();
        glslangInitialized = true;
    }
#endif
}

void ShaderNode::Compile() {
    // Get parameters
    std::string vertexPath = GetParameterValue<std::string>("vertexShaderPath", "");
    std::string fragmentPath = GetParameterValue<std::string>("fragmentShaderPath", "");

    if (vertexPath.empty() || fragmentPath.empty()) {
        throw std::runtime_error("ShaderNode: vertexShaderPath and fragmentShaderPath are required");
    }

#ifdef AUTO_COMPILE_GLSL_TO_SPV
    bool autoCompile = GetParameterValue<bool>("autoCompile", true);
#else
    bool autoCompile = false;
#endif

    if (autoCompile) {
#ifdef AUTO_COMPILE_GLSL_TO_SPV
        // Load GLSL source and compile to SPIR-V
        size_t vertSize, fragSize;
        void* vertSource = ReadShaderFile(vertexPath.c_str(), &vertSize);
        void* fragSource = ReadShaderFile(fragmentPath.c_str(), &fragSize);

        if (!vertSource || !fragSource) {
            free(vertSource);
            free(fragSource);
            throw std::runtime_error("Failed to load shader source files");
        }

        std::vector<uint32_t> vertSpirv, fragSpirv;
        
        bool vertSuccess = CompileGLSLToSPV(
            VK_SHADER_STAGE_VERTEX_BIT,
            static_cast<const char*>(vertSource),
            vertSpirv
        );
        
        bool fragSuccess = CompileGLSLToSPV(
            VK_SHADER_STAGE_FRAGMENT_BIT,
            static_cast<const char*>(fragSource),
            fragSpirv
        );

        free(vertSource);
        free(fragSource);

        if (!vertSuccess || !fragSuccess) {
            throw std::runtime_error("Failed to compile shaders to SPIR-V");
        }

        // Create shader modules
        CreateShaderModule(vertSpirv.data(), vertSpirv.size() * sizeof(uint32_t), &vertexShaderModule);
        CreateShaderModule(fragSpirv.data(), fragSpirv.size() * sizeof(uint32_t), &fragmentShaderModule);
#else
        throw std::runtime_error("GLSL compilation not enabled (AUTO_COMPILE_GLSL_TO_SPV not defined)");
#endif
    } else {
        // Load pre-compiled SPIR-V
        size_t vertSize, fragSize;
        void* vertSpirv = ReadShaderFile(vertexPath.c_str(), &vertSize);
        void* fragSpirv = ReadShaderFile(fragmentPath.c_str(), &fragSize);

        if (!vertSpirv || !fragSpirv) {
            free(vertSpirv);
            free(fragSpirv);
            throw std::runtime_error("Failed to load SPIR-V files");
        }

        CreateShaderModule(static_cast<const uint32_t*>(vertSpirv), vertSize, &vertexShaderModule);
        CreateShaderModule(static_cast<const uint32_t*>(fragSpirv), fragSize, &fragmentShaderModule);

        free(vertSpirv);
        free(fragSpirv);
    }

    // Setup shader stage create info
    shaderStages[0] = {};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertexShaderModule;
    shaderStages[0].pName = "main";

    shaderStages[1] = {};
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragmentShaderModule;
    shaderStages[1].pName = "main";

    stageCount = 2;
}

void ShaderNode::Execute(VkCommandBuffer commandBuffer) {
    // No-op - shaders are compiled in Compile phase
}

void ShaderNode::Cleanup() {
    VkDevice vkDevice = device->device;

    if (vertexShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vkDevice, vertexShaderModule, nullptr);
        vertexShaderModule = VK_NULL_HANDLE;
    }

    if (fragmentShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vkDevice, fragmentShaderModule, nullptr);
        fragmentShaderModule = VK_NULL_HANDLE;
    }

    stageCount = 0;
    memset(shaderStages, 0, sizeof(shaderStages));

#ifdef AUTO_COMPILE_GLSL_TO_SPV
    // Note: glslang::FinalizeProcess() should only be called once at program exit
#endif
}

void* ShaderNode::ReadShaderFile(const char* filename, size_t* fileSize) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << filename << std::endl;
        return nullptr;
    }

    *fileSize = static_cast<size_t>(file.tellg());
    char* buffer = static_cast<char*>(malloc(*fileSize));

    file.seekg(0);
    file.read(buffer, *fileSize);
    file.close();

    return buffer;
}

void ShaderNode::CreateShaderModule(const uint32_t* code, size_t codeSize, VkShaderModule* shaderModule) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = codeSize;
    createInfo.pCode = code;

    VkResult result = vkCreateShaderModule(device->device, &createInfo, nullptr, shaderModule);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }
}

#ifdef AUTO_COMPILE_GLSL_TO_SPV
bool ShaderNode::CompileGLSLToSPV(
    VkShaderStageFlagBits shaderType,
    const char* glslSource,
    std::vector<uint32_t>& spirv
) {
    EShLanguage stage;
    switch (shaderType) {
        case VK_SHADER_STAGE_VERTEX_BIT:
            stage = EShLangVertex;
            break;
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            stage = EShLangFragment;
            break;
        case VK_SHADER_STAGE_GEOMETRY_BIT:
            stage = EShLangGeometry;
            break;
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
            stage = EShLangTessControl;
            break;
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
            stage = EShLangTessEvaluation;
            break;
        case VK_SHADER_STAGE_COMPUTE_BIT:
            stage = EShLangCompute;
            break;
        default:
            return false;
    }

    glslang::TShader shader(stage);
    shader.setStrings(&glslSource, 1);

    // Setup environment
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    // Parse
    const TBuiltInResource* resources = GetDefaultResources();
    if (!shader.parse(resources, 100, false, EShMsgDefault)) {
        std::cerr << "GLSL Parsing Failed:" << std::endl;
        std::cerr << shader.getInfoLog() << std::endl;
        std::cerr << shader.getInfoDebugLog() << std::endl;
        return false;
    }

    // Link
    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(EShMsgDefault)) {
        std::cerr << "GLSL Linking Failed:" << std::endl;
        std::cerr << program.getInfoLog() << std::endl;
        std::cerr << program.getInfoDebugLog() << std::endl;
        return false;
    }

    // Convert to SPIR-V
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);

    return true;
}
#endif

} // namespace Vixen::RenderGraph
