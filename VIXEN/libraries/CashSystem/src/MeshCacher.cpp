#include "MeshCacher.h"
#include "MainCacher.h"
#include "VulkanDevice.h"
#include "VixenHash.h"
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <shared_mutex>
#include <cstring>

namespace CashSystem {

void MeshCacher::Cleanup() {
    std::cout << "[MeshCacher::Cleanup] Cleaning up " << m_entries.size() << " cached meshes" << std::endl;

    // Destroy all cached Vulkan resources
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource) {
                // Destroy vertex buffer
                if (entry.resource->vertexBuffer != VK_NULL_HANDLE) {
                    std::cout << "[MeshCacher::Cleanup] Destroying vertex buffer: "
                              << reinterpret_cast<uint64_t>(entry.resource->vertexBuffer) << std::endl;
                    vkDestroyBuffer(GetDevice()->device, entry.resource->vertexBuffer, nullptr);
                    entry.resource->vertexBuffer = VK_NULL_HANDLE;
                }

                // Free vertex memory
                if (entry.resource->vertexMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(GetDevice()->device, entry.resource->vertexMemory, nullptr);
                    entry.resource->vertexMemory = VK_NULL_HANDLE;
                }

                // Destroy index buffer
                if (entry.resource->indexBuffer != VK_NULL_HANDLE) {
                    std::cout << "[MeshCacher::Cleanup] Destroying index buffer: "
                              << reinterpret_cast<uint64_t>(entry.resource->indexBuffer) << std::endl;
                    vkDestroyBuffer(GetDevice()->device, entry.resource->indexBuffer, nullptr);
                    entry.resource->indexBuffer = VK_NULL_HANDLE;
                }

                // Free index memory
                if (entry.resource->indexMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(GetDevice()->device, entry.resource->indexMemory, nullptr);
                    entry.resource->indexMemory = VK_NULL_HANDLE;
                }

                // Clear cached CPU data
                entry.resource->vertexData.clear();
                entry.resource->indexData.clear();
            }
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    std::cout << "[MeshCacher::Cleanup] Cleanup complete" << std::endl;
}

std::shared_ptr<MeshWrapper> MeshCacher::GetOrCreate(const MeshCreateParams& ci) {
    auto key = ComputeKey(ci);
    std::string resourceName = ci.filePath.empty()
        ? "procedural_mesh_" + std::to_string(key)
        : ci.filePath;

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            std::cout << "[MeshCacher::GetOrCreate] CACHE HIT for " << resourceName
                      << " (key=" << key
                      << ", vertexBuffer=" << reinterpret_cast<uint64_t>(it->second.resource->vertexBuffer)
                      << ", vertices=" << it->second.resource->vertexCount
                      << ", indices=" << it->second.resource->indexCount << ")" << std::endl;
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            std::cout << "[MeshCacher::GetOrCreate] CACHE PENDING for " << resourceName
                      << " (key=" << key << "), waiting..." << std::endl;
            return pit->second.get();
        }
    }

    std::cout << "[MeshCacher::GetOrCreate] CACHE MISS for " << resourceName
              << " (key=" << key << "), creating new mesh..." << std::endl;

    // Call parent implementation which will invoke Create()
    return TypedCacher<MeshWrapper, MeshCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<MeshWrapper> MeshCacher::Create(const MeshCreateParams& ci) {
    std::cout << "[MeshCacher::Create] CACHE MISS - Creating new mesh" << std::endl;

    auto wrapper = std::make_shared<MeshWrapper>();

    // Store metadata
    wrapper->sourceIdentifier = ci.filePath.empty()
        ? "procedural_" + std::to_string(ComputeKey(ci))
        : ci.filePath;
    wrapper->vertexCount = ci.vertexCount;
    wrapper->indexCount = ci.indexCount;
    wrapper->vertexStride = ci.vertexStride;

    // Cache CPU-side vertex data
    if (ci.vertexDataPtr && ci.vertexDataSize > 0) {
        wrapper->vertexData.resize(ci.vertexDataSize / sizeof(float));
        std::memcpy(wrapper->vertexData.data(), ci.vertexDataPtr, ci.vertexDataSize);
        std::cout << "[MeshCacher::Create] Cached " << wrapper->vertexData.size()
                  << " vertex floats (" << ci.vertexDataSize << " bytes)" << std::endl;
    }

    // Cache CPU-side index data
    if (ci.indexDataPtr && ci.indexDataSize > 0) {
        wrapper->indexData.resize(ci.indexDataSize / sizeof(uint32_t));
        std::memcpy(wrapper->indexData.data(), ci.indexDataPtr, ci.indexDataSize);
        std::cout << "[MeshCacher::Create] Cached " << wrapper->indexData.size()
                  << " indices (" << ci.indexDataSize << " bytes)" << std::endl;
    }

    // Create vertex buffer
    if (ci.vertexDataSize > 0) {
        CreateBuffer(
            ci.vertexDataSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            ci.vertexMemoryFlags,
            wrapper->vertexBuffer,
            wrapper->vertexMemory
        );

        // Upload vertex data
        UploadData(wrapper->vertexMemory, ci.vertexDataPtr, ci.vertexDataSize);

        std::cout << "[MeshCacher::Create] Vertex buffer created: "
                  << reinterpret_cast<uint64_t>(wrapper->vertexBuffer) << std::endl;
    }

    // Create index buffer (if indices provided)
    if (ci.indexDataSize > 0) {
        CreateBuffer(
            ci.indexDataSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            ci.indexMemoryFlags,
            wrapper->indexBuffer,
            wrapper->indexMemory
        );

        // Upload index data
        UploadData(wrapper->indexMemory, ci.indexDataPtr, ci.indexDataSize);

        std::cout << "[MeshCacher::Create] Index buffer created: "
                  << reinterpret_cast<uint64_t>(wrapper->indexBuffer) << std::endl;
    }

    return wrapper;
}

