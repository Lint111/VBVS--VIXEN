#include "ShaderManagement/ShaderCompiler.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/SPIRV/disassemble.h>
#include <spirv-tools/libspirv.hpp>
#include <fstream>
#include <sstream>
#include <chrono>
#include <mutex>

namespace ShaderManagement {

// Initialize glslang process (global, once per process)
static bool s_glslangInitialized = false;
static std::mutex s_initMutex;

ShaderCompiler::ShaderCompiler() {
    InitializeGlslang();
}

ShaderCompiler::~ShaderCompiler() {
    FinalizeGlslang();
}

void ShaderCompiler::InitializeGlslang() {
    std::lock_guard<std::mutex> lock(s_initMutex);
    if (!s_glslangInitialized) {
        glslang::InitializeProcess();
        s_glslangInitialized = true;
        initialized = true;
    }
}

void ShaderCompiler::FinalizeGlslang() {
    // Note: We don't call glslang::FinalizeProcess() because it's shared across all instances
    // and may be used by other ShaderCompiler instances
    initialized = false;
}

// Convert ShaderStage to glslang EShLanguage
static EShLanguage GetGlslangStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:       return EShLangVertex;
        case ShaderStage::Fragment:     return EShLangFragment;
        case ShaderStage::Compute:      return EShLangCompute;
        case ShaderStage::Geometry:     return EShLangGeometry;
        case ShaderStage::TessControl:  return EShLangTessControl;
        case ShaderStage::TessEval:     return EShLangTessEvaluation;
        case ShaderStage::Mesh:         return EShLangMesh;
        case ShaderStage::Task:         return EShLangTask;
        case ShaderStage::RayGen:       return EShLangRayGen;
        case ShaderStage::Miss:         return EShLangMiss;
        case ShaderStage::ClosestHit:   return EShLangClosestHit;
        case ShaderStage::AnyHit:       return EShLangAnyHit;
        case ShaderStage::Intersection: return EShLangIntersect;
        case ShaderStage::Callable:     return EShLangCallable;
        default:                        return EShLangVertex;
    }
}

CompilationOutput ShaderCompiler::Compile(
    ShaderStage stage,
    const std::string& source,
    const std::string& entryPoint,
    const CompilationOptions& options)
{
    return CompileInternal(stage, source, "shader", entryPoint, options);
}

CompilationOutput ShaderCompiler::CompileFile(
    ShaderStage stage,
    const std::filesystem::path& filePath,
    const std::string& entryPoint,
    const CompilationOptions& options)
{
    // Read file
    std::ifstream file(filePath, std::ios::in);
    if (!file.is_open()) {
        CompilationOutput output;
        output.success = false;
        output.errorLog = "Failed to open file: " + filePath.string();
        return output;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    return CompileInternal(stage, source, filePath.string(), entryPoint, options);
}

CompilationOutput ShaderCompiler::CompileInternal(
    ShaderStage stage,
    const std::string& source,
    const std::string& sourceName,
    const std::string& entryPoint,
    const CompilationOptions& options)
{
    auto startTime = std::chrono::steady_clock::now();

    CompilationOutput output;
    EShLanguage shaderStage = GetGlslangStage(stage);

    // Create shader object
    glslang::TShader shader(shaderStage);

    // Set source
    const char* sourceStr = source.c_str();
    const char* nameStr = sourceName.c_str();
    shader.setStringsWithLengthsAndNames(&sourceStr, nullptr, &nameStr, 1);

    // Set entry point
    shader.setEntryPoint(entryPoint.c_str());
    shader.setSourceEntryPoint(entryPoint.c_str());

    // Configure environment
    int vulkanVersion = options.targetVulkanVersion;
    shader.setEnvInput(glslang::EShSourceGlsl, shaderStage, glslang::EShClientVulkan, vulkanVersion);
    shader.setEnvClient(glslang::EShClientVulkan, static_cast<glslang::EShTargetClientVersion>(vulkanVersion));
    shader.setEnvTarget(glslang::EShTargetSpv, static_cast<glslang::EShTargetLanguageVersion>(options.targetSpirvVersion));

    // Set resource limits
    TBuiltInResource resources = *GetDefaultResources();

    // Compilation messages
    EShMessages messages = static_cast<EShMessages>(
        EShMsgSpvRules | EShMsgVulkanRules
    );
    if (options.generateDebugInfo) {
        messages = static_cast<EShMessages>(messages | EShMsgDebugInfo);
    }

    // Parse
    if (!shader.parse(&resources, vulkanVersion, false, messages)) {
        output.success = false;
        output.errorLog = shader.getInfoLog();
        output.infoLog = shader.getInfoDebugLog();
        return output;
    }

    // Link program
    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(messages)) {
        output.success = false;
        output.errorLog = program.getInfoLog();
        output.infoLog = program.getInfoDebugLog();
        return output;
    }

    // Generate SPIR-V
    glslang::SpvOptions spvOptions;
    spvOptions.generateDebugInfo = options.generateDebugInfo;
    spvOptions.disableOptimizer = !options.optimizePerformance;
    spvOptions.optimizeSize = options.optimizeSize;
    spvOptions.validate = false;  // Disable glslang's internal validation - we'll use SPIRV-Tools instead

    std::vector<unsigned int> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(shaderStage), spirv, &spvOptions);

    // Copy to output (unsigned int → uint32_t)
    output.spirv.assign(spirv.begin(), spirv.end());

    // Validate SPIR-V if requested
    if (options.validateSpirv) {
        std::string validationError;
        if (!ValidateSpirv(output.spirv, validationError)) {
            output.success = false;
            output.errorLog = "SPIR-V validation failed: " + validationError;
            return output;
        }
    }

    // Success
    output.success = true;
    output.infoLog = shader.getInfoLog();
    if (output.infoLog.empty()) {
        output.infoLog = program.getInfoLog();
    }

    auto endTime = std::chrono::steady_clock::now();
    output.compilationTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    return output;
}

