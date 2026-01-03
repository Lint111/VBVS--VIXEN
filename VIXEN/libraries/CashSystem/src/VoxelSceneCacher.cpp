#include "pch.h"
#include "VoxelSceneCacher.h"
#include "VulkanDevice.h"
#include "error/VulkanError.h"
#include "Memory/BatchedUploader.h"  // For InvalidUploadHandle

// SVO library integration
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include "SVOTypes.h"
#include "SVOBuilder.h"

// RenderGraph library integration
#include "Data/SceneGenerator.h"

#include <iostream>
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <cmath>
#include <span>

using namespace Vixen::GaiaVoxel;
using namespace VIXEN::RenderGraph;

namespace CashSystem {

// ============================================================================
// VOXEL SCENE CACHER - DESTRUCTOR
// ============================================================================

VoxelSceneCacher::~VoxelSceneCacher() = default;

// ============================================================================
// VOXEL SCENE DATA - CLEANUP
// ============================================================================

void VoxelSceneData::Cleanup(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    // Destroy all buffers
    if (esvoNodesBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, esvoNodesBuffer, nullptr);
        esvoNodesBuffer = VK_NULL_HANDLE;
    }
    if (brickDataBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, brickDataBuffer, nullptr);
        brickDataBuffer = VK_NULL_HANDLE;
    }
    if (materialsBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, materialsBuffer, nullptr);
        materialsBuffer = VK_NULL_HANDLE;
    }
    if (compressedColorsBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, compressedColorsBuffer, nullptr);
        compressedColorsBuffer = VK_NULL_HANDLE;
    }
    if (compressedNormalsBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, compressedNormalsBuffer, nullptr);
        compressedNormalsBuffer = VK_NULL_HANDLE;
    }
    if (octreeConfigBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, octreeConfigBuffer, nullptr);
        octreeConfigBuffer = VK_NULL_HANDLE;
    }
    if (brickGridLookupBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, brickGridLookupBuffer, nullptr);
        brickGridLookupBuffer = VK_NULL_HANDLE;
    }

    // Free the single memory allocation
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }

    // Clear CPU-side data
    esvoNodesCPU.clear();
    brickDataCPU.clear();
    materialsCPU.clear();
    compressedColorsCPU.clear();
    compressedNormalsCPU.clear();
    brickGridLookupCPU.clear();

    // Reset sizes
    esvoNodesSize = 0;
    brickDataSize = 0;
    materialsSize = 0;
    compressedColorsSize = 0;
    compressedNormalsSize = 0;
    octreeConfigSize = 0;
    brickGridLookupSize = 0;
    totalMemorySize = 0;

    // Reset metadata
    nodeCount = 0;
    brickCount = 0;
    solidVoxelCount = 0;
}

// ============================================================================
// VOXEL SCENE CACHER - PUBLIC API
// ============================================================================

std::shared_ptr<VoxelSceneData> VoxelSceneCacher::GetOrCreate(const VoxelSceneCreateInfo& ci) {
    // Call base class GetOrCreate (which uses Create() override)
    return TypedCacher<VoxelSceneData, VoxelSceneCreateInfo>::GetOrCreate(ci);
}

// ============================================================================
// VOXEL SCENE CACHER - TYPEDCACHER IMPLEMENTATION
// ============================================================================

std::shared_ptr<VoxelSceneData> VoxelSceneCacher::Create(const VoxelSceneCreateInfo& ci) {
    LOG_INFO("[VoxelSceneCacher::Create] Creating scene data for " + SceneTypeToString(ci.sceneType) + " @ " + std::to_string(ci.resolution) + "^3, density=" + std::to_string(ci.density));

    if (!IsInitialized()) {
        throw std::runtime_error("[VoxelSceneCacher::Create] Cacher not initialized with device");
    }

    auto data = std::make_shared<VoxelSceneData>();
    data->resolution = ci.resolution;
    data->sceneType = ci.sceneType;

    // Step 1: Generate scene (VoxelGrid -> solid voxel positions)
    GenerateScene(ci, *data);

    // Step 2: Build ESVO octree from voxel data
    BuildOctree(*data);

    // Step 3: Compress colors/normals using DXT
    CompressData(*data);

    // Step 4: Build brick grid lookup table
    BuildBrickGridLookup(*data);

    // Step 5: Upload all data to GPU
    UploadToGPU(*data);

    LOG_INFO("[VoxelSceneCacher::Create] Scene data created: " + std::to_string(data->nodeCount) + " nodes, " + std::to_string(data->brickCount) + " bricks, " + std::to_string(data->solidVoxelCount) + " voxels, " + std::to_string(data->totalMemorySize / 1024.0f / 1024.0f) + " MB GPU");

    return data;
}

