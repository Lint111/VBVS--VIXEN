#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include "MainCacher.h"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

// Forward declarations for ShaderManagement types
namespace ShaderManagement {
    struct ShaderStageDefinition;
    struct CompiledProgram;
}

namespace CashSystem {

// Wrapper for compiled shader modules
struct ShaderModuleWrapper {
    VkShaderModule shaderModule = VK_NULL_HANDLE;  // Created Vulkan shader module
    std::vector<uint32_t> spirvCode;               // SPIR-V bytecode
    std::string shaderName;
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;

    // For cache key computation
    std::string sourcePath;
    std::string entryPoint;
    std::vector<std::string> macroDefinitions;
};

// Parameters for shader module creation
struct ShaderModuleCreateParams {
    std::string sourcePath;
    std::string entryPoint = "main";
    std::vector<std::string> macroDefinitions;
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
    std::string shaderName;
    
    // Hash of source file for quick validation
    std::string sourceChecksum;
};

/**
 * @brief TypedCacher for shader modules
 * 
 * Caches compiled SPIR-V modules based on:
 * - Source file path and content
 * - Entry point name
 * - Macro definitions
 * - Shader stage
 */
class ShaderModuleCacher : public TypedCacher<ShaderModuleWrapper, ShaderModuleCreateParams> {
public:
    ShaderModuleCacher() = default;
    ~ShaderModuleCacher() override = default;

    // Convenience API for shader library integration
    std::shared_ptr<ShaderModuleWrapper> GetOrCreateShaderModule(
        const std::string& sourcePath,
        const std::string& entryPoint = "main",
        const std::vector<std::string>& macros = {},
        VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT,
        const std::string& shaderName = ""
    );

protected:
    // TypedCacher implementation
    std::shared_ptr<ShaderModuleWrapper> Create(const ShaderModuleCreateParams& ci) override;
    std::uint64_t ComputeKey(const ShaderModuleCreateParams& ci) const override;

    // Serialization
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "ShaderModuleCacher"; }

private:
    // Helper methods
    std::string ComputeSourceChecksum(const std::string& sourcePath) const;
    void CompileShader(const ShaderModuleCreateParams& ci, ShaderModuleWrapper& wrapper);
};

} // namespace CashSystem