#include "VulkanShader.h"
#include <set>
#include <sstream>
#include <iomanip>
#include <functional>

// ===== Constructor / Destructor =====

VulkanShader::VulkanShader() {
    memset(shaderStages, 0, sizeof(shaderStages));
    logger = std::make_shared<Logger>("VulkanShader");
}

VulkanShader::~VulkanShader() {
    DestroyShader();
}

// ===== Builder Pattern API =====

VulkanShader& VulkanShader::AddStage(VkShaderStageFlagBits stage, const std::string& source,
                                     const std::string& entryPoint) {
    std::lock_guard<std::mutex> lock(shaderMutex);

    ShaderStageInfo info;
    info.stage = stage;
    info.source = source;
    info.entryPoint = entryPoint;
    stages.push_back(info);

    if (logger) {
        logger->Info("Added shader stage: " + GetStageExtension(stage) + " with entry point: " + entryPoint);
    }

    return *this;
}

VulkanShader& VulkanShader::AddStageSPV(VkShaderStageFlagBits stage, const std::vector<uint32_t>& spirv,
                                        const std::string& entryPoint) {
    std::lock_guard<std::mutex> lock(shaderMutex);

    ShaderStageInfo info;
    info.stage = stage;
    info.spirv = spirv;
    info.entryPoint = entryPoint;
    stages.push_back(info);

    if (logger) {
        logger->Info("Added SPIR-V shader stage: " + GetStageExtension(stage));
    }

    return *this;
}

VulkanShader& VulkanShader::AddStageFromFile(VkShaderStageFlagBits stage, const std::string& filepath,
                                             const std::string& entryPoint) {
    std::lock_guard<std::mutex> lock(shaderMutex);

    if (!std::filesystem::exists(filepath)) {
        if (logger) {
            logger->Error("Shader file not found: " + filepath);
        }
        return *this;
    }

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        if (logger) {
            logger->Error("Failed to open shader file: " + filepath);
        }
        return *this;
    }

    size_t fileSize = file.tellg();
    file.seekg(0);

    ShaderStageInfo info;
    info.stage = stage;
    info.entryPoint = entryPoint;

    // Check if it's a SPIR-V file
    std::filesystem::path path(filepath);
    if (path.extension() == ".spv") {
        // Read as SPIR-V binary
        info.spirv.resize(fileSize / sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(info.spirv.data()), fileSize);
        if (logger) {
            logger->Info("Loaded SPIR-V file: " + filepath);
        }
    } else {
        // Read as GLSL source
        std::string source(fileSize, '\0');
        file.read(&source[0], fileSize);
        info.source = source;
        if (logger) {
            logger->Info("Loaded GLSL file: " + filepath);
        }
    }

    file.close();

    // Store file path for hot reloading
    stageFilePaths[stage] = filepath;
    fileModTimes[filepath] = std::filesystem::last_write_time(filepath);

    stages.push_back(info);
    return *this;
}

VulkanShader& VulkanShader::SetCompileOptions(const ShaderCompileOptions& options) {
    std::lock_guard<std::mutex> lock(shaderMutex);
    compileOptions = options;
    return *this;
}

VulkanShader& VulkanShader::AddDefine(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(shaderMutex);
    compileOptions.defines[name] = value;
    if (logger) {
        logger->Info("Added define: " + name + " = " + value);
    }
    return *this;
}

VulkanShader& VulkanShader::EnableCache(const std::string& path) {
    std::lock_guard<std::mutex> lock(shaderMutex);
    cachingEnabled = true;
    cachePath = path;

    // Create cache directory if it doesn't exist
    if (!std::filesystem::exists(cachePath)) {
        std::filesystem::create_directories(cachePath);
    }

    if (logger) {
        logger->Info("Shader caching enabled at: " + cachePath);
    }

    return *this;
}

