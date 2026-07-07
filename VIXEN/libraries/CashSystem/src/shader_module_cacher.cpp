#include "pch.h"
#include "ShaderModuleCacher.h"
#include "CacheKeyHasher.h"
#include "CacheCodec.h"
#include "VulkanDevice.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <vulkan/vulkan.h>
#include <shared_mutex>

// Namespace alias for nested namespace
namespace VH = Vixen::Hash;

// Helper function to compute checksum for file contents using project Hash library
static std::string ComputeSourceChecksum_Helper(const std::string& sourcePath) {
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

        // Use project-wide hash function
        return VH::ComputeSHA256Hex(buffer.data(), buffer.size());
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
            LOG_DEBUG("CACHE HIT for " + ci.shaderName + " (key=" + std::to_string(key) + ")");
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            LOG_DEBUG("CACHE PENDING for " + ci.shaderName + " (key=" + std::to_string(key) + "), waiting...");
            return pit->second.get();
        }
    }

    LOG_DEBUG("CACHE MISS for " + ci.shaderName + " (key=" + std::to_string(key) + "), creating new resource...");

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
    LOG_DEBUG("GetOrCreateShaderModule: " + shaderName + ", source=" + sourcePath + ", stage=" + std::to_string(stage));

    ShaderModuleCreateParams params;
    params.sourcePath = sourcePath;
    params.entryPoint = entryPoint;
    params.macroDefinitions = macros;
    params.stage = stage;
    params.shaderName = shaderName.empty() ? sourcePath : shaderName;
    params.sourceChecksum = ComputeSourceChecksum(sourcePath);

    uint64_t key = ComputeKey(params);
    LOG_DEBUG("cache_key=" + std::to_string(key) + ", checksum=" + params.sourceChecksum);

    auto result = GetOrCreate(params);

    LOG_DEBUG("GetOrCreateShaderModule complete: VkShaderModule=" + std::to_string(reinterpret_cast<uint64_t>(result->shaderModule)));

    return result;
}

std::shared_ptr<ShaderModuleWrapper> ShaderModuleCacher::GetOrCreateFromSpirv(
    const std::vector<uint32_t>& spirvCode,
    const std::string& entryPoint,
    const std::vector<std::string>& macros,
    VkShaderStageFlagBits stage,
    const std::string& shaderName)
{
    LOG_DEBUG("GetOrCreateFromSpirv: " + shaderName + ", SPIR-V size=" + std::to_string(spirvCode.size()) + " words, stage=" + std::to_string(stage));

    // Debug: Check SPIR-V header (first 5 words)
    if (spirvCode.size() < 5) {
        LOG_ERROR("SPIR-V too small (< 5 words)");
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
    LOG_DEBUG("cache_key=" + std::to_string(key));

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            LOG_DEBUG("CACHE HIT for SPIR-V " + shaderName + " (key=" + std::to_string(key) + ")");
            return it->second.resource;
        }
    }

    LOG_DEBUG("CACHE MISS for SPIR-V " + shaderName + " (key=" + std::to_string(key) + "), creating new VkShaderModule...");

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
            LOG_ERROR("FAILED to create VkShaderModule from SPIR-V (VkResult=" + std::to_string(result) + ")");
            throw std::runtime_error("Failed to create shader module from SPIR-V: " + shaderName);
        }

        LOG_DEBUG("VkShaderModule created from SPIR-V: " + std::to_string(reinterpret_cast<uint64_t>(wrapper->shaderModule)));
    }

    // Cache the result
    {
        std::unique_lock wlock(m_lock);
        CacheEntry entry;
        entry.resource = wrapper;
        entry.key = key;
        m_entries[key] = std::move(entry);
    }

    LOG_DEBUG("GetOrCreateFromSpirv complete: VkShaderModule=" + std::to_string(reinterpret_cast<uint64_t>(wrapper->shaderModule)));

    return wrapper;
}

std::shared_ptr<ShaderModuleWrapper> ShaderModuleCacher::Create(const ShaderModuleCreateParams& ci) {
    LOG_DEBUG("Creating new shader module: " + ci.shaderName);

    auto wrapper = std::make_shared<ShaderModuleWrapper>();
    wrapper->shaderName = ci.shaderName;
    wrapper->stage = ci.stage;
    wrapper->sourcePath = ci.sourcePath;
    wrapper->entryPoint = ci.entryPoint;
    wrapper->macroDefinitions = ci.macroDefinitions;

    // Load/compile SPIR-V bytecode
    LOG_DEBUG("Loading SPIR-V bytecode...");
    CompileShader(ci, *wrapper);
    LOG_DEBUG("SPIR-V loaded: " + std::to_string(wrapper->spirvCode.size()) + " uint32_t words");

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
            LOG_ERROR("FAILED to create VkShaderModule (VkResult=" + std::to_string(result) + ")");
            throw std::runtime_error("Failed to create shader module: " + wrapper->shaderName);
        }

        LOG_DEBUG("VkShaderModule created: " + std::to_string(reinterpret_cast<uint64_t>(wrapper->shaderModule)));
    }

    LOG_DEBUG("Shader module creation complete");
    return wrapper;
}

