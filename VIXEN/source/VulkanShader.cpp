#include "Headers.h"
#include "VulkanShader.h"
#include "VulkanResources/VulkanDevice.h"

using namespace Vixen::Vulkan::Resources;

void VulkanShader::BuildShaderModuleWithSPV(uint32_t *vertShaderText, size_t vertexSPVSize, uint32_t *fragShaderText, size_t fragSPVSize, Vixen::Vulkan::Resources::VulkanDevice* deviceObj)
{

    VkResult result;
    stagesCount = 0;
    
    // Store device for later destruction
    creationDevice = deviceObj;

    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].pNext = nullptr;
    shaderStages[0].pSpecializationInfo = nullptr;
    shaderStages[0].flags = 0;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].pName = "main";

    VkShaderModuleCreateInfo moduleCreateInfo = {};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = nullptr;
    moduleCreateInfo.codeSize = vertexSPVSize;
    moduleCreateInfo.pCode = vertShaderText;
    result = vkCreateShaderModule(
        deviceObj->device,
        &moduleCreateInfo,
        nullptr,
        &shaderStages[0].module
    );
    stagesCount++;
    assert(result == VK_SUCCESS);

    std::vector<unsigned int> fragSPV;
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].pNext = nullptr;
    shaderStages[1].pSpecializationInfo = nullptr;
    shaderStages[1].flags = 0;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].pName = "main";

    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = nullptr;
    moduleCreateInfo.flags = 0;
    moduleCreateInfo.codeSize = fragSPVSize;
    moduleCreateInfo.pCode = fragShaderText;
    result = vkCreateShaderModule(
        deviceObj->device,
        &moduleCreateInfo,
        nullptr,
        &shaderStages[1].module
    );
    stagesCount++;
    assert(result == VK_SUCCESS);
    
    // Mark shader as initialized
    initialized = true;
}

void VulkanShader::DestroyShader(Vixen::Vulkan::Resources::VulkanDevice* deviceObj)
{
    std::cout << "[VulkanShader::DestroyShader] Called - initialized=" << initialized << std::endl;
    
    // Only destroy if initialized
    if (!initialized) {
        std::cout << "[VulkanShader::DestroyShader] Not initialized - skipping" << std::endl;
        return;
    }
    
    // CRITICAL: Use the device that created the shader modules
    // If no device provided, fall back to creationDevice
    VulkanDevice* actualDevice = (deviceObj != nullptr) ? deviceObj : creationDevice;
    
    if (actualDevice == nullptr) {
        std::cout << "[VulkanShader::DestroyShader] ERROR: No valid device - shader modules will leak!" << std::endl;
        // Cannot destroy without valid device - shader modules will leak
        return;
    }
    
    std::cout << "[VulkanShader::DestroyShader] Destroying shader modules..." << std::endl;
    
    if(shaderStages[0].module != VK_NULL_HANDLE) {
        std::cout << "[VulkanShader::DestroyShader] Destroying vertex shader module: " 
                  << shaderStages[0].module << std::endl;
        vkDestroyShaderModule(
            actualDevice->device,
            shaderStages[0].module,
            nullptr
        );
        shaderStages[0].module = VK_NULL_HANDLE;
    }
    if(shaderStages[1].module != VK_NULL_HANDLE) {
        std::cout << "[VulkanShader::DestroyShader] Destroying fragment shader module: " 
                  << shaderStages[1].module << std::endl;
        vkDestroyShaderModule(
            actualDevice->device,
            shaderStages[1].module,
            nullptr
        );
        shaderStages[1].module = VK_NULL_HANDLE;
    }
    
    std::cout << "[VulkanShader::DestroyShader] Cleanup complete" << std::endl;
    initialized = false;
    stagesCount = 0;
    creationDevice = nullptr;
}

#ifdef AUTO_COMPILE_GLSL_TO_SPV

