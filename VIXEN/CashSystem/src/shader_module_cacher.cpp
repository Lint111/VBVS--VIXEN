#include "CashSystem/ShaderModuleCacher.h"
#include "Hash.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>

// Helper function to compute SHA256 - BEFORE including Hash to ensure proper namespace
inline std::string ComputeSourceChecksum_Helper_Impl(const void* data, size_t len) {
    // TODO: Fix Hash namespace issue - for now return empty string
    // return ::Vixen::Hash::ComputeSHA256Hex(data, len);
    return "";
}

// Helper function to compute SHA256 outside of namespace context
static std::string ComputeSourceChecksum_Helper(const std::string& sourcePath) {
    try {
        std::ifstream file(sourcePath, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});
        return ComputeSourceChecksum_Helper_Impl(buffer.data(), buffer.size());
    } catch (const std::exception&) {
        return "";
    }
}

namespace CashSystem {

std::shared_ptr<ShaderModuleWrapper> ShaderModuleCacher::GetOrCreateShaderModule(
    const std::string& sourcePath,
    const std::string& entryPoint,
    const std::vector<std::string>& macros,
    VkShaderStageFlagBits stage,
    const std::string& shaderName)
{
    ShaderModuleCreateParams params;
    params.sourcePath = sourcePath;
    params.entryPoint = entryPoint;
    params.macroDefinitions = macros;
    params.stage = stage;
    params.shaderName = shaderName.empty() ? sourcePath : shaderName;
    params.sourceChecksum = ComputeSourceChecksum(sourcePath);
    
    return GetOrCreate(params);
}

std::shared_ptr<ShaderModuleWrapper> ShaderModuleCacher::Create(const ShaderModuleCreateParams& ci) {
    auto wrapper = std::make_shared<ShaderModuleWrapper>();
    wrapper->shaderName = ci.shaderName;
    wrapper->stage = ci.stage;
    wrapper->sourcePath = ci.sourcePath;
    wrapper->entryPoint = ci.entryPoint;
    wrapper->macroDefinitions = ci.macroDefinitions;
    
    // Compile shader from source
    CompileShader(ci, *wrapper);
    
    return wrapper;
}

std::uint64_t ShaderModuleCacher::ComputeKey(const ShaderModuleCreateParams& ci) const {
    // Combine all parameters into a unique key
    std::ostringstream keyStream;
    keyStream << ci.sourcePath << "|"
              << ci.entryPoint << "|"
              << ci.stage << "|"
              << ci.sourceChecksum << "|";
    
    for (const auto& macro : ci.macroDefinitions) {
        keyStream << macro << ",";
    }
    
    // Use hash function to create 64-bit key
    const std::string keyString = keyStream.str();
    return std::hash<std::string>{}(keyString);
}

std::string ShaderModuleCacher::ComputeSourceChecksum(const std::string& sourcePath) const {
    return ComputeSourceChecksum_Helper(sourcePath);
}

void ShaderModuleCacher::CompileShader(const ShaderModuleCreateParams& ci, ShaderModuleWrapper& wrapper) {
    // TODO: Integrate with ShaderManagement library for compilation
    // For now, this is a placeholder that would integrate with glslang or similar
    
    // Placeholder: Load precompiled SPIR-V file (for MVP)
    std::string spirvPath = ci.sourcePath.substr(0, ci.sourcePath.find_last_of('.')) + ".spv";
    
    try {
        std::ifstream file(spirvPath, std::ios::binary);
        if (file.is_open()) {
            wrapper.spirvCode.assign(std::istreambuf_iterator<char>(file), {});
        } else {
            // Fallback: create empty SPIR-V for compilation testing
            wrapper.spirvCode = {0x03, 0x02, 0x01, 0x00}; // Minimal SPIR-V header
        }
    } catch (const std::exception&) {
        wrapper.spirvCode = {0x03, 0x02, 0x01, 0x00}; // Fallback SPIR-V
    }
}

bool ShaderModuleCacher::SerializeToFile(const std::filesystem::path& path) const {
    // TODO: Implement serialization of shader modules
    // For now, return true (no-op)
    (void)path;
    return true;
}

bool ShaderModuleCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // TODO: Implement deserialization of shader modules
    // For now, return true (no-op)
    (void)path;
    (void)device;
    return true;
}

} // namespace CashSystem