#pragma once

#include "TypedCacher.h"
#include "MainCacher.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <functional>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::SVO {
    class LaineKarrasOctree;
}

namespace Vixen::GaiaVoxel {
    class GaiaVoxelWorld;
}

namespace VIXEN::RenderGraph {
    class VoxelGrid;
}

namespace CashSystem {

// ============================================================================
// SCENE TYPE ENUM
// ============================================================================

/**
 * @brief Scene types matching SceneGenerator.h
 *
 * Mirrors the factory-registered scene generator names for consistent caching.
 */
enum class SceneType : uint32_t {
    CornellBox = 0,     // ~10% density - sparse
    Noise = 1,          // ~50% density - medium
    Tunnels = 2,        // ~30-50% density - caves
    Cityscape = 3,      // ~80-95% density - dense
    Custom = 255        // User-defined generator
};

/**
 * @brief Convert scene type enum to generator name string
 */
inline std::string SceneTypeToString(SceneType type) {
    switch (type) {
        case SceneType::CornellBox: return "cornell";
        case SceneType::Noise: return "noise";
        case SceneType::Tunnels: return "tunnels";
        case SceneType::Cityscape: return "cityscape";
        case SceneType::Custom: return "custom";
        default: return "unknown";
    }
}

/**
 * @brief Convert generator name string to scene type enum
 */
inline SceneType StringToSceneType(const std::string& name) {
    if (name == "cornell") return SceneType::CornellBox;
    if (name == "noise") return SceneType::Noise;
    if (name == "tunnels") return SceneType::Tunnels;
    if (name == "cityscape") return SceneType::Cityscape;
    return SceneType::Custom;
}

// ============================================================================
// GPU MATERIAL STRUCT (matches shader std140 layout)
// ============================================================================

/**
 * @brief GPU material data structure
 *
 * Must match shader's GPUMaterial struct layout (std430/std140).
 * Size: 32 bytes per material.
 */
struct GPUMaterial {
    float albedo[3];    // 12 bytes
    float roughness;    // 4 bytes
    float metallic;     // 4 bytes
    float emissive;     // 4 bytes
    float padding[2];   // 8 bytes
};
static_assert(sizeof(GPUMaterial) == 32, "GPUMaterial must be 32 bytes for GPU alignment");

// ============================================================================
// OCTREE CONFIG STRUCT (GPU UBO layout, must match shader std140)
// ============================================================================

/**
 * @brief Octree configuration UBO data
 *
 * Must match VoxelGridNode's OctreeConfig struct exactly.
 * Layout: std140 requires vec3 alignment to 16 bytes.
 */
struct OctreeConfig {
    // ESVO scale parameters (matching LaineKarrasOctree.h)
    int32_t esvoMaxScale;       // Always 22 (ESVO normalized space)
    int32_t userMaxLevels;      // log2(resolution) = 7 for 128^3
    int32_t brickDepthLevels;   // 3 for 8^3 bricks
    int32_t brickSize;          // 8 (voxels per brick axis)

    // Derived scale values
    int32_t minESVOScale;       // esvoMaxScale - userMaxLevels + 1 = 16
    int32_t brickESVOScale;     // Scale at which nodes are brick parents = 20
    int32_t bricksPerAxis;      // resolution / brickSize = 16
    int32_t _padding1;          // Pad to 16-byte alignment

    // Grid bounds (in world units)
    float gridMinX, gridMinY, gridMinZ;
    float _padding2;            // Pad vec3 to vec4

    float gridMaxX, gridMaxY, gridMaxZ;
    float _padding3;            // Pad vec3 to vec4

    // Coordinate Transformations
    glm::mat4 localToWorld;     // 64 bytes
    glm::mat4 worldToLocal;     // 64 bytes

    // Padding to reach 256 bytes (std140 alignment)
    // Current size: 16 + 16 + 16 + 16 + 64 + 64 = 192 bytes
    // Needed: 256 - 192 = 64 bytes
    float _padding4[16];