void VulkanShader::BuildShader(const char *vertShaderText, const char *fragShaderText, Vixen::Vulkan::Resources::VulkanDevice* deviceObj)
{
    VkResult result;
    bool retVal;
    stagesCount = 0;

    std::vector<unsigned int> vertexSPV;
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].pNext = nullptr;
    shaderStages[0].pSpecializationInfo = nullptr;
    shaderStages[0].flags = 0;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].pName = "main";

    glslang::InitializeProcess();    

    retVal = GLSLtoSPV(VK_SHADER_STAGE_VERTEX_BIT, vertShaderText, vertexSPV);
    assert(retVal);

    VkShaderModuleCreateInfo moduleCreateInfo = {};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = nullptr;
    moduleCreateInfo.codeSize = vertexSPV.size() * sizeof(unsigned int);
    moduleCreateInfo.pCode = vertexSPV.data();

    result = vkCreateShaderModule(
        deviceObj->device,
        &moduleCreateInfo,
        nullptr,
        &shaderStages[0].module
    );
    stagesCount++;

    assert(result == VK_SUCCESS);

    std::vector<unsigned int> fragSPV;
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].pNext = nullptr;
    shaderStages[1].pSpecializationInfo = nullptr;
    shaderStages[1].flags = 0;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].pName = "main";

    retVal = GLSLtoSPV(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderText, fragSPV);
    assert(retVal);

    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = nullptr;
    moduleCreateInfo.flags = 0;
    moduleCreateInfo.codeSize = fragSPV.size() * sizeof(unsigned int);
    moduleCreateInfo.pCode = fragSPV.data();

    result = vkCreateShaderModule(
        deviceObj->device,
        &moduleCreateInfo,
        nullptr,
        &shaderStages[1].module
    );
    stagesCount++;

    assert(result == VK_SUCCESS);

    glslang::FinalizeProcess();
}


bool VulkanShader::GLSLtoSPV(const VkShaderStageFlagBits shaderType, const char *pShader, std::vector<unsigned int> &spirv)
{
    glslang::TProgram* program = new glslang::TProgram;
    const char* shaderStrings[1];
    TBuiltInResource Resources;
    InitializeResources(Resources);

    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

    EShLanguage stage = GetLanguage(shaderType);
    glslang::TShader* shader = new glslang::TShader(stage);

    shaderStrings[0] = pShader;
    shader->setStrings(shaderStrings, 1);

    if (!shader->parse(&Resources, 100, false, messages)) {
        puts(shader->getInfoLog());
        puts(shader->getInfoDebugLog());
        return false; // something didn't work
    }

    program->addShader(shader);

    if (!program->link(messages)) {
        puts(program->getInfoLog());
        puts(program->getInfoDebugLog());
        return false;
    }

    glslang::GlslangToSpv(*program->getIntermediate(stage), spirv);
    return true;
}