std::uint64_t MeshCacher::ComputeKey(const MeshCreateParams& ci) const {
    std::ostringstream keyStream;

    // Use file path if available
    if (!ci.filePath.empty()) {
        keyStream << ci.filePath << "|";
    } else {
        // For procedural data, hash the raw data
        // Simple hash combining vertex and index data sizes and first few bytes
        keyStream << "procedural|"
                  << ci.vertexDataSize << "|"
                  << ci.indexDataSize << "|";

        // Add hash of actual data for uniqueness
        if (ci.vertexDataPtr && ci.vertexDataSize > 0) {
            const char* dataBytes = static_cast<const char*>(ci.vertexDataPtr);
            std::hash<std::string> hasher;
            std::string dataStr(dataBytes, std::min<size_t>(ci.vertexDataSize, 256)); // Hash first 256 bytes
            keyStream << hasher(dataStr) << "|";
        }
    }

    // Add vertex format parameters
    keyStream << ci.vertexStride << "|"
              << ci.vertexCount << "|"
              << ci.indexCount << "|"
              << ci.vertexMemoryFlags << "|"
              << ci.indexMemoryFlags;

    // Use standard hash function (matching PipelineCacher pattern)
    const std::string keyString = keyStream.str();
    return std::hash<std::string>{}(keyString);
}

bool MeshCacher::SerializeToFile(const std::filesystem::path& path) const {
    std::cout << "[MeshCacher::SerializeToFile] Serializing " << m_entries.size()
              << " mesh entries to " << path << std::endl;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cout << "[MeshCacher::SerializeToFile] Failed to open file for writing" << std::endl;
        return false;
    }

    // Write entry count
    std::shared_lock rlock(m_lock);
    uint32_t count = static_cast<uint32_t>(m_entries.size());
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // Write each entry: key + metadata + cached CPU data
    for (const auto& [key, entry] : m_entries) {
        ofs.write(reinterpret_cast<const char*>(&key), sizeof(key));

        const auto& w = entry.resource;

        // Write metadata
        ofs.write(reinterpret_cast<const char*>(&w->vertexCount), sizeof(w->vertexCount));
        ofs.write(reinterpret_cast<const char*>(&w->indexCount), sizeof(w->indexCount));
        ofs.write(reinterpret_cast<const char*>(&w->vertexStride), sizeof(w->vertexStride));

        // Write source identifier
        uint32_t sourceIdLen = static_cast<uint32_t>(w->sourceIdentifier.size());
        ofs.write(reinterpret_cast<const char*>(&sourceIdLen), sizeof(sourceIdLen));
        ofs.write(w->sourceIdentifier.data(), sourceIdLen);

        // Write cached vertex data
        uint32_t vertexDataCount = static_cast<uint32_t>(w->vertexData.size());
        ofs.write(reinterpret_cast<const char*>(&vertexDataCount), sizeof(vertexDataCount));
        if (vertexDataCount > 0) {
            ofs.write(reinterpret_cast<const char*>(w->vertexData.data()),
                      vertexDataCount * sizeof(float));
        }

        // Write cached index data
        uint32_t indexDataCount = static_cast<uint32_t>(w->indexData.size());
        ofs.write(reinterpret_cast<const char*>(&indexDataCount), sizeof(indexDataCount));
        if (indexDataCount > 0) {
            ofs.write(reinterpret_cast<const char*>(w->indexData.data()),
                      indexDataCount * sizeof(uint32_t));
        }
    }

    std::cout << "[MeshCacher::SerializeToFile] Serialization complete" << std::endl;
    return true;
}

