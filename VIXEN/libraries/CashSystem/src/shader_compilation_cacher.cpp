#include "pch.h"
#include "ShaderCompilationCacher.h"
#include "CacheKeyHasher.h"
#include "VixenHash.h"
#include "ShaderCompiler.h"
#include "ShaderPreprocessor.h"
#include <fstream>
#include <functional>

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
    // Use CacheKeyHasher for deterministic, binary hashing
    CacheKeyHasher hasher;
    hasher.Add(ci.sourcePath)
          .Add(ci.entryPoint)
          .Add(ci.sourceChecksum)
          .Add(ci.compilerVersion);

    hasher.Add(static_cast<uint32_t>(ci.macroDefinitions.size()));
    for (const auto& macro : ci.macroDefinitions) {
        hasher.Add(macro);
    }

    hasher.Add(static_cast<uint32_t>(ci.compileFlags.size()));
    for (const auto& flag : ci.compileFlags) {
        hasher.Add(flag);
    }

    hasher.Add(static_cast<uint32_t>(ci.stage));

    return hasher.Finalize();
}

bool ShaderCompilationCacher::SerializeToFile(const std::filesystem::path& path) const {
    // Serialize device-independent SPIR-V bytecode and metadata

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("SerializeToFile: Failed to open file: " + path.string());
        return false;
    }

    // Write version header
    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Write number of cached shaders
    std::shared_lock lock(m_lock);
    uint32_t cacheSize = static_cast<uint32_t>(m_entries.size());
    file.write(reinterpret_cast<const char*>(&cacheSize), sizeof(cacheSize));

    // Serialize each compiled shader
    for (const auto& [key, entry] : m_entries) {
        const auto& wrapper = entry.resource;

        // Write source path
        uint32_t pathLen = static_cast<uint32_t>(wrapper->sourcePath.size());
        file.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
        file.write(wrapper->sourcePath.data(), pathLen);

        // Write entry point
        uint32_t entryLen = static_cast<uint32_t>(wrapper->entryPoint.size());
        file.write(reinterpret_cast<const char*>(&entryLen), sizeof(entryLen));
        file.write(wrapper->entryPoint.data(), entryLen);

        // Write stage
        file.write(reinterpret_cast<const char*>(&wrapper->stage), sizeof(wrapper->stage));

        // Write compiler version
        uint32_t versionLen = static_cast<uint32_t>(wrapper->compilerVersion.size());
        file.write(reinterpret_cast<const char*>(&versionLen), sizeof(versionLen));
        file.write(wrapper->compilerVersion.data(), versionLen);

        // Write macro definitions
        uint32_t macroCount = static_cast<uint32_t>(wrapper->macroDefinitions.size());
        file.write(reinterpret_cast<const char*>(&macroCount), sizeof(macroCount));
        for (const auto& macro : wrapper->macroDefinitions) {
            uint32_t macroLen = static_cast<uint32_t>(macro.size());
            file.write(reinterpret_cast<const char*>(&macroLen), sizeof(macroLen));
            file.write(macro.data(), macroLen);
        }

        // Write compile flags
        uint32_t flagCount = static_cast<uint32_t>(wrapper->compileFlags.size());
        file.write(reinterpret_cast<const char*>(&flagCount), sizeof(flagCount));
        for (const auto& flag : wrapper->compileFlags) {
            uint32_t flagLen = static_cast<uint32_t>(flag.size());
            file.write(reinterpret_cast<const char*>(&flagLen), sizeof(flagLen));
            file.write(flag.data(), flagLen);
        }

        // Write SPIR-V bytecode (key benefit - avoid recompilation)
        uint32_t spirvSize = static_cast<uint32_t>(wrapper->spirvCode.size());
        file.write(reinterpret_cast<const char*>(&spirvSize), sizeof(spirvSize));
        if (spirvSize > 0) {
            file.write(reinterpret_cast<const char*>(wrapper->spirvCode.data()), spirvSize * sizeof(uint32_t));
        }
    }

    LOG_INFO("SerializeToFile: Serialized " + std::to_string(cacheSize) + " compiled shaders to " + path.string());
    return true;
}

