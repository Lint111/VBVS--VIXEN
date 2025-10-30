#include "CashSystem/ShaderModuleCacher.h"
#include "VulkanResources/VulkanDevice.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <vulkan/vulkan.h>
#include <iostream>
#include <shared_mutex>

// Helper function to compute basic checksum for file contents
// TODO: Restore SHA256 after resolving namespace conflict with stbrumme_hash
static std::string ComputeSourceChecksum_Helper(const std::string& sourcePath) {
    try {
        std::ifstream file(sourcePath, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        // Simple FNV-1a hash for now
        std::uint64_t hash = 14695981039346656037ULL;
        char byte;
        while (file.get(byte)) {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(byte));
            hash *= 1099511628211ULL;
        }
        std::ostringstream oss;
        oss << std::hex << hash;
        return oss.str();
    } catch (const std::exception&) {
        return "";
    }
}

namespace CashSystem {

std::shared_ptr<ShaderModuleWrapper> ShaderModuleCacher::GetOrCreate(const ShaderModuleCreateParams& ci) {
    auto key = ComputeKey(ci);

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            std::cout << "[ShaderModuleCacher::GetOrCreate] CACHE HIT for " << ci.shaderName
                      << " (key=" << key << ", VkShaderModule="
                      << reinterpret_cast<uint64_t>(it->second.resource->shaderModule) << ")" << std::endl;
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            std::cout << "[ShaderModuleCacher::GetOrCreate] CACHE PENDING for " << ci.shaderName
                      << " (key=" << key << "), waiting..." << std::endl;
            return pit->second.get();
        }
    }

    std::cout << "[ShaderModuleCacher::GetOrCreate] CACHE MISS for " << ci.shaderName
              << " (key=" << key << "), creating new resource..." << std::endl;

    // Call parent implementation which will invoke Create()
    return TypedCacher<ShaderModuleWrapper, ShaderModuleCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<ShaderModuleWrapper> ShaderModuleCacher::GetOrCreateShaderModule(
    const std::string& sourcePath,
    const std::string& entryPoint,
    const std::vector<std::string>& macros,
    VkShaderStageFlagBits stage,
    const std::string& shaderName)
{
    std::cout << "[ShaderModuleCacher] GetOrCreateShaderModule ENTRY: " << shaderName << std::endl;
    std::cout << "[ShaderModuleCacher]   sourcePath=" << sourcePath << std::endl;
    std::cout << "[ShaderModuleCacher]   entryPoint=" << entryPoint << std::endl;
    std::cout << "[ShaderModuleCacher]   stage=" << stage << std::endl;

    ShaderModuleCreateParams params;
    params.sourcePath = sourcePath;
    params.entryPoint = entryPoint;
    params.macroDefinitions = macros;
    params.stage = stage;
    params.shaderName = shaderName.empty() ? sourcePath : shaderName;
    params.sourceChecksum = ComputeSourceChecksum(sourcePath);

    std::cout << "[ShaderModuleCacher]   checksum=" << params.sourceChecksum << std::endl;

    uint64_t key = ComputeKey(params);
    std::cout << "[ShaderModuleCacher]   cache_key=" << key << std::endl;

    auto result = GetOrCreate(params);

    std::cout << "[ShaderModuleCacher] GetOrCreateShaderModule EXIT: VkShaderModule="
              << reinterpret_cast<uint64_t>(result->shaderModule) << std::endl;

    return result;
}

std::shared_ptr<ShaderModuleWrapper> ShaderModuleCacher::Create(const ShaderModuleCreateParams& ci) {
    std::cout << "[ShaderModuleCacher::Create] CACHE MISS - Creating new shader module: " << ci.shaderName << std::endl;

    auto wrapper = std::make_shared<ShaderModuleWrapper>();
    wrapper->shaderName = ci.shaderName;
    wrapper->stage = ci.stage;
    wrapper->sourcePath = ci.sourcePath;
    wrapper->entryPoint = ci.entryPoint;
    wrapper->macroDefinitions = ci.macroDefinitions;

    // Load/compile SPIR-V bytecode
    std::cout << "[ShaderModuleCacher::Create] Loading SPIR-V bytecode..." << std::endl;
    CompileShader(ci, *wrapper);
    std::cout << "[ShaderModuleCacher::Create] SPIR-V loaded: " << wrapper->spirvCode.size() << " uint32_t words" << std::endl;

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
            std::cout << "[ShaderModuleCacher::Create] FAILED to create VkShaderModule (VkResult=" << result << ")" << std::endl;
            throw std::runtime_error("Failed to create shader module: " + wrapper->shaderName);
        }

        std::cout << "[ShaderModuleCacher::Create] VkShaderModule created: "
                  << reinterpret_cast<uint64_t>(wrapper->shaderModule) << std::endl;
    }

    std::cout << "[ShaderModuleCacher::Create] Shader module creation complete" << std::endl;
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

    std::cout << "[ShaderModuleCacher::CompileShader] Resolved SPIR-V path: " << spirvPath << std::endl;

    try {
        std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t fileSize = static_cast<size_t>(file.tellg());
            file.seekg(0);

            std::cout << "[ShaderModuleCacher::CompileShader] File opened, size: " << fileSize << " bytes" << std::endl;

            // Read as bytes and convert to uint32_t
            std::vector<char> buffer(fileSize);
            file.read(buffer.data(), fileSize);

            // SPIR-V must be aligned to uint32_t
            wrapper.spirvCode.resize(fileSize / sizeof(uint32_t));
            std::memcpy(wrapper.spirvCode.data(), buffer.data(), fileSize);

            std::cout << "[ShaderModuleCacher::CompileShader] SPIR-V loaded successfully" << std::endl;
        } else {
            std::cout << "[ShaderModuleCacher::CompileShader] FAILED to open file: " << spirvPath << std::endl;
            throw std::runtime_error("Failed to open SPIR-V file: " + spirvPath);
        }
    } catch (const std::exception& e) {
        std::cout << "[ShaderModuleCacher::CompileShader] EXCEPTION: " << e.what() << std::endl;
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