#pragma once

#include "TypedCacher.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

namespace CashSystem {

// Wrapper for compiled shader modules (device-independent)
struct CompiledShaderWrapper {
    std::vector<uint32_t> spirvCode;
    std::string shaderName;
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
    
    // Cache key information
    std::string sourcePath;
    std::string entryPoint;
    std::vector<std::string> macroDefinitions;
    std::string compilerVersion;
    std::vector<std::string> compileFlags;
};

// Parameters for shader compilation
struct ShaderCompilationParams {
    std::string sourcePath;
    std::string entryPoint = "main";
    std::vector<std::string> macroDefinitions;
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
    std::string compilerVersion;
    std::vector<std::string> compileFlags;
    std::string sourceChecksum;
};

/**
 * @brief Device-independent shader compilation cacher
 * 
 * Caches compiled SPIR-V modules that can be used across all Vulkan devices.
 * Once compiled, the SPIR-V code is device-independent and can be reused.
 */
class ShaderCompilationCacher : public TypedCacher<CompiledShaderWrapper, ShaderCompilationParams> {
public:
    ShaderCompilationCacher() = default;
    ~ShaderCompilationCacher() override = default;

    /**
     * @brief Get or create compiled shader module
     */
    std::shared_ptr<CompiledShaderWrapper> GetOrCreateShaderModule(
        const std::string& sourcePath,
        const std::string& entryPoint = "main",
        const std::vector<std::string>& macros = {},
        VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT,
        const std::string& compilerVersion = "1.0",
        const std::vector<std::string>& compileFlags = {}
    ) {
        ShaderCompilationParams params;
        params.sourcePath = sourcePath;
        params.entryPoint = entryPoint;
        params.macroDefinitions = macros;
        params.stage = stage;
        params.compilerVersion = compilerVersion;
        params.compileFlags = compileFlags;
        params.sourceChecksum = ComputeSourceChecksum(sourcePath);

        return GetOrCreate(params);
    }

    // Serialization
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "ShaderCompilationCacher"; }

protected:
    // TypedCacher implementation
    std::shared_ptr<CompiledShaderWrapper> Create(const ShaderCompilationParams& ci) override;
    std::uint64_t ComputeKey(const ShaderCompilationParams& ci) const override;

private:
    // Helper methods
    std::string ComputeSourceChecksum(const std::string& sourcePath) const;
    void CompileShader(const ShaderCompilationParams& ci, CompiledShaderWrapper& wrapper);
    
    /**
     * @brief Override OnInitialize - device-independent cachers don't need device context
     */
    void OnInitialize() override {
        // No device initialization needed for device-independent compilation
    }
};

} // namespace CashSystem