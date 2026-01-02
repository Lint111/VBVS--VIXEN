#include "pch.h"
#include "MeshCacher.h"
#include "MainCacher.h"
#include "VulkanDevice.h"
#include "VixenHash.h"
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>
#include <fstream>
#include <filesystem>
#include <shared_mutex>
#include <cstring>

namespace CashSystem {

void MeshCacher::Cleanup() {
    LOG_INFO("Cleaning up " + std::to_string(m_entries.size()) + " cached meshes");

    // Free all cached Vulkan resources via allocator infrastructure
    for (auto& [key, entry] : m_entries) {
        if (entry.resource) {
            // Free vertex allocation
            if (entry.resource->vertexAllocation.buffer != VK_NULL_HANDLE) {
                LOG_DEBUG("Freeing vertex buffer: " + std::to_string(reinterpret_cast<uint64_t>(entry.resource->vertexAllocation.buffer)));
                FreeBufferTracked(entry.resource->vertexAllocation);
            }

            // Free index allocation
            if (entry.resource->indexAllocation.buffer != VK_NULL_HANDLE) {
                LOG_DEBUG("Freeing index buffer: " + std::to_string(reinterpret_cast<uint64_t>(entry.resource->indexAllocation.buffer)));
                FreeBufferTracked(entry.resource->indexAllocation);
            }

            // Clear cached CPU data
            entry.resource->vertexData.clear();
            entry.resource->indexData.clear();
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    LOG_INFO("Cleanup complete");
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
            LOG_DEBUG("CACHE HIT for " + resourceName + " (key=" + std::to_string(key) + ", vertices=" + std::to_string(it->second.resource->vertexCount) + ", indices=" + std::to_string(it->second.resource->indexCount) + ")");
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            LOG_DEBUG("CACHE PENDING for " + resourceName + " (key=" + std::to_string(key) + "), waiting...");
            return pit->second.get();
        }
    }

    LOG_DEBUG("CACHE MISS for " + resourceName + " (key=" + std::to_string(key) + "), creating new mesh...");

    // Call parent implementation which will invoke Create()
    return TypedCacher<MeshWrapper, MeshCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<MeshWrapper> MeshCacher::Create(const MeshCreateParams& ci) {
    LOG_DEBUG("Creating new mesh");

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
        LOG_DEBUG("Cached " + std::to_string(wrapper->vertexData.size()) + " vertex floats (" + std::to_string(ci.vertexDataSize) + " bytes)");
    }

    // Cache CPU-side index data
    if (ci.indexDataPtr && ci.indexDataSize > 0) {
        wrapper->indexData.resize(ci.indexDataSize / sizeof(uint32_t));
        std::memcpy(wrapper->indexData.data(), ci.indexDataPtr, ci.indexDataSize);
        LOG_DEBUG("Cached " + std::to_string(wrapper->indexData.size()) + " indices (" + std::to_string(ci.indexDataSize) + " bytes)");
    }

    // Create vertex buffer via allocator infrastructure
    if (ci.vertexDataSize > 0) {
        auto vertexAlloc = AllocateBufferTracked(
            ci.vertexDataSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            ci.vertexMemoryFlags,
            "Mesh_vertex"
        );
        if (!vertexAlloc) {
            throw std::runtime_error("MeshCacher: Failed to allocate vertex buffer");
        }
        wrapper->vertexAllocation = *vertexAlloc;

        // Upload vertex data via MapBufferTracked
        void* mappedData = MapBufferTracked(wrapper->vertexAllocation);
        if (!mappedData) {
            FreeBufferTracked(wrapper->vertexAllocation);
            throw std::runtime_error("MeshCacher: Failed to map vertex buffer");
        }
        std::memcpy(mappedData, ci.vertexDataPtr, ci.vertexDataSize);
        UnmapBufferTracked(wrapper->vertexAllocation);

        LOG_DEBUG("Vertex buffer created: " + std::to_string(reinterpret_cast<uint64_t>(wrapper->vertexAllocation.buffer)));
    }

    // Create index buffer (if indices provided)
    if (ci.indexDataSize > 0) {
        auto indexAlloc = AllocateBufferTracked(
            ci.indexDataSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            ci.indexMemoryFlags,
            "Mesh_index"
        );
        if (!indexAlloc) {
            // Clean up vertex allocation on failure
            FreeBufferTracked(wrapper->vertexAllocation);
            throw std::runtime_error("MeshCacher: Failed to allocate index buffer");
        }
        wrapper->indexAllocation = *indexAlloc;

        // Upload index data via MapBufferTracked
        void* mappedData = MapBufferTracked(wrapper->indexAllocation);
        if (!mappedData) {
            FreeBufferTracked(wrapper->indexAllocation);
            FreeBufferTracked(wrapper->vertexAllocation);
            throw std::runtime_error("MeshCacher: Failed to map index buffer");
        }
        std::memcpy(mappedData, ci.indexDataPtr, ci.indexDataSize);
        UnmapBufferTracked(wrapper->indexAllocation);

        LOG_DEBUG("Index buffer created: " + std::to_string(reinterpret_cast<uint64_t>(wrapper->indexAllocation.buffer)));
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
    LOG_INFO("SerializeToFile: Serializing " + std::to_string(m_entries.size()) + " mesh entries to " + path.string());

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        LOG_ERROR("SerializeToFile: Failed to open file for writing");
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

    LOG_INFO("SerializeToFile: Serialization complete");
    return true;
}

bool MeshCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    LOG_INFO("DeserializeFromFile: Deserializing from " + path.string());

    if (!std::filesystem::exists(path)) {
        LOG_INFO("DeserializeFromFile: Cache file does not exist");
        return false;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        LOG_ERROR("DeserializeFromFile: Failed to open file for reading");
        return false;
    }

    // Read entry count
    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

    LOG_INFO("DeserializeFromFile: Loading " + std::to_string(count) + " mesh metadata entries");

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

        LOG_DEBUG("Loaded mesh metadata for key " + std::to_string(key) + " (" + sourceIdentifier + ", " + std::to_string(vertexCount) + " vertices, " + std::to_string(indexCount) + " indices)");

        // Note: Could optionally pre-populate cache with deserialized data here
        // For now, we just log it. Buffers will be recreated on-demand.
    }

    LOG_INFO("DeserializeFromFile: Deserialization complete (buffers will be created on-demand)");
    return true;
}

} // namespace CashSystem