bool ShaderCompilationCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // Deserialize device-independent SPIR-V bytecode and metadata
    // Compiled SPIR-V can be used directly without recompilation

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("DeserializeFromFile: Failed to open file: " + path.string());
        return false;
    }

    // Read version header
    uint32_t version = 0;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) {
        LOG_ERROR("DeserializeFromFile: Unsupported version: " + std::to_string(version));
        return false;
    }

    // Read number of cached shaders
    uint32_t cacheSize = 0;
    file.read(reinterpret_cast<char*>(&cacheSize), sizeof(cacheSize));

    LOG_INFO("DeserializeFromFile: Loading " + std::to_string(cacheSize) + " compiled shaders from " + path.string());

    // Deserialize each compiled shader
    for (uint32_t i = 0; i < cacheSize; ++i) {
        auto wrapper = std::make_shared<CompiledShaderWrapper>();

        // Read source path
        uint32_t pathLen = 0;
        file.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        wrapper->sourcePath.resize(pathLen);
        file.read(wrapper->sourcePath.data(), pathLen);

        // Read entry point
        uint32_t entryLen = 0;
        file.read(reinterpret_cast<char*>(&entryLen), sizeof(entryLen));
        wrapper->entryPoint.resize(entryLen);
        file.read(wrapper->entryPoint.data(), entryLen);

        // Read stage
        file.read(reinterpret_cast<char*>(&wrapper->stage), sizeof(wrapper->stage));

        // Read compiler version
        uint32_t versionLen = 0;
        file.read(reinterpret_cast<char*>(&versionLen), sizeof(versionLen));
        wrapper->compilerVersion.resize(versionLen);
        file.read(wrapper->compilerVersion.data(), versionLen);

        // Read macro definitions
        uint32_t macroCount = 0;
        file.read(reinterpret_cast<char*>(&macroCount), sizeof(macroCount));
        wrapper->macroDefinitions.resize(macroCount);
        for (uint32_t j = 0; j < macroCount; ++j) {
            uint32_t macroLen = 0;
            file.read(reinterpret_cast<char*>(&macroLen), sizeof(macroLen));
            wrapper->macroDefinitions[j].resize(macroLen);
            file.read(wrapper->macroDefinitions[j].data(), macroLen);
        }

        // Read compile flags
        uint32_t flagCount = 0;
        file.read(reinterpret_cast<char*>(&flagCount), sizeof(flagCount));
        wrapper->compileFlags.resize(flagCount);
        for (uint32_t j = 0; j < flagCount; ++j) {
            uint32_t flagLen = 0;
            file.read(reinterpret_cast<char*>(&flagLen), sizeof(flagLen));
            wrapper->compileFlags[j].resize(flagLen);
            file.read(wrapper->compileFlags[j].data(), flagLen);
        }

        // Read SPIR-V bytecode
        uint32_t spirvSize = 0;
        file.read(reinterpret_cast<char*>(&spirvSize), sizeof(spirvSize));
        wrapper->spirvCode.resize(spirvSize);
        if (spirvSize > 0) {
            file.read(reinterpret_cast<char*>(wrapper->spirvCode.data()), spirvSize * sizeof(uint32_t));
        }

        wrapper->shaderName = wrapper->sourcePath;

        // Compute key and insert into cache
        ShaderCompilationParams params;
        params.sourcePath = wrapper->sourcePath;
        params.entryPoint = wrapper->entryPoint;
        params.macroDefinitions = wrapper->macroDefinitions;
        params.stage = wrapper->stage;
        params.compilerVersion = wrapper->compilerVersion;
        params.compileFlags = wrapper->compileFlags;
        params.sourceChecksum = ComputeSourceChecksum(wrapper->sourcePath);

        uint64_t key = ComputeKey(params);

        // Insert into cache
        std::unique_lock lock(m_lock);
        CacheEntry entry;
        entry.key = key;
        entry.ci = params;
        entry.resource = wrapper;
        m_entries.emplace(key, std::move(entry));
    }

    LOG_INFO("DeserializeFromFile: Loaded " + std::to_string(cacheSize) + " compiled shaders");

    (void)device;  // Device not needed for device-independent SPIR-V
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
            LOG_ERROR("Unsupported shader stage: " + std::to_string(ci.stage));
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
        LOG_ERROR("Compilation failed for " + ci.sourcePath + ": " + result.GetFullLog());
        wrapper.spirvCode = {};
    }
}

} // namespace CashSystem
