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

std::shared_ptr<ShaderModuleWrapper> ShaderModuleCacher::GetOrCreateFromSpirv(
    const std::vector<uint32_t>& spirvCode,
    const std::string& entryPoint,
    const std::vector<std::string>& macros,
    VkShaderStageFlagBits stage,
    const std::string& shaderName)
{
    std::cout << "[ShaderModuleCacher] GetOrCreateFromSpirv ENTRY: " << shaderName << std::endl;
    std::cout << "[ShaderModuleCacher]   SPIR-V size=" << spirvCode.size() << " uint32_t words" << std::endl;
    std::cout << "[ShaderModuleCacher]   entryPoint=" << entryPoint << std::endl;
    std::cout << "[ShaderModuleCacher]   stage=" << stage << std::endl;

    // Debug: Check SPIR-V header (first 5 words)
    if (spirvCode.size() >= 5) {
        std::cout << "[ShaderModuleCacher]   SPIR-V header: magic=" << std::hex << spirvCode[0]
                  << " version=" << spirvCode[1]
                  << " generator=" << spirvCode[2]
                  << " bound=" << spirvCode[3]
                  << " schema=" << spirvCode[4] << std::dec << std::endl;
    } else {
        std::cout << "[ShaderModuleCacher]   ERROR: SPIR-V too small (< 5 words)" << std::endl;
    }

    // Compute hash of SPIR-V code for cache key
    std::uint64_t spirvHash = 14695981039346656037ULL;
    for (uint32_t word : spirvCode) {
        spirvHash ^= word;
        spirvHash *= 1099511628211ULL;
    }
    std::ostringstream oss;
    oss << std::hex << spirvHash;
    std::string spirvChecksum = oss.str();

    // Create pseudo-source path from shader name and checksum (for cache key)
    std::string pseudoSourcePath = "spirv://" + shaderName + "/" + spirvChecksum;

    ShaderModuleCreateParams params;
    params.sourcePath = pseudoSourcePath;
    params.entryPoint = entryPoint;
    params.macroDefinitions = macros;
    params.stage = stage;
    params.shaderName = shaderName;
    params.sourceChecksum = spirvChecksum;

    uint64_t key = ComputeKey(params);
    std::cout << "[ShaderModuleCacher]   cache_key=" << key << std::endl;

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            std::cout << "[ShaderModuleCacher] CACHE HIT for SPIR-V " << shaderName
                      << " (key=" << key << ", VkShaderModule="
                      << reinterpret_cast<uint64_t>(it->second.resource->shaderModule) << ")" << std::endl;
            return it->second.resource;
        }
    }

    std::cout << "[ShaderModuleCacher] CACHE MISS for SPIR-V " << shaderName
              << " (key=" << key << "), creating new VkShaderModule..." << std::endl;

    // Create wrapper directly from SPIR-V
    auto wrapper = std::make_shared<ShaderModuleWrapper>();
    wrapper->shaderName = shaderName;
    wrapper->stage = stage;
    wrapper->sourcePath = pseudoSourcePath;
    wrapper->entryPoint = entryPoint;
    wrapper->macroDefinitions = macros;
    wrapper->spirvCode = spirvCode;

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
            std::cout << "[ShaderModuleCacher] FAILED to create VkShaderModule from SPIR-V (VkResult=" << result << ")" << std::endl;
            throw std::runtime_error("Failed to create shader module from SPIR-V: " + shaderName);
        }

        std::cout << "[ShaderModuleCacher] VkShaderModule created from SPIR-V: "
                  << reinterpret_cast<uint64_t>(wrapper->shaderModule) << std::endl;
    }

    // Cache the result
    {
        std::unique_lock wlock(m_lock);
        CacheEntry entry;
        entry.resource = wrapper;
        entry.key = key;
        m_entries[key] = std::move(entry);
    }

    std::cout << "[ShaderModuleCacher] GetOrCreateFromSpirv EXIT: VkShaderModule="
              << reinterpret_cast<uint64_t>(wrapper->shaderModule) << std::endl;

    return wrapper;
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