    // Non-UBO field (not uploaded) for convenience
    float worldGridSize;        // World space extent of the grid (used by code, not shader)
};
// Note: worldGridSize is outside the 256 byte UBO - only first 256 bytes are uploaded
static_assert(offsetof(OctreeConfig, worldGridSize) == 256, "OctreeConfig UBO portion must be 256 bytes");

// ============================================================================
// VOXEL SCENE CREATE INFO
// ============================================================================

/**
 * @brief Creation parameters for cached voxel scene data
 *
 * Used as key for cache lookup. Scenes with same (sceneType, resolution, density)
 * produce identical data, so we cache the result.
 */
struct VoxelSceneCreateInfo {
    SceneType sceneType = SceneType::CornellBox;
    uint32_t resolution = 128;
    float density = 0.5f;       // 0.0-1.0 (used by some generators)
    uint32_t seed = 42;         // For reproducibility

    /**
     * @brief Compute hash for cache key
     *
     * Density is quantized to 1% increments for stable hashing.
     */
    uint64_t ComputeHash() const noexcept {
        // Quantize density to prevent floating-point hash instability
        uint32_t densityQuantized = static_cast<uint32_t>(density * 100.0f);

        uint64_t hash = static_cast<uint64_t>(sceneType);
        hash = hash * 31 + resolution;
        hash = hash * 31 + densityQuantized;
        hash = hash * 31 + seed;
        return hash;
    }

    bool operator==(const VoxelSceneCreateInfo& other) const noexcept {
        return sceneType == other.sceneType &&
               resolution == other.resolution &&
               static_cast<uint32_t>(density * 100) == static_cast<uint32_t>(other.density * 100) &&
               seed == other.seed;
    }
};

// ============================================================================
// VOXEL SCENE DATA (Resource Wrapper)
// ============================================================================

/**
 * @brief Cached voxel scene data - CPU and GPU resources
 *
 * Contains all data outputs from VoxelGridNode's scene generation pipeline:
 * 1. Scene generation (VoxelGrid)
 * 2. Octree construction (ESVO nodes)
 * 3. DXT compression (colors/normals)
 * 4. GPU buffer upload
 *
 * GPU buffers share a single VkDeviceMemory allocation for efficiency.
 */
struct VoxelSceneData {
    // ===== CPU-side data (for re-upload or CPU-side queries) =====
    std::vector<uint8_t> esvoNodesCPU;          // ESVO octree node array
    std::vector<uint8_t> brickDataCPU;          // Raw brick voxel data
    std::vector<GPUMaterial> materialsCPU;      // Material palette
    std::vector<uint8_t> compressedColorsCPU;   // DXT1 color blocks
    std::vector<uint8_t> compressedNormalsCPU;  // DXT normal blocks
    OctreeConfig configCPU;                      // Octree configuration UBO
    std::vector<uint32_t> brickGridLookupCPU;   // Grid coord -> brick index

    // ===== GPU buffers =====
    VkBuffer esvoNodesBuffer = VK_NULL_HANDLE;
    VkBuffer brickDataBuffer = VK_NULL_HANDLE;
    VkBuffer materialsBuffer = VK_NULL_HANDLE;
    VkBuffer compressedColorsBuffer = VK_NULL_HANDLE;
    VkBuffer compressedNormalsBuffer = VK_NULL_HANDLE;
    VkBuffer octreeConfigBuffer = VK_NULL_HANDLE;
    VkBuffer brickGridLookupBuffer = VK_NULL_HANDLE;

    // ===== Buffer sizes (for descriptor set binding) =====
    VkDeviceSize esvoNodesSize = 0;
    VkDeviceSize brickDataSize = 0;
    VkDeviceSize materialsSize = 0;
    VkDeviceSize compressedColorsSize = 0;
    VkDeviceSize compressedNormalsSize = 0;
    VkDeviceSize octreeConfigSize = 0;
    VkDeviceSize brickGridLookupSize = 0;

    // ===== Single memory allocation for all buffers =====
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize totalMemorySize = 0;

    // ===== Metadata =====
    uint32_t nodeCount = 0;         // Number of ESVO nodes
    uint32_t brickCount = 0;        // Number of bricks with data
    uint32_t solidVoxelCount = 0;   // Total solid voxels
    uint32_t resolution = 0;        // Grid resolution
    SceneType sceneType = SceneType::CornellBox;