void VulkanShader::InitializeResources(TBuiltInResource &Resources)
{
    Resources.maxLights = 32;
    Resources.maxClipPlanes = 6;
    Resources.maxTextureUnits = 32;
    Resources.maxTextureCoords = 32;
    Resources.maxVertexAttribs = 64;
    Resources.maxVertexUniformComponents = 4096;
    Resources.maxVaryingFloats = 64;
    Resources.maxVertexTextureImageUnits = 32;
    Resources.maxCombinedTextureImageUnits = 80;
    Resources.maxTextureImageUnits = 32;
    Resources.maxFragmentUniformComponents = 4096;
    Resources.maxDrawBuffers = 32;
    Resources.maxVertexUniformVectors = 128;
    Resources.maxVaryingVectors = 8;
    Resources.maxFragmentUniformVectors = 16;
    Resources.maxVertexOutputVectors = 16;
    Resources.maxFragmentInputVectors = 15;
    Resources.minProgramTexelOffset = -8;
    Resources.maxProgramTexelOffset = 7;
    Resources.maxClipDistances = 8;
    Resources.maxComputeWorkGroupCountX = 65535;
    Resources.maxComputeWorkGroupCountY = 65535;
    Resources.maxComputeWorkGroupCountZ = 65535;
    Resources.maxComputeWorkGroupSizeX = 1024;
    Resources.maxComputeWorkGroupSizeY = 1024;
    Resources.maxComputeWorkGroupSizeZ = 64;
    Resources.maxComputeUniformComponents = 1024;
    Resources.maxComputeTextureImageUnits = 16;
    Resources.maxComputeImageUniforms = 8;
    Resources.maxComputeAtomicCounters = 8;
    Resources.maxComputeAtomicCounterBuffers = 1;
    Resources.maxVaryingComponents = 60;
    Resources.maxVertexOutputComponents = 64;
    Resources.maxGeometryInputComponents = 64;
    Resources.maxGeometryOutputComponents = 128;
    Resources.maxFragmentInputComponents = 128;
    Resources.maxImageUnits = 8;
    Resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
    Resources.maxCombinedShaderOutputResources = 8;
    Resources.maxImageSamples = 0;
    Resources.maxVertexImageUniforms = 0;
    Resources.maxTessControlImageUniforms = 0;
    Resources.maxTessEvaluationImageUniforms = 0;
    Resources.maxGeometryImageUniforms = 0;
    Resources.maxFragmentImageUniforms = 8;
    Resources.maxCombinedImageUniforms = 8;
    Resources.maxGeometryTextureImageUnits = 16;
    Resources.maxGeometryOutputVertices = 256;
    Resources.maxGeometryTotalOutputComponents = 1024;
    Resources.maxGeometryUniformComponents = 1024;
    Resources.maxGeometryVaryingComponents = 64;
    Resources.maxTessControlInputComponents = 128;
    Resources.maxTessControlOutputComponents = 128;
    Resources.maxTessControlTextureImageUnits = 16;
    Resources.maxTessControlUniformComponents = 1024;
    Resources.maxTessControlTotalOutputComponents = 4096;
    Resources.maxTessEvaluationInputComponents = 128;
    Resources.maxTessEvaluationOutputComponents = 128;
    Resources.maxTessEvaluationTextureImageUnits = 16;
    Resources.maxTessEvaluationUniformComponents = 1024;
    Resources.maxTessPatchComponents = 120;
    Resources.maxPatchVertices = 32;
    Resources.maxTessGenLevel = 64;
    Resources.maxViewports = 16;
    Resources.maxVertexAtomicCounters = 0;
    Resources.maxTessControlAtomicCounters = 0;
    Resources.maxTessEvaluationAtomicCounters = 0;
    Resources.maxGeometryAtomicCounters = 0;
    Resources.maxFragmentAtomicCounters = 8;
    Resources.maxCombinedAtomicCounters = 8;
    Resources.maxAtomicCounterBindings = 1;
    Resources.maxVertexAtomicCounterBuffers = 0;
    Resources.maxTessControlAtomicCounterBuffers = 0;
    Resources.maxTessEvaluationAtomicCounterBuffers = 0;
    Resources.maxGeometryAtomicCounterBuffers = 0;
    Resources.maxFragmentAtomicCounterBuffers = 1;
    Resources.maxCombinedAtomicCounterBuffers = 1;
    Resources.maxAtomicCounterBufferSize = 16384;
    Resources.maxTransformFeedbackBuffers = 4;
    Resources.maxTransformFeedbackInterleavedComponents = 64;
    Resources.maxCullDistances = 8;
    Resources.maxCombinedClipAndCullDistances = 8;
    Resources.maxSamples = 4;
    Resources.maxMeshOutputVerticesNV = 256;
    Resources.maxMeshOutputPrimitivesNV = 512;
    Resources.maxMeshWorkGroupSizeX_NV = 32;
    Resources.maxMeshWorkGroupSizeY_NV = 1;
    Resources.maxMeshWorkGroupSizeZ_NV = 1;
    Resources.maxTaskWorkGroupSizeX_NV = 32;
    Resources.maxTaskWorkGroupSizeY_NV = 1;
    Resources.maxTaskWorkGroupSizeZ_NV = 1;
    Resources.maxMeshViewCountNV = 4;
    Resources.limits.nonInductiveForLoops = 1;
    Resources.limits.whileLoops = 1;
    Resources.limits.doWhileLoops = 1;
    Resources.limits.generalUniformIndexing = 1;
    Resources.limits.generalAttributeMatrixVectorIndexing = 1;
    Resources.limits.generalVaryingIndexing = 1;
    Resources.limits.generalSamplerIndexing = 1;
    Resources.limits.generalVariableIndexing = 1;
    Resources.limits.generalConstantMatrixVectorIndexing = 1;
}

EShLanguage VulkanShader::GetLanguage(const VkShaderStageFlagBits shaderType)
{
    switch (shaderType) {
        case VK_SHADER_STAGE_VERTEX_BIT: return EShLangVertex;
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return EShLangTessControl;
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return EShLangTessEvaluation;
        case VK_SHADER_STAGE_GEOMETRY_BIT: return EShLangGeometry;
        case VK_SHADER_STAGE_FRAGMENT_BIT: return EShLangFragment;
        case VK_SHADER_STAGE_COMPUTE_BIT: return EShLangCompute;
        case VK_SHADER_STAGE_RAYGEN_BIT_KHR: return EShLangRayGen;
        case VK_SHADER_STAGE_ANY_HIT_BIT_KHR: return EShLangAnyHit;
        case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR: return EShLangClosestHit;
        case VK_SHADER_STAGE_MISS_BIT_KHR: return EShLangMiss;
        case VK_SHADER_STAGE_INTERSECTION_BIT_KHR: return EShLangIntersect;
        case VK_SHADER_STAGE_CALLABLE_BIT_KHR: return EShLangCallable;
        case VK_SHADER_STAGE_TASK_BIT_NV: return EShLangTask;
        case VK_SHADER_STAGE_MESH_BIT_NV: return EShLangMesh;
        default: 
            printf("Unknown shader type specified: %d. Exiting...\n", shaderType);
            exit(1);
    }
}

#endif // AUTO_COMPILE_GLSL_TO_SPV