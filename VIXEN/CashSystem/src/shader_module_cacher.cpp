#include "CashSystem/ShaderModuleCacher.h"
#include "VulkanResources/VulkanDevice.h"
#include "Hash.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>
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

    // Load/compile SPIR-V bytecode
    CompileShader(ci, *wrapper);

    // Create VkShaderModule from SPIR-V
    if (!wrapper->spirvCode.empty() && GetDevice()) {
        VkShaderModuleCreateInfo moduleCreateInfo{};
        moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleCreateInfo.codeSize = wrapper->spirvCode.size() * sizeof(uint32_t);
        moduleCreateInfo.pCode = wrapper->spirvCode.data();

        VkResult result = vkCreateShaderModule(
            GetDevice()->device,
            &moduleCreateInfo,
            nullptr,
            &wrapper->shaderModule
        );

        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module: " + wrapper->shaderName);
        }
    }

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
    // For now, load precompiled SPIR-V file

    // Try to load SPIR-V file
    std::string spirvPath = ci.sourcePath;

    // If source path doesn't end with .spv, assume it's source and look for .spv
    if (spirvPath.find(".spv") == std::string::npos) {
        size_t lastDot = spirvPath.find_last_of('.');
        if (lastDot != std::string::npos) {
            spirvPath = spirvPath.substr(0, lastDot) + ".spv";
        } else {
            spirvPath += ".spv";
        }
    }

    try {
        std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t fileSize = static_cast<size_t>(file.tellg());
            file.seekg(0);

            // Read as bytes and convert to uint32_t
            std::vector<char> buffer(fileSize);
            file.read(buffer.data(), fileSize);

            // SPIR-V must be aligned to uint32_t
            wrapper.spirvCode.resize(fileSize / sizeof(uint32_t));
            std::memcpy(wrapper.spirvCode.data(), buffer.data(), fileSize);
        } else {
            throw std::runtime_error("Failed to open SPIR-V file: " + spirvPath);
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Shader compilation failed for " + ci.shaderName + ": " + e.what());
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