bool MeshCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    std::cout << "[MeshCacher::DeserializeFromFile] Deserializing from " << path << std::endl;

    if (!std::filesystem::exists(path)) {
        std::cout << "[MeshCacher::DeserializeFromFile] Cache file does not exist" << std::endl;
        return false;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cout << "[MeshCacher::DeserializeFromFile] Failed to open file for reading" << std::endl;
        return false;
    }

    // Read entry count
    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

    std::cout << "[MeshCacher::DeserializeFromFile] Loading " << count
              << " mesh metadata entries" << std::endl;

    // Note: We deserialize metadata and CPU-side data.
    // Vulkan buffers will be recreated on-demand via GetOrCreate() when parameters match.

    for (uint32_t i = 0; i < count; ++i) {
        std::uint64_t key;
        ifs.read(reinterpret_cast<char*>(&key), sizeof(key));

        // Read metadata
        uint32_t vertexCount, indexCount, vertexStride;
        ifs.read(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
        ifs.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));
        ifs.read(reinterpret_cast<char*>(&vertexStride), sizeof(vertexStride));

        // Read source identifier
        uint32_t sourceIdLen;
        ifs.read(reinterpret_cast<char*>(&sourceIdLen), sizeof(sourceIdLen));
        std::string sourceIdentifier(sourceIdLen, '\0');
        ifs.read(sourceIdentifier.data(), sourceIdLen);

        // Read cached vertex data
        uint32_t vertexDataCount;
        ifs.read(reinterpret_cast<char*>(&vertexDataCount), sizeof(vertexDataCount));
        std::vector<float> vertexData(vertexDataCount);
        if (vertexDataCount > 0) {
            ifs.read(reinterpret_cast<char*>(vertexData.data()),
                     vertexDataCount * sizeof(float));
        }

        // Read cached index data
        uint32_t indexDataCount;
        ifs.read(reinterpret_cast<char*>(&indexDataCount), sizeof(indexDataCount));
        std::vector<uint32_t> indexData(indexDataCount);
        if (indexDataCount > 0) {
            ifs.read(reinterpret_cast<char*>(indexData.data()),
                     indexDataCount * sizeof(uint32_t));
        }

        std::cout << "[MeshCacher::DeserializeFromFile] Loaded mesh metadata for key " << key
                  << " (" << sourceIdentifier << ", "
                  << vertexCount << " vertices, " << indexCount << " indices)" << std::endl;

        // Note: Could optionally pre-populate cache with deserialized data here
        // For now, we just log it. Buffers will be recreated on-demand.
    }

    std::cout << "[MeshCacher::DeserializeFromFile] Deserialization complete (buffers will be created on-demand)" << std::endl;
    return true;
}

void MeshCacher::CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memoryFlags,
    VkBuffer& buffer,
    VkDeviceMemory& memory
) {
    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = 0;
    bufferInfo.pQueueFamilyIndices = nullptr;
    bufferInfo.flags = 0;

    VkResult result = vkCreateBuffer(GetDevice()->device, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("MeshCacher: Failed to create buffer (VkResult: " +
                                 std::to_string(result) + ")");
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(GetDevice()->device, buffer, &memRequirements);

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;
    allocInfo.allocationSize = memRequirements.size;

    // Find suitable memory type
    auto memoryTypeIndex = GetDevice()->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        memoryFlags
    );

    if (!memoryTypeIndex.has_value()) {
        vkDestroyBuffer(GetDevice()->device, buffer, nullptr);
        throw std::runtime_error("MeshCacher: Failed to find suitable memory type for buffer");
    }

    allocInfo.memoryTypeIndex = memoryTypeIndex.value();

    result = vkAllocateMemory(GetDevice()->device, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(GetDevice()->device, buffer, nullptr);
        throw std::runtime_error("MeshCacher: Failed to allocate buffer memory (VkResult: " +
                                 std::to_string(result) + ")");
    }

    // Bind buffer to memory
    result = vkBindBufferMemory(GetDevice()->device, buffer, memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(GetDevice()->device, memory, nullptr);
        vkDestroyBuffer(GetDevice()->device, buffer, nullptr);
        throw std::runtime_error("MeshCacher: Failed to bind buffer memory (VkResult: " +
                                 std::to_string(result) + ")");
    }
}

void MeshCacher::UploadData(
    VkDeviceMemory memory,
    const void* data,
    VkDeviceSize size
) {
    // Map memory
    void* mappedData;
    VkResult result = vkMapMemory(GetDevice()->device, memory, 0, size, 0, &mappedData);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("MeshCacher: Failed to map buffer memory (VkResult: " +
                                 std::to_string(result) + ")");
    }

    // Copy data
    std::memcpy(mappedData, data, static_cast<size_t>(size));

    // Unmap memory
    vkUnmapMemory(GetDevice()->device, memory);
}

} // namespace CashSystem
