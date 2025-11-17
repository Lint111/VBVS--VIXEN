#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include <cstdint>
#include <string>
#include <memory>
#include <vector>

namespace CashSystem {

/**
 * @brief Resource wrapper for Mesh (Vertex + Index Buffers)
 *
 * Stores VkBuffer handles, VkDeviceMemory, and cached CPU-side data.
 * Key benefit: Caches BOTH Vulkan buffers AND parsed vertex/index arrays.
 */
struct MeshWrapper {
    // Vulkan resources
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;

    // Cached CPU-side data (key benefit of caching)
    std::vector<float> vertexData;      // Interleaved vertex data
    std::vector<uint32_t> indexData;    // Index data

    // Metadata
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;

    // Cache identification (for debugging/logging)
    std::string sourceIdentifier;  // File path or hash of procedural data
};

/**
 * @brief Creation parameters for Mesh
 *
 * All parameters that affect mesh creation.
 * Used to generate cache keys and create resources.
 */
struct MeshCreateParams {
    // Source data - either file path OR raw data
    std::string filePath;  // Empty if using raw data

    // Raw vertex/index data (if not loading from file)
    const void* vertexDataPtr = nullptr;
    const void* indexDataPtr = nullptr;
    size_t vertexDataSize = 0;
    size_t indexDataSize = 0;

    // Vertex format
    uint32_t vertexStride = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    // Memory properties
    VkMemoryPropertyFlags vertexMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkMemoryPropertyFlags indexMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
};

/**
 * @brief TypedCacher for Mesh resources
 *
 * Caches meshes based on file path (or hash of procedural data) and vertex format.
 * Meshes are expensive to create because of heavy I/O (OBJ, GLTF parsing) and large binary data.
 *
 * Usage:
 * ```cpp
 * auto& mainCacher = GetOwningGraph()->GetMainCacher();
 *
 * // Register if needed (done in node)
 * if (!mainCacher.IsRegistered(std::type_index(typeid(MeshWrapper)))) {
 *     mainCacher.RegisterCacher<MeshCacher, MeshWrapper, MeshCreateParams>(
 *         std::type_index(typeid(MeshWrapper)),
 *         "Mesh",
 *         true  // device-dependent
 *     );
 * }
 *
 * // Get cacher
 * auto* cacher = mainCacher.GetCacher<MeshCacher, MeshWrapper, MeshCreateParams>(
 *     std::type_index(typeid(MeshWrapper)), device
 * );
 *
 * // Create parameters
 * MeshCreateParams params{};
 * params.filePath = "models/cube.obj";
 * // OR for raw data:
 * // params.vertexDataPtr = geometryData;
 * // params.vertexDataSize = sizeof(geometryData);
 * params.vertexStride = sizeof(VertexWithUV);
 * params.vertexCount = 36;
 * params.indexCount = 0;
 *
 * // Get or create cached resource
 * auto wrapper = cacher->GetOrCreate(params);
 * VkBuffer vertexBuffer = wrapper->vertexBuffer;
 * VkBuffer indexBuffer = wrapper->indexBuffer;
 * ```
 */
class MeshCacher : public TypedCacher<MeshWrapper, MeshCreateParams> {
public:
    MeshCacher() = default;
    ~MeshCacher() override = default;

    // Override to add cache hit/miss logging
    std::shared_ptr<MeshWrapper> GetOrCreate(const MeshCreateParams& ci);

    // Serialization (mesh data can be serialized)
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "MeshCacher"; }

protected:
    // TypedCacher implementation
    std::shared_ptr<MeshWrapper> Create(const MeshCreateParams& ci) override;
    std::uint64_t ComputeKey(const MeshCreateParams& ci) const override;

    // Resource cleanup
    void Cleanup() override;

private:
    // Helper functions
    void CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memoryFlags,
        VkBuffer& buffer,
        VkDeviceMemory& memory
    );

    void UploadData(
        VkDeviceMemory memory,
        const void* data,
        VkDeviceSize size
    );
};

} // namespace CashSystem