bool VulkanShader::Build() {
    std::lock_guard<std::mutex> lock(shaderMutex);

    if (logger) {
        logger->Info("Building shader with " + std::to_string(stages.size()) + " stages");
    }

    VulkanDevice* deviceObj = VulkanApplication::GetInstance()->deviceObj.get();
    if (!deviceObj) {
        if (logger) {
            logger->Error("Failed to get Vulkan device");
        }
        return false;
    }

    // Clean up any existing shader modules
    for (uint32_t i = 0; i < stagesCount; ++i) {
        if (shaderStages[i].module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(deviceObj->device, shaderStages[i].module, nullptr);
            shaderStages[i].module = VK_NULL_HANDLE;
        }
    }

    stagesCount = 0;

#ifdef AUTO_COMPILE_GLSL_TO_SPV
    glslang::InitializeProcess();
#endif

    // Build each stage
    for (size_t i = 0; i < stages.size() && i < MAX_SHADER_STAGES; ++i) {
        ShaderStageInfo& stage = stages[i];

        // Compile GLSL to SPIR-V if source is provided
        if (!stage.source.empty() && stage.spirv.empty()) {
#ifdef AUTO_COMPILE_GLSL_TO_SPV
            std::string processedSource = PreprocessSource(stage.source, compileOptions);

            // Check cache first
            std::string cacheKey = GenerateCacheKey(processedSource, stage.stage, compileOptions);
            bool foundInCache = false;

            if (cachingEnabled) {
                foundInCache = LoadFromCache(cacheKey, stage.spirv);
                if (foundInCache && logger) {
                    logger->Info("Loaded shader from cache: " + GetStageExtension(stage.stage));
                }
            }

            if (!foundInCache) {
                if (!GLSLtoSPV(stage.stage, processedSource, stage.spirv, compileOptions)) {
                    if (logger) {
                        logger->Error("Failed to compile GLSL to SPIR-V for stage: " + GetStageExtension(stage.stage));
                    }
                    continue;
                }

                if (cachingEnabled) {
                    SaveToCache(cacheKey, stage.spirv);
                }
            }
#else
            if (logger) {
                logger->Error("GLSL compilation not enabled. Build with AUTO_COMPILE_GLSL_TO_SPV flag.");
            }
            continue;
#endif
        }

        // Create shader module
        if (!CreateShaderModule(stage.spirv, stage.module)) {
            if (logger) {
                logger->Error("Failed to create shader module for stage: " + GetStageExtension(stage.stage));
            }
            continue;
        }

        // Setup shader stage create info
        shaderStages[stagesCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[stagesCount].pNext = nullptr;
        shaderStages[stagesCount].flags = 0;
        shaderStages[stagesCount].stage = stage.stage;
        shaderStages[stagesCount].module = stage.module;
        shaderStages[stagesCount].pName = stage.entryPoint.c_str();
        shaderStages[stagesCount].pSpecializationInfo = stage.specializationInfo;

        stagesCount++;
    }

#ifdef AUTO_COMPILE_GLSL_TO_SPV
    glslang::FinalizeProcess();
#endif

    if (stagesCount == 0) {
        if (logger) {
            logger->Error("No shader stages were successfully built");
        }
        return false;
    }

    initialized = true;

    if (logger) {
        logger->Info("Shader built successfully with " + std::to_string(stagesCount) + " stages");
    }

    return true;
}

// ===== Legacy API =====

void VulkanShader::BuildShaderModuleWithSPV(uint32_t* vertShaderText, size_t vertexSPVSize,
                                            uint32_t* fragShaderText, size_t fragSPVSize) {
    VulkanDevice* deviceObj = VulkanApplication::GetInstance()->deviceObj.get();
    VkResult result;
    stagesCount = 0;

    // Vertex shader
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
    result = vkCreateShaderModule(deviceObj->device, &moduleCreateInfo, nullptr, &shaderStages[0].module);
    stagesCount++;
    assert(result == VK_SUCCESS);

    // Fragment shader
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
    result = vkCreateShaderModule(deviceObj->device, &moduleCreateInfo, nullptr, &shaderStages[1].module);
    stagesCount++;
    assert(result == VK_SUCCESS);

    initialized = true;
}

#ifdef AUTO_COMPILE_GLSL_TO_SPV
void VulkanShader::BuildShader(const char* vertShaderText, const char* fragShaderText) {
    VulkanDevice* deviceObj = VulkanApplication::GetInstance()->deviceObj.get();
    VkResult result;
    bool retVal;
    stagesCount = 0;

    glslang::InitializeProcess();

    // Vertex shader
    std::vector<unsigned int> vertexSPV;
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].pNext = nullptr;
    shaderStages[0].pSpecializationInfo = nullptr;
    shaderStages[0].flags = 0;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].pName = "main";

    retVal = GLSLtoSPV(VK_SHADER_STAGE_VERTEX_BIT, vertShaderText, vertexSPV, compileOptions);
    assert(retVal);

    VkShaderModuleCreateInfo moduleCreateInfo = {};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = nullptr;
    moduleCreateInfo.codeSize = vertexSPV.size() * sizeof(unsigned int);
    moduleCreateInfo.pCode = vertexSPV.data();
    result = vkCreateShaderModule(deviceObj->device, &moduleCreateInfo, nullptr, &shaderStages[0].module);
    stagesCount++;
    assert(result == VK_SUCCESS);

    // Fragment shader
    std::vector<unsigned int> fragSPV;
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].pNext = nullptr;
    shaderStages[1].pSpecializationInfo = nullptr;
    shaderStages[1].flags = 0;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].pName = "main";

    retVal = GLSLtoSPV(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderText, fragSPV, compileOptions);
    assert(retVal);

    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = nullptr;
    moduleCreateInfo.flags = 0;
    moduleCreateInfo.codeSize = fragSPV.size() * sizeof(unsigned int);
    moduleCreateInfo.pCode = fragSPV.data();
    result = vkCreateShaderModule(deviceObj->device, &moduleCreateInfo, nullptr, &shaderStages[1].module);
    stagesCount++;
    assert(result == VK_SUCCESS);

    glslang::FinalizeProcess();
    initialized = true;
}
#endif