std::uint64_t VoxelSceneCacher::ComputeKey(const VoxelSceneCreateInfo& ci) const {
    return ci.ComputeHash();
}

void VoxelSceneCacher::Cleanup() {
    LOG_INFO("[VoxelSceneCacher::Cleanup] Cleaning up cached scene data");

    // Cleanup all cached entries
    for (auto& [key, entry] : m_entries) {
        if (entry.resource) {
            entry.resource->Cleanup(m_device->device);
        }
    }

    // Note: BatchedUploader now owned by VulkanDevice - no cleanup needed here

    // Clear temporary build data
    m_cachedGrid.reset();
    m_voxelWorld.reset();
    m_octree.reset();

    // Clear entries
    Clear();

    LOG_INFO("[VoxelSceneCacher::Cleanup] Cleanup complete");
}

// ============================================================================
// SERIALIZATION - Persist CPU-side scene data to disk
// ============================================================================

// File format version - increment when format changes
static constexpr uint32_t VOXEL_SCENE_CACHE_VERSION = 1;
static constexpr uint32_t VOXEL_SCENE_CACHE_MAGIC = 0x56534341; // "VSCA"

// Helper to write a vector to file
template<typename T>
static void WriteVector(std::ofstream& out, const std::vector<T>& vec) {
    uint64_t size = vec.size();
    out.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (size > 0) {
        out.write(reinterpret_cast<const char*>(vec.data()), size * sizeof(T));
    }
}

// Helper to read a vector from file
template<typename T>
static bool ReadVector(std::ifstream& in, std::vector<T>& vec) {
    uint64_t size = 0;
    in.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!in) return false;

    vec.resize(size);
    if (size > 0) {
        in.read(reinterpret_cast<char*>(vec.data()), size * sizeof(T));
        if (!in) return false;
    }
    return true;
}

bool VoxelSceneCacher::SerializeToFile(const std::filesystem::path& path) const {
    std::lock_guard lock(m_lock);

    if (m_entries.empty()) {
        LOG_INFO("[VoxelSceneCacher::SerializeToFile] No entries to serialize");
        return true;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        LOG_ERROR("[VoxelSceneCacher::SerializeToFile] Failed to open file: " + path.string());
        return false;
    }

    // Write header
    out.write(reinterpret_cast<const char*>(&VOXEL_SCENE_CACHE_MAGIC), sizeof(VOXEL_SCENE_CACHE_MAGIC));
    out.write(reinterpret_cast<const char*>(&VOXEL_SCENE_CACHE_VERSION), sizeof(VOXEL_SCENE_CACHE_VERSION));

    uint32_t entryCount = static_cast<uint32_t>(m_entries.size());
    out.write(reinterpret_cast<const char*>(&entryCount), sizeof(entryCount));

    LOG_INFO("[VoxelSceneCacher::SerializeToFile] Serializing " + std::to_string(entryCount) + " scene entries to " + path.string());

    // Write each entry
    for (const auto& [key, entry] : m_entries) {
        const auto& ci = entry.ci;
        const auto& data = entry.resource;

        // Write key (for validation on load)
        out.write(reinterpret_cast<const char*>(&key), sizeof(key));

        // Write CreateInfo
        out.write(reinterpret_cast<const char*>(&ci.sceneType), sizeof(ci.sceneType));
        out.write(reinterpret_cast<const char*>(&ci.resolution), sizeof(ci.resolution));
        out.write(reinterpret_cast<const char*>(&ci.density), sizeof(ci.density));
        out.write(reinterpret_cast<const char*>(&ci.seed), sizeof(ci.seed));

        // Write CPU data vectors
        WriteVector(out, data->esvoNodesCPU);
        WriteVector(out, data->brickDataCPU);
        WriteVector(out, data->materialsCPU);
        WriteVector(out, data->compressedColorsCPU);
        WriteVector(out, data->compressedNormalsCPU);
        WriteVector(out, data->brickGridLookupCPU);

        // Write OctreeConfig (fixed-size struct)
        out.write(reinterpret_cast<const char*>(&data->configCPU), sizeof(OctreeConfig));

        // Write metadata
        out.write(reinterpret_cast<const char*>(&data->nodeCount), sizeof(data->nodeCount));
        out.write(reinterpret_cast<const char*>(&data->brickCount), sizeof(data->brickCount));
        out.write(reinterpret_cast<const char*>(&data->solidVoxelCount), sizeof(data->solidVoxelCount));
        out.write(reinterpret_cast<const char*>(&data->resolution), sizeof(data->resolution));
        out.write(reinterpret_cast<const char*>(&data->sceneType), sizeof(data->sceneType));
    }

    LOG_INFO("[VoxelSceneCacher::SerializeToFile] Serialization complete");
    return out.good();
}

