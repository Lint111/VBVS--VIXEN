#include "CashSystem/ShaderCompilationCacher.h"
#include "VixenHash.h"
#include "ShaderManagement/ShaderCompiler.h"
#include "ShaderManagement/ShaderPreprocessor.h"
#include <fstream>
#include <functional>
#include <iostream>

using namespace Vixen::Hash;

namespace CashSystem {

std::shared_ptr<CompiledShaderWrapper> ShaderCompilationCacher::Create(const ShaderCompilationParams& ci) {
    auto wrapper = std::make_shared<CompiledShaderWrapper>();
    wrapper->sourcePath = ci.sourcePath;
    wrapper->entryPoint = ci.entryPoint;
    wrapper->macroDefinitions = ci.macroDefinitions;
    wrapper->stage = ci.stage;
    wrapper->compilerVersion = ci.compilerVersion;
    wrapper->compileFlags = ci.compileFlags;

    wrapper->shaderName = ci.sourcePath;

    // Compile shader using ShaderManagement
    CompileShader(ci, *wrapper);

    return wrapper;
}

std::uint64_t ShaderCompilationCacher::ComputeKey(const ShaderCompilationParams& ci) const {
    // Build key string from all compilation parameters
    std::string keyString = ci.sourcePath + "|" + ci.entryPoint + "|" + ci.sourceChecksum + "|" + ci.compilerVersion;

    for (const auto& macro : ci.macroDefinitions) {
        keyString += "|" + macro;
    }

    for (const auto& flag : ci.compileFlags) {
        keyString += "|" + flag;
    }

    keyString += "|" + std::to_string(static_cast<uint32_t>(ci.stage));

    // Use std::hash (good enough for now, deterministic)
    return std::hash<std::string>{}(keyString);
}

bool ShaderCompilationCacher::SerializeToFile(const std::filesystem::path& path) const {
    // TODO: Implement serialization
    return true;
}

bool ShaderCompilationCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // TODO: Implement deserialization
    return true;
}

std::string ShaderCompilationCacher::ComputeSourceChecksum(const std::string& sourcePath) const {
    try {
        std::ifstream file(sourcePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return "";
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            return "";
        }

        return ComputeSHA256Hex(buffer.data(), buffer.size());
    } catch (const std::exception&) {
        return "";
    }
}

void ShaderCompilationCacher::CompileShader(const ShaderCompilationParams& ci, CompiledShaderWrapper& wrapper) {
    // Convert VkShaderStageFlagBits to ShaderManagement::ShaderStage
    ShaderManagement::ShaderStage stage;
    switch (ci.stage) {
        case VK_SHADER_STAGE_VERTEX_BIT:                  stage = ShaderManagement::ShaderStage::Vertex; break;
        case VK_SHADER_STAGE_FRAGMENT_BIT:                stage = ShaderManagement::ShaderStage::Fragment; break;
        case VK_SHADER_STAGE_COMPUTE_BIT:                 stage = ShaderManagement::ShaderStage::Compute; break;
        case VK_SHADER_STAGE_GEOMETRY_BIT:                stage = ShaderManagement::ShaderStage::Geometry; break;
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:    stage = ShaderManagement::ShaderStage::TessControl; break;
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: stage = ShaderManagement::ShaderStage::TessEval; break;
        default:
            std::cerr << "[ShaderCompilationCacher] Unsupported shader stage: " << ci.stage << std::endl;
            wrapper.spirvCode = {};
            return;
    }

    // Set up compilation options
    ShaderManagement::CompilationOptions options;
    options.optimizePerformance = true;
    options.generateDebugInfo = false;
    options.targetVulkanVersion = 130;  // Vulkan 1.3
    options.targetSpirvVersion = 160;   // SPIR-V 1.6

    // Create compiler
    ShaderManagement::ShaderCompiler compiler;

    // Compile from file
    auto result = compiler.CompileFile(stage, ci.sourcePath, ci.entryPoint, options);

    if (result.success) {
        wrapper.spirvCode = std::move(result.spirv);
    } else {
        std::cerr << "[ShaderCompilationCacher] Compilation failed for " << ci.sourcePath << std::endl;
        std::cerr << result.GetFullLog() << std::endl;
        wrapper.spirvCode = {};
    }
}

} // namespace CashSystem