std::uint64_t ShaderModuleCacher::ComputeKey(const ShaderModuleCreateParams& ci) const {
    // Use CacheKeyHasher for deterministic, binary hashing
    CacheKeyHasher hasher;
    hasher.Add(ci.sourcePath)
          .Add(ci.entryPoint)
          .Add(static_cast<uint32_t>(ci.stage))
          .Add(ci.sourceChecksum);

    hasher.Add(static_cast<uint32_t>(ci.macroDefinitions.size()));
    for (const auto& macro : ci.macroDefinitions) {
        hasher.Add(macro);
    }

    return hasher.Finalize();
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

    LOG_DEBUG("Resolved SPIR-V path: " + spirvPath);

    try {
        std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t fileSize = static_cast<size_t>(file.tellg());
            file.seekg(0);

            LOG_DEBUG("File opened, size: " + std::to_string(fileSize) + " bytes");

            // SPIR-V is a stream of uint32_t words; a size that isn't a multiple of 4 (or is
            // empty) cannot be valid SPIR-V and would previously overflow the destination
            // buffer by up to 3 bytes (V-M7: resize() rounded down, memcpy() copied fileSize).
            if (fileSize == 0 || fileSize % sizeof(uint32_t) != 0) {
                throw std::runtime_error("SPIR-V file size " + std::to_string(fileSize) +
                                          " is not a non-zero multiple of 4: " + spirvPath);
            }

            std::vector<char> buffer(fileSize);
            if (!file.read(buffer.data(), static_cast<std::streamsize>(fileSize))) {
                throw std::runtime_error("Failed to read SPIR-V file contents: " + spirvPath);
            }

            wrapper.spirvCode.resize(fileSize / sizeof(uint32_t));
            std::memcpy(wrapper.spirvCode.data(), buffer.data(), wrapper.spirvCode.size() * sizeof(uint32_t));

            // Reject anything that doesn't start with the SPIR-V magic number before it ever
            // reaches vkCreateShaderModule.
            constexpr uint32_t kSpirvMagic = 0x07230203u;
            if (wrapper.spirvCode.empty() || wrapper.spirvCode[0] != kSpirvMagic) {
                throw std::runtime_error("SPIR-V magic number mismatch: " + spirvPath);
            }

            LOG_DEBUG("SPIR-V loaded successfully");
        } else {
            LOG_ERROR("FAILED to open file: " + spirvPath);
            throw std::runtime_error("Failed to open SPIR-V file: " + spirvPath);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("EXCEPTION: " + std::string(e.what()));
        throw std::runtime_error("Shader compilation failed for " + ci.shaderName + ": " + e.what());
    }
}

void ShaderModuleCacher::Cleanup() {
    LOG_INFO("Cleaning up " + std::to_string(m_entries.size()) + " cached shader modules");

    // Destroy all cached VkShaderModule handles
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource && entry.resource->shaderModule != VK_NULL_HANDLE) {
                LOG_DEBUG("Destroying VkShaderModule: " + std::to_string(reinterpret_cast<uint64_t>(entry.resource->shaderModule)));
                vkDestroyShaderModule(GetDevice()->device, entry.resource->shaderModule, nullptr);
                entry.resource->shaderModule = VK_NULL_HANDLE;
            }
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    LOG_INFO("Cleanup complete");
}