bool VoxelSceneCacher::DeserializeFromFile(const std::filesystem::path& path, void* devicePtr) {
    if (!std::filesystem::exists(path)) {
        LOG_INFO("[VoxelSceneCacher::DeserializeFromFile] Cache file not found: " + path.string());
        return true; // Not an error - just no cached data
    }

    auto* vulkanDevice = static_cast<Vixen::Vulkan::Resources::VulkanDevice*>(devicePtr);
    if (!vulkanDevice) {
        LOG_ERROR("[VoxelSceneCacher::DeserializeFromFile] Invalid device pointer");
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LOG_ERROR("[VoxelSceneCacher::DeserializeFromFile] Failed to open file: " + path.string());
        return false;
    }

    // Read and validate header
    uint32_t magic = 0, version = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));

    if (magic != VOXEL_SCENE_CACHE_MAGIC) {
        LOG_ERROR("[VoxelSceneCacher::DeserializeFromFile] Invalid magic number");
        return false;
    }

    if (version != VOXEL_SCENE_CACHE_VERSION) {
        LOG_INFO("[VoxelSceneCacher::DeserializeFromFile] Version mismatch (got " + std::to_string(version) + ", expected " + std::to_string(VOXEL_SCENE_CACHE_VERSION) + "), regenerating");
        return true; // Stale cache - will regenerate on demand
    }

    uint32_t entryCount = 0;
    in.read(reinterpret_cast<char*>(&entryCount), sizeof(entryCount));

    LOG_INFO("[VoxelSceneCacher::DeserializeFromFile] Loading " + std::to_string(entryCount) + " scene entries from " + path.string());

    std::lock_guard lock(m_lock);

    for (uint32_t i = 0; i < entryCount; ++i) {
        uint64_t key = 0;
        in.read(reinterpret_cast<char*>(&key), sizeof(key));

        // Read CreateInfo
        VoxelSceneCreateInfo ci;
        in.read(reinterpret_cast<char*>(&ci.sceneType), sizeof(ci.sceneType));
        in.read(reinterpret_cast<char*>(&ci.resolution), sizeof(ci.resolution));
        in.read(reinterpret_cast<char*>(&ci.density), sizeof(ci.density));
        in.read(reinterpret_cast<char*>(&ci.seed), sizeof(ci.seed));

        // Validate key matches computed hash
        if (ci.ComputeHash() != key) {
            LOG_ERROR("[VoxelSceneCacher::DeserializeFromFile] Key mismatch for entry " + std::to_string(i));
            return false;
        }

        // Create scene data and read CPU vectors
        auto data = std::make_shared<VoxelSceneData>();

        if (!ReadVector(in, data->esvoNodesCPU)) return false;
        if (!ReadVector(in, data->brickDataCPU)) return false;
        if (!ReadVector(in, data->materialsCPU)) return false;
        if (!ReadVector(in, data->compressedColorsCPU)) return false;
        if (!ReadVector(in, data->compressedNormalsCPU)) return false;
        if (!ReadVector(in, data->brickGridLookupCPU)) return false;

        // Read OctreeConfig
        in.read(reinterpret_cast<char*>(&data->configCPU), sizeof(OctreeConfig));

        // Read metadata
        in.read(reinterpret_cast<char*>(&data->nodeCount), sizeof(data->nodeCount));
        in.read(reinterpret_cast<char*>(&data->brickCount), sizeof(data->brickCount));
        in.read(reinterpret_cast<char*>(&data->solidVoxelCount), sizeof(data->solidVoxelCount));
        in.read(reinterpret_cast<char*>(&data->resolution), sizeof(data->resolution));
        in.read(reinterpret_cast<char*>(&data->sceneType), sizeof(data->sceneType));

        if (!in) {
            LOG_ERROR("[VoxelSceneCacher::DeserializeFromFile] Read error at entry " + std::to_string(i));
            return false;
        }

        // Re-upload CPU data to GPU
        LOG_INFO("[VoxelSceneCacher::DeserializeFromFile] Re-uploading entry " + std::to_string(i) + " (" + SceneTypeToString(ci.sceneType) + " @ " + std::to_string(ci.resolution) + "^3) to GPU");
        UploadToGPU(*data);

        // Store in cache
        CacheEntry entry;
        entry.key = key;
        entry.ci = ci;
        entry.resource = data;
        m_entries.emplace(key, std::move(entry));
    }

    LOG_INFO("[VoxelSceneCacher::DeserializeFromFile] Loaded " + std::to_string(entryCount) + " entries");
    return true;
}

// ============================================================================
// PRIVATE HELPER METHODS - Scene Generation
// ============================================================================