void VulkanShader::DestroyShader() {
    std::lock_guard<std::mutex> lock(shaderMutex);

    VulkanApplication* appObj = VulkanApplication::GetInstance();
    VulkanDevice* deviceObj = appObj->deviceObj.get();

    for (uint32_t i = 0; i < stagesCount; ++i) {
        if (shaderStages[i].module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(deviceObj->device, shaderStages[i].module, nullptr);
            shaderStages[i].module = VK_NULL_HANDLE;
        }
    }

    // Also clean up stages info
    for (auto& stage : stages) {
        if (stage.module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(deviceObj->device, stage.module, nullptr);
            stage.module = VK_NULL_HANDLE;
        }
    }

    initialized = false;
    stagesCount = 0;
    stages.clear();

    if (logger) {
        logger->Info("Shader destroyed");
    }
}

// ===== Hot Reloading =====

bool VulkanShader::HotReload() {
    if (!HasSourceChanged()) {
        return false;
    }

    if (logger) {
        logger->Info("Source files changed, reloading shaders...");
    }

    // Reload files
    std::lock_guard<std::mutex> lock(shaderMutex);

    for (auto& [stage, filepath] : stageFilePaths) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            if (logger) {
                logger->Error("Failed to open file for hot reload: " + filepath);
            }
            continue;
        }

        size_t fileSize = file.tellg();
        file.seekg(0);

        // Find corresponding stage info
        for (auto& stageInfo : stages) {
            if (stageInfo.stage == stage) {
                std::filesystem::path path(filepath);
                if (path.extension() == ".spv") {
                    stageInfo.spirv.resize(fileSize / sizeof(uint32_t));
                    file.read(reinterpret_cast<char*>(stageInfo.spirv.data()), fileSize);
                } else {
                    stageInfo.source.resize(fileSize);
                    file.read(&stageInfo.source[0], fileSize);
                    stageInfo.spirv.clear(); // Force recompilation
                }

                fileModTimes[filepath] = std::filesystem::last_write_time(filepath);
                break;
            }
        }

        file.close();
    }

    // Rebuild shaders
    return Build();
}

bool VulkanShader::HasSourceChanged() const {
    std::lock_guard<std::mutex> lock(shaderMutex);

    for (const auto& [filepath, modTime] : fileModTimes) {
        if (!std::filesystem::exists(filepath)) {
            continue;
        }

        auto currentModTime = std::filesystem::last_write_time(filepath);
        if (currentModTime != modTime) {
            return true;
        }
    }

    return false;
}

// ===== Shader Reflection =====

bool VulkanShader::ReflectShader() {
    // Note: Full SPIR-V reflection would require SPIRV-Reflect library
    // This is a placeholder for basic reflection functionality
    // For production use, integrate SPIRV-Reflect or similar library

    if (logger) {
        logger->Warning("Shader reflection is not fully implemented. Consider integrating SPIRV-Reflect library.");
    }

    return false;
}