void ShaderModuleCacher::Cleanup() {
    std::cout << "[ShaderModuleCacher::Cleanup] Cleaning up " << m_entries.size() << " cached shader modules" << std::endl;

    // Destroy all cached VkShaderModule handles
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource && entry.resource->shaderModule != VK_NULL_HANDLE) {
                std::cout << "[ShaderModuleCacher::Cleanup] Destroying VkShaderModule: "
                          << reinterpret_cast<uint64_t>(entry.resource->shaderModule) << std::endl;
                vkDestroyShaderModule(GetDevice()->device, entry.resource->shaderModule, nullptr);
                entry.resource->shaderModule = VK_NULL_HANDLE;
            }
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    std::cout << "[ShaderModuleCacher::Cleanup] Cleanup complete" << std::endl;
}

bool ShaderModuleCacher::SerializeToFile(const std::filesystem::path& path) const {
    try {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[ShaderModuleCacher::SerializeToFile] Failed to open file: " << path << std::endl;
            return false;
        }

        std::cout << "[ShaderModuleCacher::SerializeToFile] Saving " << m_entries.size() << " shader modules to " << path << std::endl;

        // Write header: version + entry count
        uint32_t version = 1;
        uint32_t entryCount = static_cast<uint32_t>(m_entries.size());
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));
        file.write(reinterpret_cast<const char*>(&entryCount), sizeof(entryCount));

        // Write each cache entry
        for (const auto& [key, entry] : m_entries) {
            if (!entry.resource || entry.resource->spirvCode.empty()) {
                continue;  // Skip invalid entries
            }

            // Write cache key
            file.write(reinterpret_cast<const char*>(&key), sizeof(key));

            // Write creation params
            const auto& ci = entry.ci;

            // sourcePath
            uint32_t sourcePathLen = static_cast<uint32_t>(ci.sourcePath.size());
            file.write(reinterpret_cast<const char*>(&sourcePathLen), sizeof(sourcePathLen));
            file.write(ci.sourcePath.data(), sourcePathLen);

            // entryPoint
            uint32_t entryPointLen = static_cast<uint32_t>(ci.entryPoint.size());
            file.write(reinterpret_cast<const char*>(&entryPointLen), sizeof(entryPointLen));
            file.write(ci.entryPoint.data(), entryPointLen);

            // shader stage
            file.write(reinterpret_cast<const char*>(&ci.stage), sizeof(ci.stage));

            // shaderName
            uint32_t shaderNameLen = static_cast<uint32_t>(ci.shaderName.size());
            file.write(reinterpret_cast<const char*>(&shaderNameLen), sizeof(shaderNameLen));
            file.write(ci.shaderName.data(), shaderNameLen);

            // sourceChecksum
            uint32_t checksumLen = static_cast<uint32_t>(ci.sourceChecksum.size());
            file.write(reinterpret_cast<const char*>(&checksumLen), sizeof(checksumLen));
            file.write(ci.sourceChecksum.data(), checksumLen);

            // macroDefinitions count
            uint32_t macroCount = static_cast<uint32_t>(ci.macroDefinitions.size());
            file.write(reinterpret_cast<const char*>(&macroCount), sizeof(macroCount));
            for (const auto& macro : ci.macroDefinitions) {
                uint32_t macroLen = static_cast<uint32_t>(macro.size());
                file.write(reinterpret_cast<const char*>(&macroLen), sizeof(macroLen));
                file.write(macro.data(), macroLen);
            }

            // Write SPIR-V bytecode
            uint32_t spirvSize = static_cast<uint32_t>(entry.resource->spirvCode.size());
            file.write(reinterpret_cast<const char*>(&spirvSize), sizeof(spirvSize));
            file.write(reinterpret_cast<const char*>(entry.resource->spirvCode.data()),
                       spirvSize * sizeof(uint32_t));
        }

        file.close();
        std::cout << "[ShaderModuleCacher::SerializeToFile] Successfully saved cache" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[ShaderModuleCacher::SerializeToFile] Exception: " << e.what() << std::endl;
        return false;
    }
}