void VoxelSceneCacher::GenerateScene(const VoxelSceneCreateInfo& ci, VoxelSceneData& data) {
    LOG_INFO("[VoxelSceneCacher::GenerateScene] Generating " + SceneTypeToString(ci.sceneType) + " @ " + std::to_string(ci.resolution) + "^3");

    // Get scene type as string for factory lookup
    std::string sceneTypeName = SceneTypeToString(ci.sceneType);

    // Create generator from factory
    auto generator = SceneGeneratorFactory::Create(sceneTypeName);
    if (!generator) {
        LOG_DEBUG("[VoxelSceneCacher::GenerateScene] Unknown scene type '" + sceneTypeName + "', falling back to 'cornell'");
        generator = SceneGeneratorFactory::Create("cornell");
        if (!generator) {
            throw std::runtime_error("[VoxelSceneCacher::GenerateScene] Failed to create scene generator");
        }
    }

    // Build generation parameters
    SceneGeneratorParams params;
    params.resolution = ci.resolution;
    params.seed = ci.seed;
    params.densityThreshold = ci.density;

    // Create voxel grid and generate scene
    m_cachedGrid = std::make_unique<VoxelGrid>(ci.resolution);
    generator->Generate(*m_cachedGrid, params);

    // Count solid voxels
    data.solidVoxelCount = m_cachedGrid->CountSolidVoxels();

    LOG_INFO("[VoxelSceneCacher::GenerateScene] Generated " + std::to_string(data.solidVoxelCount) + " solid voxels (" + std::to_string(m_cachedGrid->GetDensityPercent()) + "% density)");

    // Create material palette matching VoxelGridNode's palette
    // Cornell Box material palette (IDs 0-20)
    data.materialsCPU.resize(64);  // Extra space for future materials

    // Material 0: Default white diffuse
    data.materialsCPU[0] = {{0.8f, 0.8f, 0.8f}, 0.8f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 1: Red (left wall)
    data.materialsCPU[1] = {{0.75f, 0.1f, 0.1f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 2: Green (right wall)
    data.materialsCPU[2] = {{0.1f, 0.75f, 0.1f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 3: White (back wall)
    data.materialsCPU[3] = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 4: White (floor)
    data.materialsCPU[4] = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 5: White (ceiling)
    data.materialsCPU[5] = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 6: Light gray (checker floor)
    data.materialsCPU[6] = {{0.7f, 0.7f, 0.7f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 7: Dark gray (checker floor)
    data.materialsCPU[7] = {{0.3f, 0.3f, 0.3f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 8-9: Reserved
    data.materialsCPU[8] = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    data.materialsCPU[9] = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 10: Left cube (beige diffuse)
    data.materialsCPU[10] = {{0.8f, 0.7f, 0.5f}, 0.8f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 11: Right cube (light blue)
    data.materialsCPU[11] = {{0.4f, 0.6f, 0.8f}, 0.7f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 12-18: Reserved
    for (uint32_t i = 12; i < 19; ++i) {
        data.materialsCPU[i] = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    }
    // Material 19: Debug marker (bright magenta)
    data.materialsCPU[19] = {{1.0f, 0.0f, 1.0f}, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 20: Ceiling light (emissive white)
    data.materialsCPU[20] = {{1.0f, 1.0f, 0.9f}, 0.0f, 0.0f, 5.0f, {0.0f, 0.0f}};

    // Fill remaining materials with defaults
    for (size_t i = 21; i < data.materialsCPU.size(); ++i) {
        data.materialsCPU[i] = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    }

    // Noise/Tunnel scene materials (30-40)
    if (ci.sceneType == SceneType::Noise || ci.sceneType == SceneType::Tunnels) {
        data.materialsCPU[30] = {{0.6f, 0.5f, 0.4f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};  // Stone
        data.materialsCPU[31] = {{0.4f, 0.3f, 0.2f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};  // Dark stone
        data.materialsCPU[32] = {{0.3f, 0.6f, 0.3f}, 0.8f, 0.0f, 0.0f, {0.0f, 0.0f}};  // Moss
    }

    // Cityscape materials (50-61)
    if (ci.sceneType == SceneType::Cityscape) {
        data.materialsCPU[50] = {{0.4f, 0.4f, 0.5f}, 0.8f, 0.0f, 0.0f, {0.0f, 0.0f}};  // Concrete
        data.materialsCPU[51] = {{0.3f, 0.3f, 0.4f}, 0.7f, 0.0f, 0.0f, {0.0f, 0.0f}};  // Dark building
        data.materialsCPU[52] = {{0.5f, 0.5f, 0.6f}, 0.6f, 0.2f, 0.0f, {0.0f, 0.0f}}; // Glass
        data.materialsCPU[53] = {{0.2f, 0.2f, 0.2f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};  // Asphalt
        data.materialsCPU[54] = {{1.0f, 0.9f, 0.5f}, 0.0f, 0.0f, 2.0f, {0.0f, 0.0f}}; // Window light
    }
}

// ============================================================================
// PRIVATE HELPER METHODS - Octree Building
// ============================================================================

void VoxelSceneCacher::BuildOctree(VoxelSceneData& data) {
    LOG_INFO("[VoxelSceneCacher::BuildOctree] Building ESVO octree...");

    if (!m_cachedGrid) {
        throw std::runtime_error("[VoxelSceneCacher::BuildOctree] No cached grid - call GenerateScene first");
    }

    const uint32_t resolution = data.resolution;
    const auto& gridData = m_cachedGrid->GetData();

    // Create GaiaVoxelWorld to store voxel entities
    m_voxelWorld = std::make_unique<GaiaVoxelWorld>();

    // Two-pass entity creation to avoid dangling pointers:
    // Pass 1: Count solid voxels
    size_t solidCount = 0;
    for (size_t i = 0; i < gridData.size(); ++i) {
        if (gridData[i] != 0) solidCount++;
    }

    // Pre-allocate storage
    std::vector<VoxelCreationRequest> requests;
    std::vector<ComponentQueryRequest> componentStorage;
    requests.reserve(solidCount);
    componentStorage.resize(solidCount);

    // Pass 2: Fill creation requests
    size_t voxelIdx = 0;
    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                size_t idx = static_cast<size_t>(z) * resolution * resolution +
                             static_cast<size_t>(y) * resolution + x;
                if (gridData[idx] != 0) {
                    glm::vec3 pos(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));

                    // Store Material component in pre-allocated storage
                    componentStorage[voxelIdx] = Material{gridData[idx]};

                    // Create request with span pointing to stable address
                    requests.push_back(VoxelCreationRequest{
                        pos,
                        std::span<const ComponentQueryRequest>(&componentStorage[voxelIdx], 1)
                    });
                    voxelIdx++;
                }
            }
        }
    }

    LOG_INFO("[VoxelSceneCacher::BuildOctree] Creating " + std::to_string(requests.size()) + " voxel entities...");

    // Batch create entities
    constexpr size_t BATCH_SIZE = 10000;
    size_t processed = 0;
    while (processed < requests.size()) {
        size_t batchEnd = std::min(processed + BATCH_SIZE, requests.size());
        std::span<const VoxelCreationRequest> batch(requests.data() + processed, batchEnd - processed);
        m_voxelWorld->createVoxelsBatch(batch);
        processed = batchEnd;
    }

    // Create LaineKarrasOctree and rebuild
    glm::vec3 worldMin(0.0f);
    glm::vec3 worldMax(static_cast<float>(resolution));

    // Calculate appropriate maxLevels based on resolution
    int brickDepth = 3;  // 8x8x8 bricks
    int resolutionLevels = static_cast<int>(std::ceil(std::log2(resolution)));
    int maxLevels = resolutionLevels;

    LOG_INFO("[VoxelSceneCacher::BuildOctree] Creating LaineKarrasOctree: maxLevels=" + std::to_string(maxLevels) + ", brickDepth=" + std::to_string(brickDepth));

    m_octree = std::make_unique<Vixen::SVO::LaineKarrasOctree>(*m_voxelWorld, nullptr, maxLevels, brickDepth);
    m_octree->rebuild(*m_voxelWorld, worldMin, worldMax);

    // Get octree data
    const auto* octreeData = m_octree->getOctree();
    if (!octreeData || !octreeData->root) {
        throw std::runtime_error("[VoxelSceneCacher::BuildOctree] Failed to build LaineKarras octree");
    }

    // Copy ESVO nodes to CPU buffer
    const auto& childDescriptors = octreeData->root->childDescriptors;
    data.esvoNodesCPU.resize(childDescriptors.size() * sizeof(Vixen::SVO::ChildDescriptor));
    std::memcpy(data.esvoNodesCPU.data(), childDescriptors.data(), data.esvoNodesCPU.size());
    data.nodeCount = static_cast<uint32_t>(childDescriptors.size());

    // Build sparse brick data from brickViews
    const auto& brickViews = octreeData->root->brickViews;
    const size_t voxelsPerBrick = 512;  // 8^3
    const int brickSideLength = octreeData->brickSideLength;

    std::vector<uint32_t> sparseBrickData;
    sparseBrickData.reserve(brickViews.size() * voxelsPerBrick);

    for (size_t viewIdx = 0; viewIdx < brickViews.size(); ++viewIdx) {
        const auto& view = brickViews[viewIdx];
        const glm::ivec3 gridOrigin = view.getLocalGridOrigin();

        // Extract voxel data for this brick directly from grid
        for (int bz = 0; bz < brickSideLength; ++bz) {
            for (int by = 0; by < brickSideLength; ++by) {
                for (int bx = 0; bx < brickSideLength; ++bx) {
                    int worldX = gridOrigin.x + bx;
                    int worldY = gridOrigin.y + by;
                    int worldZ = gridOrigin.z + bz;

                    uint32_t materialId = 0;
                    if (worldX >= 0 && worldX < static_cast<int>(resolution) &&
                        worldY >= 0 && worldY < static_cast<int>(resolution) &&
                        worldZ >= 0 && worldZ < static_cast<int>(resolution)) {
                        size_t gridIdx = static_cast<size_t>(worldZ) * resolution * resolution +
                                         static_cast<size_t>(worldY) * resolution + worldX;
                        materialId = static_cast<uint32_t>(gridData[gridIdx]);
                    }
                    sparseBrickData.push_back(materialId);
                }
            }
        }
    }

    // Copy brick data to CPU buffer
    data.brickDataCPU.resize(sparseBrickData.size() * sizeof(uint32_t));
    std::memcpy(data.brickDataCPU.data(), sparseBrickData.data(), data.brickDataCPU.size());
    data.brickCount = static_cast<uint32_t>(brickViews.size());

    // Setup OctreeConfig - zero-initialize first to ensure padding fields
    // are zero (Release builds don't zero-initialize, causing GPU hangs from garbage UBO data)
    std::memset(&data.configCPU, 0, sizeof(OctreeConfig));

    data.configCPU.esvoMaxScale = 22;
    data.configCPU.userMaxLevels = maxLevels;
    data.configCPU.brickDepthLevels = brickDepth;
    data.configCPU.brickSize = 1 << brickDepth;

    // Derived scale values
    data.configCPU.minESVOScale = data.configCPU.esvoMaxScale - data.configCPU.userMaxLevels + 1;
    int brickUserScale = data.configCPU.userMaxLevels - data.configCPU.brickDepthLevels;
    data.configCPU.brickESVOScale = data.configCPU.esvoMaxScale - (data.configCPU.userMaxLevels - 1 - brickUserScale);
    data.configCPU.bricksPerAxis = octreeData->bricksPerAxis;

    // World grid size
    constexpr float WORLD_GRID_SIZE = 10.0f;
    data.configCPU.worldGridSize = WORLD_GRID_SIZE;

    // Grid bounds
    data.configCPU.gridMinX = 0.0f;
    data.configCPU.gridMinY = 0.0f;
    data.configCPU.gridMinZ = 0.0f;
    data.configCPU.gridMaxX = static_cast<float>(resolution);
    data.configCPU.gridMaxY = static_cast<float>(resolution);
    data.configCPU.gridMaxZ = static_cast<float>(resolution);

    // Coordinate transformations
    glm::vec3 gridScale(WORLD_GRID_SIZE);
    glm::vec3 gridTranslation(0.0f);

    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), gridScale);
    glm::mat4 translateMat = glm::translate(glm::mat4(1.0f), gridTranslation);

    data.configCPU.localToWorld = translateMat * scaleMat;
    data.configCPU.worldToLocal = glm::inverse(data.configCPU.localToWorld);

    LOG_INFO("[VoxelSceneCacher::BuildOctree] Built ESVO octree: " + std::to_string(data.nodeCount) + " nodes, " + std::to_string(data.brickCount) + " bricks");
}

// ============================================================================
// PRIVATE HELPER METHODS - Data Compression
// ============================================================================

void VoxelSceneCacher::CompressData(VoxelSceneData& data) {
    LOG_INFO("[VoxelSceneCacher::CompressData] Compressing colors/normals...");

    if (!m_octree) {
        LOG_DEBUG("[VoxelSceneCacher::CompressData] No octree - skipping compression");
        return;
    }

    if (!m_octree->hasCompressedData()) {
        LOG_DEBUG("[VoxelSceneCacher::CompressData] Octree has no compressed data");
        return;
    }

    // Copy compressed color data
    size_t colorSize = m_octree->getCompressedColorSize();
    if (colorSize > 0) {
        const uint64_t* colorData = m_octree->getCompressedColorData();
        data.compressedColorsCPU.resize(colorSize);
        std::memcpy(data.compressedColorsCPU.data(), colorData, colorSize);
        LOG_DEBUG("[VoxelSceneCacher::CompressData] Copied " + std::to_string(colorSize) + " bytes compressed colors");
    }

    // Copy compressed normal data
    size_t normalSize = m_octree->getCompressedNormalSize();
    if (normalSize > 0) {
        const void* normalData = m_octree->getCompressedNormalData();
        data.compressedNormalsCPU.resize(normalSize);
        std::memcpy(data.compressedNormalsCPU.data(), normalData, normalSize);
        LOG_DEBUG("[VoxelSceneCacher::CompressData] Copied " + std::to_string(normalSize) + " bytes compressed normals");
    }
}

// ============================================================================
// PRIVATE HELPER METHODS - Brick Grid Lookup
// ============================================================================

void VoxelSceneCacher::BuildBrickGridLookup(VoxelSceneData& data) {
    LOG_INFO("[VoxelSceneCacher::BuildBrickGridLookup] Building brick lookup table...");

    const uint32_t brickSize = 8;  // 8x8x8 bricks
    const uint32_t bricksPerAxis = data.resolution / brickSize;
    const uint32_t totalGridSlots = bricksPerAxis * bricksPerAxis * bricksPerAxis;

    // Initialize all slots to 0xFFFFFFFF (empty)
    data.brickGridLookupCPU.resize(totalGridSlots, 0xFFFFFFFF);

    if (!m_octree) {
        LOG_DEBUG("[VoxelSceneCacher::BuildBrickGridLookup] No octree - creating empty lookup");
        return;
    }

    const auto* octreeData = m_octree->getOctree();
    if (!octreeData || !octreeData->root) {
        LOG_DEBUG("[VoxelSceneCacher::BuildBrickGridLookup] No octree root - creating empty lookup");
        return;
    }

    // Populate from brickGridToBrickView mapping
    const auto& brickGridToBrickView = octreeData->root->brickGridToBrickView;
    size_t populatedCount = 0;

    for (const auto& [key, brickIdx] : brickGridToBrickView) {
        // Decode grid key: brickX | (brickY << 10) | (brickZ << 20)
        uint32_t brickX = key & 0x3FF;
        uint32_t brickY = (key >> 10) & 0x3FF;
        uint32_t brickZ = (key >> 20) & 0x3FF;

        // Linear index into lookup buffer (XYZ order matching shader access)
        uint32_t linearIdx = brickX +
                            brickY * bricksPerAxis +
                            brickZ * bricksPerAxis * bricksPerAxis;

        if (linearIdx < totalGridSlots) {
            data.brickGridLookupCPU[linearIdx] = brickIdx;
            populatedCount++;
        }
    }

    LOG_INFO("[VoxelSceneCacher::BuildBrickGridLookup] Populated " + std::to_string(populatedCount) + " / " + std::to_string(totalGridSlots) + " slots");
}

// ============================================================================
// PRIVATE HELPER METHODS - GPU Upload
// ============================================================================

void VoxelSceneCacher::UploadToGPU(VoxelSceneData& data) {
    LOG_INFO("[VoxelSceneCacher::UploadToGPU] Uploading data to GPU...");

    if (!m_device || !m_device->device) {
        throw std::runtime_error("[VoxelSceneCacher::UploadToGPU] Device not initialized");
    }

    // Calculate buffer sizes
    data.esvoNodesSize = data.esvoNodesCPU.size();
    data.brickDataSize = data.brickDataCPU.size();
    data.materialsSize = data.materialsCPU.size() * sizeof(GPUMaterial);
    data.compressedColorsSize = data.compressedColorsCPU.size();
    data.compressedNormalsSize = data.compressedNormalsCPU.size();
    data.octreeConfigSize = sizeof(OctreeConfig);
    data.brickGridLookupSize = data.brickGridLookupCPU.size() * sizeof(uint32_t);

    // Calculate total size (with alignment padding)
    auto alignSize = [](VkDeviceSize size, VkDeviceSize alignment) -> VkDeviceSize {
        return (size + alignment - 1) & ~(alignment - 1);
    };

    const VkDeviceSize alignment = 256;  // Conservative alignment for all buffer types
    VkDeviceSize totalSize = 0;

    // Only allocate buffers that have data
    if (data.esvoNodesSize > 0) totalSize += alignSize(data.esvoNodesSize, alignment);
    if (data.brickDataSize > 0) totalSize += alignSize(data.brickDataSize, alignment);
    if (data.materialsSize > 0) totalSize += alignSize(data.materialsSize, alignment);
    if (data.compressedColorsSize > 0) totalSize += alignSize(data.compressedColorsSize, alignment);
    if (data.compressedNormalsSize > 0) totalSize += alignSize(data.compressedNormalsSize, alignment);
    if (data.octreeConfigSize > 0) totalSize += alignSize(data.octreeConfigSize, alignment);
    if (data.brickGridLookupSize > 0) totalSize += alignSize(data.brickGridLookupSize, alignment);

    if (totalSize == 0) {
        LOG_DEBUG("[VoxelSceneCacher::UploadToGPU] No data to upload");
        return;
    }

    data.totalMemorySize = totalSize;

    // Create all buffers
    struct BufferInfo {
        VkBuffer* buffer;
        VkDeviceSize size;
        VkBufferUsageFlags usage;
        const void* cpuData;
    };

    std::vector<BufferInfo> buffers;

    if (data.esvoNodesSize > 0) {
        buffers.push_back({&data.esvoNodesBuffer, data.esvoNodesSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          data.esvoNodesCPU.data()});
    }
    if (data.brickDataSize > 0) {
        buffers.push_back({&data.brickDataBuffer, data.brickDataSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          data.brickDataCPU.data()});
    }
    if (data.materialsSize > 0) {
        buffers.push_back({&data.materialsBuffer, data.materialsSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          data.materialsCPU.data()});
    }
    if (data.compressedColorsSize > 0) {
        buffers.push_back({&data.compressedColorsBuffer, data.compressedColorsSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          data.compressedColorsCPU.data()});
    }
    if (data.compressedNormalsSize > 0) {
        buffers.push_back({&data.compressedNormalsBuffer, data.compressedNormalsSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          data.compressedNormalsCPU.data()});
    }
    if (data.octreeConfigSize > 0) {
        buffers.push_back({&data.octreeConfigBuffer, data.octreeConfigSize,
                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          &data.configCPU});
    }
    if (data.brickGridLookupSize > 0) {
        // Note: TRANSFER_SRC_BIT needed for VoxelAABBConverterNode::DownloadBufferToHost()
        buffers.push_back({&data.brickGridLookupBuffer, data.brickGridLookupSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          data.brickGridLookupCPU.data()});
    }

    // Create buffers and collect memory requirements
    VkMemoryRequirements combinedMemReq{};
    combinedMemReq.size = 0;
    combinedMemReq.alignment = 1;
    combinedMemReq.memoryTypeBits = 0xFFFFFFFF;

    std::vector<std::pair<VkMemoryRequirements, VkDeviceSize>> bufferMemReqs;
    VkDeviceSize currentOffset = 0;

    for (auto& info : buffers) {
        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = info.size;
        bufferCreateInfo.usage = info.usage;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateBuffer(m_device->device, &bufferCreateInfo, nullptr, info.buffer);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("[VoxelSceneCacher::UploadToGPU] Failed to create buffer");
        }

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, *info.buffer, &memReq);

        // Align current offset
        currentOffset = alignSize(currentOffset, memReq.alignment);

        bufferMemReqs.push_back({memReq, currentOffset});
        currentOffset += memReq.size;

        // Combine memory requirements
        combinedMemReq.memoryTypeBits &= memReq.memoryTypeBits;
        if (memReq.alignment > combinedMemReq.alignment) {
            combinedMemReq.alignment = memReq.alignment;
        }
    }

    combinedMemReq.size = currentOffset;

    // Find device-local memory type
    uint32_t memoryTypeIndex = CacherAllocationHelpers::FindMemoryType(
        m_device->gpuMemoryProperties,
        combinedMemReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    // Allocate single memory block
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = combinedMemReq.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkResult result = vkAllocateMemory(m_device->device, &allocInfo, nullptr, &data.memory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelSceneCacher::UploadToGPU] Failed to allocate device memory");
    }

    // Bind buffers to memory at their offsets
    for (size_t i = 0; i < buffers.size(); ++i) {
        result = vkBindBufferMemory(m_device->device, *buffers[i].buffer, data.memory, bufferMemReqs[i].second);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("[VoxelSceneCacher::UploadToGPU] Failed to bind buffer memory");
        }
    }

    // Upload data via VulkanDevice (Sprint 5 Phase 2.5.3)
    // Centralized upload API hides staging/batching mechanics
    if (!m_device->HasUploadSupport()) {
        throw std::runtime_error("[VoxelSceneCacher::UploadToGPU] Upload infrastructure not configured");
    }

    // Queue all uploads (non-blocking)
    for (size_t i = 0; i < buffers.size(); ++i) {
        if (buffers[i].cpuData && buffers[i].size > 0) {
            auto handle = m_device->Upload(buffers[i].cpuData, buffers[i].size, *buffers[i].buffer, 0);
            if (handle == ResourceManagement::InvalidUploadHandle) {
                throw std::runtime_error("[VoxelSceneCacher::UploadToGPU] Failed to queue upload for buffer");
            }
        }
    }

    // Flush all queued uploads in a single batch and wait for completion
    m_device->WaitAllUploads();

    LOG_INFO("[VoxelSceneCacher::UploadToGPU] Uploaded " + std::to_string(buffers.size()) + " buffers, total " + std::to_string(combinedMemReq.size / 1024.0f) + " KB (via BatchedUploader)");
}

VkBuffer VoxelSceneCacher::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer;
    VkResult result = vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelSceneCacher::CreateBuffer] Failed to create buffer");
    }

    return buffer;
}

// NOTE: UploadBufferData removed - replaced by BatchedUploader in UploadToGPU (Sprint 5 Phase 2.5.2)

} // namespace CashSystem
