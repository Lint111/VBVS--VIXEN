#include "VulkanShader.h"

void VulkanShader::BuildShaderModuleWithSPV(uint32_t *vertShaderText, size_t vertexSPVSize, uint32_t *fragShaderText, size_t fragSPVSize)
{
    VulkanDevice* deviceObj = VulkanApplication::GetInstance()-> deviceObj;

    VkResult result;
    stagesCount = 0;

    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].pNext = NULL;
    shaderStages[0].pSpecializationInfo = NULL;
    shaderStages[0].flags = 0;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].pName = "main";

    VkShaderModuleCreateInfo moduleCreateInfo = {};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = NULL;
    moduleCreateInfo.codeSize = vertexSPVSize;
    moduleCreateInfo.pCode = vertShaderText;
    result = vkCreateShaderModule(
        deviceObj->device,
        &moduleCreateInfo,
        NULL,
        &shaderStages[0].module
    );
    stagesCount++;
    assert(result == VK_SUCCESS);

    std::vector<unsigned int> fragSPV;
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].pNext = NULL;
    shaderStages[1].pSpecializationInfo = NULL;
    shaderStages[1].flags = 0;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].pName = "main";

    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = NULL;
    moduleCreateInfo.flags = 0;
    moduleCreateInfo.codeSize = fragSPVSize;
    moduleCreateInfo.pCode = fragShaderText;
    result = vkCreateShaderModule(
        deviceObj->device,
        &moduleCreateInfo,
        NULL,
        &shaderStages[1].module
    );
    stagesCount++;
    assert(result == VK_SUCCESS);
}

void VulkanShader::DestroyShader()
{
    VulkanApplication* appObj = VulkanApplication::GetInstance();
    VulkanDevice* deviceObj = appObj->deviceObj;    

    if(shaderStages[0].module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(
            deviceObj->device,
            shaderStages[0].module,
            NULL
        );
        shaderStages[0].module = VK_NULL_HANDLE;
    }
    if(shaderStages[1].module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(
            deviceObj->device,
            shaderStages[1].module,
            NULL
        );
        shaderStages[1].module = VK_NULL_HANDLE;
    }
    initialized = false;
    stagesCount = 0;
}

#ifdef AUTO_COMPILE_GLSL_TO_SPV

void VulkanShader::BuildShader(const char *vertShaderText, const char *fragShaderText)
{
    VulkanDevice* deviceObj = VulkanApplication::GetInstance()->deviceObj;

    VkResult result;
    bool retVal;
    stagesCount = 0;

    std::vector<unsigned int> vertexSPV;
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].pNext = NULL;
    shaderStages[0].pSpecializationInfo = NULL;
    shaderStages[0].flags = 0;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].pName = "main";

    glslang::InitializeProcess();    

    retVal = GLSLtoSPV(VK_SHADER_STAGE_VERTEX_BIT, vertShaderText, vertexSPV);
    assert(retVal);

    VkShaderModuleCreateInfo moduleCreateInfo = {};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = NULL;
    moduleCreateInfo.codeSize = vertexSPV.size() * sizeof(unsigned int);
    moduleCreateInfo.pCode = vertexSPV.data();

    result = vkCreateShaderModule(
        deviceObj->device,
        &moduleCreateInfo,
        NULL,
        &shaderStages[0].module
    );
    stagesCount++;

    assert(result == VK_SUCCESS);

    std::vector<unsigned int> fragSPV;
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].pNext = NULL;
    shaderStages[1].pSpecializationInfo = NULL;
    shaderStages[1].flags = 0;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].pName = "main";

    retVal = GLSLtoSPV(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderText, fragSPV);
    assert(retVal);

    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = NULL;
    moduleCreateInfo.flags = 0;
    moduleCreateInfo.codeSize = fragSPV.size() * sizeof(unsigned int);
    moduleCreateInfo.pCode = fragSPV.data();

    result = vkCreateShaderModule(
        deviceObj->device,
        &moduleCreateInfo,
        NULL,
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