bool ShaderModuleCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    try {
        if (!std::filesystem::exists(path)) {
            std::cout << "[ShaderModuleCacher::DeserializeFromFile] Cache file doesn't exist: " << path << std::endl;
            return true;  // Not an error, just no cache to load
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[ShaderModuleCacher::DeserializeFromFile] Failed to open file: " << path << std::endl;
            return false;
        }

        std::cout << "[ShaderModuleCacher::DeserializeFromFile] Loading cache from " << path << std::endl;

        // Read header
        uint32_t version = 0;
        uint32_t entryCount = 0;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        file.read(reinterpret_cast<char*>(&entryCount), sizeof(entryCount));

        if (version != 1) {
            std::cerr << "[ShaderModuleCacher::DeserializeFromFile] Unsupported cache version: " << version << std::endl;
            return false;
        }

        std::cout << "[ShaderModuleCacher::DeserializeFromFile] Loading " << entryCount << " shader modules" << std::endl;

        // Read each entry
        for (uint32_t i = 0; i < entryCount; ++i) {
            // Read cache key
            std::uint64_t key = 0;
            file.read(reinterpret_cast<char*>(&key), sizeof(key));

            // Read creation params
            ShaderModuleCreateParams ci;

            // sourcePath
            uint32_t sourcePathLen = 0;
            file.read(reinterpret_cast<char*>(&sourcePathLen), sizeof(sourcePathLen));
            ci.sourcePath.resize(sourcePathLen);
            file.read(ci.sourcePath.data(), sourcePathLen);

            // entryPoint
            uint32_t entryPointLen = 0;
            file.read(reinterpret_cast<char*>(&entryPointLen), sizeof(entryPointLen));
            ci.entryPoint.resize(entryPointLen);
            file.read(ci.entryPoint.data(), entryPointLen);

            // shader stage
            file.read(reinterpret_cast<char*>(&ci.stage), sizeof(ci.stage));

            // shaderName
            uint32_t shaderNameLen = 0;
            file.read(reinterpret_cast<char*>(&shaderNameLen), sizeof(shaderNameLen));
            ci.shaderName.resize(shaderNameLen);
            file.read(ci.shaderName.data(), shaderNameLen);

            // sourceChecksum
            uint32_t checksumLen = 0;
            file.read(reinterpret_cast<char*>(&checksumLen), sizeof(checksumLen));
            ci.sourceChecksum.resize(checksumLen);
            file.read(ci.sourceChecksum.data(), checksumLen);

            // macroDefinitions
            uint32_t macroCount = 0;
            file.read(reinterpret_cast<char*>(&macroCount), sizeof(macroCount));
            ci.macroDefinitions.resize(macroCount);
            for (uint32_t m = 0; m < macroCount; ++m) {
                uint32_t macroLen = 0;
                file.read(reinterpret_cast<char*>(&macroLen), sizeof(macroLen));
                ci.macroDefinitions[m].resize(macroLen);
                file.read(ci.macroDefinitions[m].data(), macroLen);
            }

            // Read SPIR-V bytecode
            uint32_t spirvSize = 0;
            file.read(reinterpret_cast<char*>(&spirvSize), sizeof(spirvSize));
            std::vector<uint32_t> spirvCode(spirvSize);
            file.read(reinterpret_cast<char*>(spirvCode.data()), spirvSize * sizeof(uint32_t));

            // Create wrapper and recreate VkShaderModule
            auto wrapper = std::make_shared<ShaderModuleWrapper>();
            wrapper->shaderName = ci.shaderName;
            wrapper->stage = ci.stage;
            wrapper->sourcePath = ci.sourcePath;
            wrapper->entryPoint = ci.entryPoint;
            wrapper->macroDefinitions = ci.macroDefinitions;
            wrapper->spirvCode = std::move(spirvCode);

            // Recreate VkShaderModule if device available
            if (GetDevice() && !wrapper->spirvCode.empty()) {
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
                    std::cerr << "[ShaderModuleCacher::DeserializeFromFile] Failed to recreate VkShaderModule for "
                              << ci.shaderName << std::endl;
                    continue;  // Skip this entry
                }
            }

            // Insert into cache
            CacheEntry entry;
            entry.key = key;
            entry.ci = std::move(ci);
            entry.resource = wrapper;

            std::unique_lock lock(m_lock);
            m_entries.emplace(key, std::move(entry));
        }

        file.close();
        std::cout << "[ShaderModuleCacher::DeserializeFromFile] Successfully loaded " << m_entries.size()
                  << " shader modules from cache" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[ShaderModuleCacher::DeserializeFromFile] Exception: " << e.what() << std::endl;
        return false;
    }
}

} // namespace CashSystem