    /**
     * @brief Check if the cached data is valid and usable
     */
    bool IsValid() const noexcept {
        return esvoNodesBuffer != VK_NULL_HANDLE &&
               brickDataBuffer != VK_NULL_HANDLE &&
               materialsBuffer != VK_NULL_HANDLE &&
               memory != VK_NULL_HANDLE &&
               nodeCount > 0;
    }

    /**
     * @brief Check if compressed data is available
     */
    bool HasCompressedData() const noexcept {
        return compressedColorsBuffer != VK_NULL_HANDLE &&
               compressedNormalsBuffer != VK_NULL_HANDLE &&
               !compressedColorsCPU.empty();
    }

    /**
     * @brief Cleanup all GPU resources
     *
     * Must be called before VoxelSceneData is destroyed.
     * @param device Vulkan device used to create the resources
     */
    void Cleanup(VkDevice device);
};

// ============================================================================
// VOXEL SCENE CACHER
// ============================================================================

/**
 * @brief Cacher for voxel scene data
 *
 * Caches the expensive scene generation + octree construction + compression
 * pipeline. Key: (sceneType, resolution, density, seed).
 *
 * Thread-safe via TypedCacher's shared_mutex.
 *
 * @note This cacher is device-dependent (GPU buffers).
 */
class VoxelSceneCacher : public TypedCacher<VoxelSceneData, VoxelSceneCreateInfo> {
public:
    VoxelSceneCacher() = default;
    ~VoxelSceneCacher() override;  // Defined in .cpp where complete types are available

    /**
     * @brief Get or create cached scene data
     *
     * If scene with matching key exists, returns cached data.
     * Otherwise, generates scene, builds octree, compresses, uploads to GPU.
     *
     * @param ci Scene creation parameters
     * @return Shared pointer to cached scene data
     */
    std::shared_ptr<VoxelSceneData> GetOrCreate(const VoxelSceneCreateInfo& ci);

    // ===== TypedCacher interface =====
    std::string_view name() const noexcept override { return "VoxelSceneCacher"; }

    // Serialization (stub for now - scene data is regeneratable)
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;

protected:
    // ===== TypedCacher implementation =====
    std::shared_ptr<VoxelSceneData> Create(const VoxelSceneCreateInfo& ci) override;
    std::uint64_t ComputeKey(const VoxelSceneCreateInfo& ci) const override;

    // Resource cleanup
    void Cleanup() override;

private:
    // ===== Helper methods =====

    /**
     * @brief Generate the voxel scene using SceneGeneratorFactory
     */
    void GenerateScene(const VoxelSceneCreateInfo& ci, VoxelSceneData& data);

    /**
     * @brief Build ESVO octree from voxel grid
     */
    void BuildOctree(VoxelSceneData& data);

    /**
     * @brief Compress colors and normals using DXT
     */
    void CompressData(VoxelSceneData& data);

    /**
     * @brief Build brick grid lookup table
     */
    void BuildBrickGridLookup(VoxelSceneData& data);

    /**
     * @brief Create GPU buffers and upload data
     */
    void UploadToGPU(VoxelSceneData& data);

    /**
     * @brief Create a buffer with device-local memory
     */
    VkBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage);

    /**
     * @brief Find memory type index for buffer requirements
     */
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    /**
     * @brief Upload data to buffer via staging buffer
     */
    void UploadBufferData(VkBuffer buffer, const void* data, VkDeviceSize size, VkDeviceSize offset);

    // Command pool for transfers (created on first use)
    VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;

    // Temporary build data (cleared after Create() completes)
    std::unique_ptr<VIXEN::RenderGraph::VoxelGrid> m_cachedGrid;
    std::unique_ptr<Vixen::GaiaVoxel::GaiaVoxelWorld> m_voxelWorld;
    std::unique_ptr<Vixen::SVO::LaineKarrasOctree> m_octree;
};

// ============================================================================
// REGISTRATION HELPER
// ============================================================================

/**
 * @brief Register VoxelSceneCacher with MainCacher
 *
 * Call during application initialization before using the cacher.
 */
inline void RegisterVoxelSceneCacher() {
    MainCacher::Instance().RegisterCacher<VoxelSceneCacher, VoxelSceneData, VoxelSceneCreateInfo>(
        std::type_index(typeid(VoxelSceneData)),
        "VoxelSceneCacher",
        true  // device-dependent
    );
}

} // namespace CashSystem