// ===== Internal Methods =====

#ifdef AUTO_COMPILE_GLSL_TO_SPV

bool VulkanShader::GLSLtoSPV(VkShaderStageFlagBits stage, const std::string& source,
                             std::vector<uint32_t>& spirv, const ShaderCompileOptions& options) {
    glslang::TProgram* program = new glslang::TProgram;
    const char* shaderStrings[1];
    TBuiltInResource Resources;
    InitializeResources(Resources);

    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
    if (options.enableDebugInfo) {
        messages = (EShMessages)(messages | EShMsgDebugInfo);
    }

    EShLanguage language = GetLanguage(stage);
    glslang::TShader* shader = new glslang::TShader(language);

    shaderStrings[0] = source.c_str();
    shader->setStrings(shaderStrings, 1);
    shader->setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100);
    shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

    if (!shader->parse(&Resources, 450, false, messages)) {
        if (logger) {
            logger->Error("Shader parsing failed:");
            logger->Error(shader->getInfoLog());
            logger->Error(shader->getInfoDebugLog());
        } else {
            puts(shader->getInfoLog());
            puts(shader->getInfoDebugLog());
        }
        delete shader;
        delete program;
        return false;
    }

    program->addShader(shader);

    if (!program->link(messages)) {
        if (logger) {
            logger->Error("Shader linking failed:");
            logger->Error(program->getInfoLog());
            logger->Error(program->getInfoDebugLog());
        } else {
            puts(program->getInfoLog());
            puts(program->getInfoDebugLog());
        }
        delete shader;
        delete program;
        return false;
    }

    glslang::SpvOptions spvOptions;
    spvOptions.generateDebugInfo = options.enableDebugInfo;
    spvOptions.disableOptimizer = !options.enableOptimization;
    spvOptions.optimizeSize = false;

    glslang::GlslangToSpv(*program->getIntermediate(language), spirv, &spvOptions);

    delete shader;
    delete program;
    return true;
}

std::string VulkanShader::PreprocessSource(const std::string& source, const ShaderCompileOptions& options) {
    std::string result = source;

    // Add defines at the beginning (after #version if present)
    std::ostringstream definesStream;
    for (const auto& [name, value] : options.defines) {
        definesStream << "#define " << name << " " << value << "\n";
    }

    std::string defines = definesStream.str();
    if (!defines.empty()) {
        // Find #version directive
        size_t versionPos = result.find("#version");
        if (versionPos != std::string::npos) {
            // Insert after #version line
            size_t lineEnd = result.find('\n', versionPos);
            if (lineEnd != std::string::npos) {
                result.insert(lineEnd + 1, defines);
            }
        } else {
            // Insert at beginning
            result.insert(0, defines);
        }
    }

    // Resolve includes if any
    if (!options.includePaths.empty()) {
        std::set<std::string> includeGuard;
        result = ResolveIncludes(result, "", options.includePaths, includeGuard);
    }

    return result;
}

std::string VulkanShader::ResolveIncludes(const std::string& source, const std::string& currentPath,
                                         const std::vector<std::string>& includePaths,
                                         std::set<std::string>& includeGuard) {
    std::string result;
    std::istringstream stream(source);
    std::string line;

    while (std::getline(stream, line)) {
        // Check for #include directive
        size_t includePos = line.find("#include");
        if (includePos != std::string::npos) {
            // Extract filename
            size_t quoteStart = line.find('"', includePos);
            size_t quoteEnd = line.find('"', quoteStart + 1);

            if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                std::string includeFile = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

                // Check if already included
                if (includeGuard.find(includeFile) != includeGuard.end()) {
                    continue;
                }
                includeGuard.insert(includeFile);

                // Try to find and load the include file
                std::filesystem::path includePath;
                bool found = false;

                // Try relative to current file
                if (!currentPath.empty()) {
                    std::filesystem::path currentDir = std::filesystem::path(currentPath).parent_path();
                    includePath = currentDir / includeFile;
                    if (std::filesystem::exists(includePath)) {
                        found = true;
                    }
                }

                // Try include paths
                if (!found) {
                    for (const auto& searchPath : includePaths) {
                        includePath = std::filesystem::path(searchPath) / includeFile;
                        if (std::filesystem::exists(includePath)) {
                            found = true;
                            break;
                        }
                    }
                }

                if (found) {
                    std::ifstream includeFileStream(includePath);
                    if (includeFileStream.is_open()) {
                        std::string includeContent((std::istreambuf_iterator<char>(includeFileStream)),
                                                  std::istreambuf_iterator<char>());
                        includeFileStream.close();

                        // Recursively resolve includes in the included file
                        std::string resolvedInclude = ResolveIncludes(includeContent,
                                                                     includePath.string(),
                                                                     includePaths,
                                                                     includeGuard);
                        result += resolvedInclude + "\n";
                    } else if (logger) {
                        logger->Warning("Failed to open include file: " + includePath.string());
                    }
                } else if (logger) {
                    logger->Warning("Include file not found: " + includeFile);
                }
            }
        } else {
            result += line + "\n";
        }
    }

    return result;
}