CompilationOutput ShaderCompiler::LoadSpirv(
    const std::filesystem::path& filePath,
    bool validate)
{
    CompilationOutput output;

    // Open file
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        output.success = false;
        output.errorLog = "Failed to open SPIR-V file: " + filePath.string();
        return output;
    }

    // Get file size and read
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (fileSize % 4 != 0) {
        output.success = false;
        output.errorLog = "Invalid SPIR-V file size (not multiple of 4 bytes)";
        return output;
    }

    output.spirv.resize(fileSize / 4);
    file.read(reinterpret_cast<char*>(output.spirv.data()), fileSize);

    if (!file.good()) {
        output.success = false;
        output.errorLog = "Failed to read SPIR-V file";
        return output;
    }

    // Validate if requested
    if (validate) {
        std::string validationError;
        if (!ValidateSpirv(output.spirv, validationError)) {
            output.success = false;
            output.errorLog = "SPIR-V validation failed: " + validationError;
            return output;
        }
    }

    output.success = true;
    return output;
}

bool ShaderCompiler::ValidateSpirv(const std::vector<uint32_t>& spirv, std::string& outError) {
    if (spirv.empty()) {
        outError = "SPIR-V buffer is empty";
        return false;
    }

    // Use C++ API for validation - pass vector directly
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_2);
    tools.SetMessageConsumer([&outError](spv_message_level_t, const char*, const spv_position_t&, const char* message) {
        outError = message;
    });

    if (!tools.Validate(spirv)) {
        if (outError.empty()) {
            outError = "Unknown validation error";
        }
        return false;
    }

    return true;
}

std::string ShaderCompiler::DisassembleSpirv(const std::vector<uint32_t>& spirv) {
    std::stringstream ss;
    spv::Disassemble(ss, spirv);
    return ss.str();
}

bool ShaderCompiler::IsAvailable() {
    return true; // glslang is always available (static library)
}

std::string ShaderCompiler::GetVersion() {
    // glslang version constants may not be available in all SDK versions
    // Return a generic version string
    return "glslang-integrated";
}

std::optional<ShaderStage> InferStageFromPath(const std::filesystem::path& path) {
    std::string ext = path.extension().string();

    // Remove leading dot
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }

    if (ext == "vert") return ShaderStage::Vertex;
    if (ext == "frag") return ShaderStage::Fragment;
    if (ext == "comp") return ShaderStage::Compute;
    if (ext == "geom") return ShaderStage::Geometry;
    if (ext == "tesc") return ShaderStage::TessControl;
    if (ext == "tese") return ShaderStage::TessEval;
    if (ext == "mesh") return ShaderStage::Mesh;
    if (ext == "task") return ShaderStage::Task;
    if (ext == "rgen") return ShaderStage::RayGen;
    if (ext == "rmiss") return ShaderStage::Miss;
    if (ext == "rchit") return ShaderStage::ClosestHit;
    if (ext == "rahit") return ShaderStage::AnyHit;
    if (ext == "rint") return ShaderStage::Intersection;
    if (ext == "rcall") return ShaderStage::Callable;

    return std::nullopt;
}

} // namespace ShaderManagement