bool ShaderModuleCacher::SerializeToFile(const std::filesystem::path& path) const {
    try {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("SerializeToFile: Failed to open file: " + path.string());
            return false;
        }

        CacheWriter writer(file);

        LOG_INFO("SerializeToFile: Saving " + std::to_string(m_entries.size()) + " shader modules to " + path.string());

        // Write header: version + entry count
        uint32_t version = 1;
        uint32_t entryCount = static_cast<uint32_t>(m_entries.size());
        writer.WritePod(version);
        writer.WritePod(entryCount);

        // Write each cache entry
        for (const auto& [key, entry] : m_entries) {
            if (!entry.resource || entry.resource->spirvCode.empty()) {
                continue;  // Skip invalid entries
            }

            // Write cache key
            writer.WritePod(key);

            // Write creation params
            const auto& ci = entry.ci;

            writer.WriteString32(ci.sourcePath);
            writer.WriteString32(ci.entryPoint);
            writer.WritePod(ci.stage);
            writer.WriteString32(ci.shaderName);
            writer.WriteString32(ci.sourceChecksum);

            // macroDefinitions count
            uint32_t macroCount = static_cast<uint32_t>(ci.macroDefinitions.size());
            writer.WritePod(macroCount);
            for (const auto& macro : ci.macroDefinitions) {
                writer.WriteString32(macro);
            }

            // Write SPIR-V bytecode
            writer.WriteVector32(entry.resource->spirvCode);
        }

        file.close();
        LOG_INFO("SerializeToFile: Successfully saved cache");
        return writer.Ok();

    } catch (const std::exception& e) {
        LOG_ERROR("SerializeToFile: Exception: " + std::string(e.what()));
        return false;
    }
}

bool ShaderModuleCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    try {
        if (!std::filesystem::exists(path)) {
            LOG_INFO("DeserializeFromFile: Cache file doesn't exist: " + path.string());
            return true;  // Not an error, just no cache to load
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("DeserializeFromFile: Failed to open file: " + path.string());
            return false;
        }

        LOG_INFO("DeserializeFromFile: Loading cache from " + path.string());

        CacheReader reader(file);

        // Caps: generous for any real shader (paths/names are never remotely this long; a
        // shader is never anywhere near 256Mi SPIR-V words) but small enough that a corrupt
        // length can't drive a runaway allocation.
        constexpr size_t kMaxStringLen = 1u << 20;       // 1 MiB
        constexpr size_t kMaxMacroCount = 1u << 16;      // 65536 macros
        constexpr size_t kMaxSpirvWords = 1u << 28;      // 256 Mi uint32_t words (1 GiB)

        // Read header
        uint32_t version = 0;
        uint32_t entryCount = 0;
        if (!reader.ReadPod(version) || !reader.ReadPod(entryCount)) {
            LOG_ERROR("DeserializeFromFile: Truncated header");
            return false;
        }

        if (version != 1) {
            LOG_ERROR("DeserializeFromFile: Unsupported cache version: " + std::to_string(version));
            return false;
        }

        LOG_INFO("DeserializeFromFile: Loading " + std::to_string(entryCount) + " shader modules");

        // Read each entry
        for (uint32_t i = 0; i < entryCount; ++i) {
            // Read cache key
            std::uint64_t key = 0;
            if (!reader.ReadPod(key)) {
                LOG_ERROR("DeserializeFromFile: Truncated key at entry " + std::to_string(i));
                return false;
            }

            // Read creation params
            ShaderModuleCreateParams ci;

            if (!reader.ReadString32(ci.sourcePath, kMaxStringLen) ||
                !reader.ReadString32(ci.entryPoint, kMaxStringLen) ||
                !reader.ReadPod(ci.stage) ||
                !reader.ReadString32(ci.shaderName, kMaxStringLen) ||
                !reader.ReadString32(ci.sourceChecksum, kMaxStringLen)) {
                LOG_ERROR("DeserializeFromFile: Truncated/oversized CreateInfo at entry " + std::to_string(i));
                return false;
            }

            // macroDefinitions
            uint32_t macroCount = 0;
            if (!reader.ReadPod(macroCount) || macroCount > kMaxMacroCount) {
                LOG_ERROR("DeserializeFromFile: Truncated/oversized macro count at entry " + std::to_string(i));
                return false;
            }
            ci.macroDefinitions.resize(macroCount);
            for (uint32_t m = 0; m < macroCount; ++m) {
                if (!reader.ReadString32(ci.macroDefinitions[m], kMaxStringLen)) {
                    LOG_ERROR("DeserializeFromFile: Truncated/oversized macro at entry " + std::to_string(i));
                    return false;
                }
            }

            // Read SPIR-V bytecode
            std::vector<uint32_t> spirvCode;
            if (!reader.ReadVector32(spirvCode, kMaxSpirvWords)) {
                LOG_ERROR("DeserializeFromFile: Truncated/oversized SPIR-V at entry " + std::to_string(i));
                return false;
            }

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
                    LOG_ERROR("DeserializeFromFile: Failed to recreate VkShaderModule for " + ci.shaderName);
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
        LOG_INFO("DeserializeFromFile: Successfully loaded " + std::to_string(m_entries.size()) + " shader modules from cache");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("DeserializeFromFile: Exception: " + std::string(e.what()));
        return false;
    }
}

} // namespace CashSystem