EShLanguage VulkanShader::GetLanguage(VkShaderStageFlagBits shaderType) {
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
            if (logger) {
                logger->Error("Unknown shader type specified: " + std::to_string(shaderType));
            } else {
                printf("Unknown shader type specified: %d. Exiting...\n", shaderType);
            }
            exit(1);
    }
}

void VulkanShader::InitializeResources(TBuiltInResource& Resources) {
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

#endif // AUTO_COMPILE_GLSL_TO_SPV

bool VulkanShader::CreateShaderModule(const std::vector<uint32_t>& spirv, VkShaderModule& module) {
    VulkanDevice* deviceObj = VulkanApplication::GetInstance()->deviceObj.get();

    VkShaderModuleCreateInfo moduleCreateInfo = {};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = nullptr;
    moduleCreateInfo.flags = 0;
    moduleCreateInfo.codeSize = spirv.size() * sizeof(uint32_t);
    moduleCreateInfo.pCode = spirv.data();

    VkResult result = vkCreateShaderModule(deviceObj->device, &moduleCreateInfo, nullptr, &module);

    if (result != VK_SUCCESS) {
        if (logger) {
            logger->Error("Failed to create shader module. VkResult: " + std::to_string(result));
        }
        return false;
    }

    return true;
}

// ===== Caching =====

bool VulkanShader::LoadFromCache(const std::string& cacheKey, std::vector<uint32_t>& spirv) {
    std::filesystem::path cachePath = std::filesystem::path(this->cachePath) / (cacheKey + ".spv");

    if (!std::filesystem::exists(cachePath)) {
        return false;
    }

    std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0);

    spirv.resize(fileSize / sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(spirv.data()), fileSize);
    file.close();

    return true;
}

void VulkanShader::SaveToCache(const std::string& cacheKey, const std::vector<uint32_t>& spirv) {
    std::filesystem::path cachePath = std::filesystem::path(this->cachePath) / (cacheKey + ".spv");

    std::ofstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        if (logger) {
            logger->Warning("Failed to save shader to cache: " + cachePath.string());
        }
        return;
    }

    file.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(uint32_t));
    file.close();
}

std::string VulkanShader::GenerateCacheKey(const std::string& source, VkShaderStageFlagBits stage,
                                          const ShaderCompileOptions& options) {
    // Simple hash function for cache key generation
    std::hash<std::string> hasher;

    std::ostringstream keyStream;
    keyStream << source;
    keyStream << static_cast<int>(stage);

    for (const auto& [name, value] : options.defines) {
        keyStream << name << value;
    }

    keyStream << options.entryPoint;
    keyStream << options.enableOptimization;
    keyStream << options.enableDebugInfo;

    size_t hash = hasher(keyStream.str());

    std::ostringstream hashStr;
    hashStr << std::hex << std::setfill('0') << std::setw(16) << hash;

    return hashStr.str();
}

std::string VulkanShader::GetStageExtension(VkShaderStageFlagBits stage) const {
    switch (stage) {
        case VK_SHADER_STAGE_VERTEX_BIT: return "vert";
        case VK_SHADER_STAGE_FRAGMENT_BIT: return "frag";
        case VK_SHADER_STAGE_GEOMETRY_BIT: return "geom";
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return "tesc";
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return "tese";
        case VK_SHADER_STAGE_COMPUTE_BIT: return "comp";
        default: return "unknown";
    }
}
