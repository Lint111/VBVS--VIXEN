#include "Nodes/VoxelGridNode.h"
#include "Data/SceneGenerator.h"
#include "Data/VoxelOctree.h" // Legacy - will be removed
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "Core/RenderGraph.h"
#include "MainCacher.h"
#include "VoxelSceneCacher.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <span>

// New SVO library integration
#include "SVOBuilder.h"
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"   // For GaiaVoxelWorld entity-based voxel storage
#include "VoxelComponents.h"  // For GaiaVoxel::Material component

using VIXEN::RenderGraph::VoxelGrid;
using VIXEN::RenderGraph::SparseVoxelOctree; // Legacy - will be removed
using VIXEN::RenderGraph::SceneGeneratorFactory;
using VIXEN::RenderGraph::SceneGeneratorParams;
using VIXEN::RenderGraph::ISceneGenerator;
using VIXEN::RenderGraph::VoxelDataCache;
using VIXEN::RenderGraph::OctreeNode; // Legacy - will be removed
using VIXEN::RenderGraph::VoxelBrick; // Legacy - will be removed

namespace Vixen::RenderGraph {

// ============================================================================
// OCTREE CONFIG STRUCT (GPU UBO layout, must match shader std140)
// ============================================================================
// Contains all configurable octree parameters - eliminates hard-coded constants in shader
// Layout: std140 requires vec3 alignment to 16 bytes, int to 4 bytes
struct OctreeConfig {
    // ESVO scale parameters (matching LaineKarrasOctree.h)
    int32_t esvoMaxScale;       // Always 22 (ESVO normalized space)
    int32_t userMaxLevels;      // log2(resolution) = 7 for 128³
    int32_t brickDepthLevels;   // 3 for 8³ bricks
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
    glm::mat4 localToWorld;     // Transform from Grid Local [0,1] to World Space, std140 layout requires 16-byte alignment
    glm::mat4 worldToLocal;     // Transform from World Space to Grid Local [0,1] , std140 layout requires 16-byte alignment

    // Padding to reach 256 bytes (std140 alignment)
    // Current size: 16 + 16 + 16 + 16 + 64 + 64 = 192 bytes
    // Needed: 256 - 192 = 64 bytes
    float _padding4[16];
};

static_assert(sizeof(OctreeConfig) == 256, "OctreeConfig must be 256 bytes for std140 alignment");

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> VoxelGridNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::unique_ptr<NodeInstance>(new VoxelGridNode(instanceName, const_cast<VoxelGridNodeType*>(this)));
}

// ============================================================================
// VOXEL GRID NODE IMPLEMENTATION
// ============================================================================

VoxelGridNode::VoxelGridNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<VoxelGridNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("VoxelGridNode constructor");
}

void VoxelGridNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[VoxelGridNode::SetupImpl] ENTERED with taskIndex=" + std::to_string(ctx.taskIndex));
    NODE_LOG_INFO("VoxelGridNode setup");

    // Read parameters
    resolution = GetParameterValue<uint32_t>(VoxelGridNodeConfig::PARAM_RESOLUTION, 128u);
    sceneType = GetParameterValue<std::string>(VoxelGridNodeConfig::PARAM_SCENE_TYPE, std::string("test"));

    NODE_LOG_INFO("Voxel grid: " + std::to_string(resolution) + "^3, scene=" + sceneType);
    NODE_LOG_DEBUG("[VoxelGridNode::SetupImpl] COMPLETED");
}

void VoxelGridNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] ENTERED with taskIndex=" + std::to_string(ctx.taskIndex));
    NODE_LOG_INFO("=== VoxelGridNode::CompileImpl START ===");

    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] Getting device...");
    // Get device
    VulkanDevice* devicePtr = ctx.In(VoxelGridNodeConfig::VULKAN_DEVICE_IN);
    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] Device ptr: " + std::to_string(reinterpret_cast<uint64_t>(devicePtr)));
    if (!devicePtr) {
        NODE_LOG_ERROR("[VoxelGridNode::CompileImpl] ERROR: Device is null!");
        throw std::runtime_error("[VoxelGridNode] VULKAN_DEVICE_IN is null");
    }

    SetDevice(devicePtr);
    vulkanDevice = devicePtr;

    // Get command pool
    commandPool = ctx.In(VoxelGridNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[VoxelGridNode] COMMAND_POOL is null");
    }

    // Create memory tracking logger for Week 3 benchmarking
    memoryLogger_ = std::make_shared<GPUPerformanceLogger>("VoxelGrid_Memory", vulkanDevice, 1);
    memoryLogger_->SetEnabled(true);
    memoryLogger_->SetTerminalOutput(true);
    if (nodeLogger) {
        nodeLogger->AddChild(memoryLogger_);
    }

    // Register VoxelSceneCacher with CashSystem (idempotent)
    RegisterVoxelSceneCacher();

    // ========================================================================
    // Create scene via cacher (the only path now)
    // ========================================================================
    if (!voxelSceneCacher_) {
        throw std::runtime_error("[VoxelGridNode] VoxelSceneCacher not registered - cannot proceed");
    }
    CreateSceneViaCacher();
    NODE_LOG_INFO("VoxelGridNode: Scene created via cacher");

#if 0  // ========================================================================
       // LEGACY MANUAL CREATION PATH - Now handled by VoxelSceneCacher
       // ========================================================================
       // This entire block is commented out because VoxelSceneCacher now handles:
       // - Scene generation (VoxelGrid creation from SceneGeneratorFactory)
       // - GaiaVoxelWorld population
       // - LaineKarrasOctree building
       // - DXT compression of colors/normals
       // - GPU buffer creation and upload
       // - OctreeConfig UBO creation
       // - Brick grid lookup buffer creation
       // ========================================================================
    {

    // Generate procedural voxel scene (using cache for performance)
    std::cout << "[VoxelGridNode] Requesting voxel grid: resolution=" << resolution << ", sceneType=" << sceneType << std::endl;

    // Setup generation parameters
    SceneGeneratorParams params;
    params.resolution = resolution;
    params.seed = 42;  // Fixed seed for reproducibility

    // Try to get cached grid, or generate fresh if not cached
    const VoxelGrid* cachedGrid = VoxelDataCache::GetOrGenerate(sceneType, resolution, params);

    // If cache returned null (disabled or error), generate fresh
    std::unique_ptr<VoxelGrid> freshGrid;
    const VoxelGrid* gridPtr = cachedGrid;
    if (!gridPtr) {
        freshGrid = std::make_unique<VoxelGrid>(resolution);
        GenerateProceduralScene(*freshGrid);
        gridPtr = freshGrid.get();
    }

    // Reference the grid (either cached or fresh)
    const VoxelGrid& grid = *gridPtr;

    size_t voxelCount = resolution * resolution * resolution;
    std::cout << "[VoxelGridNode] Using voxel grid with " << voxelCount << " voxels, density=" << grid.GetDensityPercent() << "%" << std::endl;
    NODE_LOG_INFO("Using grid with " + std::to_string(voxelCount) + " voxels, density=" +
                  std::to_string(grid.GetDensityPercent()) + "%");

    // Build sparse voxel octree using LaineKarrasOctree with GaiaVoxelWorld
    std::cout << "[VoxelGridNode] Building ESVO octree via LaineKarrasOctree..." << std::endl;

    // 1. Create GaiaVoxelWorld to store voxel entities
    GaiaVoxel::GaiaVoxelWorld voxelWorld;

    // 2. Populate voxelWorld from VoxelGrid data using batched sequential creation
    //    Note: Gaia ECS doesn't support concurrent structural changes, so we use single-threaded batching
    const auto& data = grid.GetData();

    // First pass: count solid voxels to pre-allocate exact sizes
    // CRITICAL: VoxelCreationRequest::components is a std::span (pointer+size, doesn't own data)
    // We need stable memory addresses, so we must count first and reserve EXACTLY before filling
    size_t solidCount = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] != 0) solidCount++;
    }

    // Pre-allocate EXACT size to prevent reallocation (which would invalidate spans)
    std::vector<GaiaVoxel::VoxelCreationRequest> requests;
    std::vector<GaiaVoxel::ComponentQueryRequest> componentStorage;
    requests.reserve(solidCount);
    componentStorage.resize(solidCount);  // RESIZE not reserve - allocates memory immediately

    // Second pass: fill both vectors
    size_t voxelIdx = 0;
    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                size_t idx = static_cast<size_t>(z) * resolution * resolution +
                             static_cast<size_t>(y) * resolution + x;
                if (data[idx] != 0) {
                    glm::vec3 pos(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));

                    // Store Material component in pre-allocated storage
                    componentStorage[voxelIdx] = GaiaVoxel::Material{data[idx]};

                    // Create request with span pointing to stable address in componentStorage
                    requests.push_back(GaiaVoxel::VoxelCreationRequest{
                        pos,
                        std::span<const GaiaVoxel::ComponentQueryRequest>(&componentStorage[voxelIdx], 1)
                    });

                    voxelIdx++;
                }
            }
        }
    }

    std::cout << "[VoxelGridNode] Found " << requests.size() << " solid voxels" << std::endl;

    // Process in batches for progress reporting
    constexpr size_t BATCH_SIZE = 10000;
    size_t processed = 0;
    size_t lastProgress = 0;

    while (processed < requests.size()) {
        size_t batchEnd = std::min(processed + BATCH_SIZE, requests.size());
        std::span<const GaiaVoxel::VoxelCreationRequest> batch(
            requests.data() + processed, batchEnd - processed);

        voxelWorld.createVoxelsBatch(batch);
        processed = batchEnd;

        // Progress reporting every 10%
        size_t progress = (processed * 100) / requests.size();
        if (progress >= lastProgress + 10) {
            std::cout << "[VoxelGridNode] Created: " << progress << "%" << std::endl;
            lastProgress = progress;
        }
    }

    std::cout << "[VoxelGridNode] Created " << requests.size() << " voxel entities" << std::endl;

    // 3. Create LaineKarrasOctree and rebuild from entities
    glm::vec3 worldMin(0.0f);
    glm::vec3 worldMax(static_cast<float>(resolution));

    // Calculate appropriate maxLevels based on resolution
    // For resolution 32: log2(32) = 5 levels for octree + 3 for bricks = 8
    // For resolution 64: log2(64) = 6 levels for octree + 3 for bricks = 9
    // For resolution 128: log2(128) = 7 levels for octree + 3 for bricks = 10
    int brickDepth = 3;  // 8x8x8 bricks
    int resolutionLevels = static_cast<int>(std::ceil(std::log2(resolution)));
    int maxLevels = resolutionLevels;

    std::cout << "[VoxelGridNode] Creating LaineKarrasOctree: maxLevels=" << maxLevels
              << ", brickDepth=" << brickDepth << std::endl;

    SVO::LaineKarrasOctree octree(voxelWorld, nullptr, maxLevels, brickDepth);
    octree.rebuild(voxelWorld, worldMin, worldMax);

    // 4. Get octree data for GPU upload
    const auto* octreeData = octree.getOctree();
    if (!octreeData || !octreeData->root) {
        throw std::runtime_error("[VoxelGridNode] Failed to build LaineKarras octree");
    }

    std::cout << "[VoxelGridNode] Built ESVO octree: "
              << octreeData->root->childDescriptors.size() << " nodes, "
              << octreeData->root->brickViews.size() << " bricks, "
              << "total voxels=" << octreeData->totalVoxels << ", "
              << "memory=" << (octreeData->memoryUsage / 1024.0f / 1024.0f) << " MB" << std::endl;
    NODE_LOG_INFO("Built ESVO octree: " +
                  std::to_string(octreeData->root->childDescriptors.size()) + " nodes, " +
                  std::to_string(octreeData->root->brickViews.size()) + " bricks, " +
                  "total voxels=" + std::to_string(octreeData->totalVoxels));

    // Debug: Print hierarchy structure with leaf analysis
    if (octreeData->root->childDescriptors.size() > 0) {
        std::cout << "[VoxelGridNode] DEBUG: Hierarchy structure:" << std::endl;
        std::cout << "  Total nodes: " << octreeData->root->childDescriptors.size() << std::endl;

        // Count nodes with leaves
        int nodesWithLeaves = 0;
        int totalLeafBits = 0;
        for (size_t i = 0; i < octreeData->root->childDescriptors.size(); ++i) {
            const auto& desc = octreeData->root->childDescriptors[i];
            if (desc.leafMask != 0) {
                nodesWithLeaves++;
                totalLeafBits += std::popcount(static_cast<unsigned>(desc.leafMask));
            }
        }
        std::cout << "  Nodes with leafMask!=0: " << nodesWithLeaves << std::endl;
        std::cout << "  Total leaf bits set: " << totalLeafBits << std::endl;

        // Print first few nodes with leafMask != 0
        std::cout << "  First nodes with leaves:" << std::endl;
        int printed = 0;
        for (size_t i = 0; i < octreeData->root->childDescriptors.size() && printed < 5; ++i) {
            const auto& desc = octreeData->root->childDescriptors[i];
            if (desc.leafMask != 0) {
                std::cout << "    [" << i << "] validMask=0x" << std::hex << static_cast<int>(desc.validMask)
                          << ", leafMask=0x" << static_cast<int>(desc.leafMask) << std::dec << std::endl;
                printed++;
            }
        }

        // Print root and first level
        const auto& root = octreeData->root->childDescriptors[0];
        std::cout << "  ROOT[0]: childPointer=" << root.childPointer
                  << ", validMask=0x" << std::hex << static_cast<int>(root.validMask)
                  << ", leafMask=0x" << static_cast<int>(root.leafMask) << std::dec << std::endl;
    }

    // Upload ESVO octree to GPU buffers (pass grid for direct brick data extraction)
    UploadESVOBuffers(*octreeData, grid);
    std::cout << "[VoxelGridNode] ESVO buffers uploaded successfully" << std::endl;

    // ========================================================================
    // Upload DXT Compressed Buffers (Week 3)
    // ========================================================================
    // LaineKarrasOctree::rebuild() generates compressed color/normal data.
    // These buffers are optional - only created if compressed data is available.
    if (octree.hasCompressedData()) {
        const VkPhysicalDeviceMemoryProperties& memProperties = vulkanDevice->gpuMemoryProperties;

        // Helper lambda to create and upload a buffer
        auto createAndUploadBuffer = [&](VkBuffer& buffer, VkDeviceMemory& memory,
                                        const void* data, VkDeviceSize size,
                                        const char* bufferName) {
            if (size == 0 || data == nullptr) {
                std::cout << "[VoxelGridNode] Skipping " << bufferName << " (no data)" << std::endl;
                return;
            }

            // Create buffer
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = size;
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VkResult result = vkCreateBuffer(vulkanDevice->device, &bufferInfo, nullptr, &buffer);
            if (result != VK_SUCCESS) {
                throw std::runtime_error(std::string("[VoxelGridNode] Failed to create ") + bufferName);
            }

            // Allocate device-local memory
            VkMemoryRequirements memReq;
            vkGetBufferMemoryRequirements(vulkanDevice->device, buffer, &memReq);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;

            uint32_t memoryTypeIndex = UINT32_MAX;
            for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
                if ((memReq.memoryTypeBits & (1 << i)) &&
                    (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                    memoryTypeIndex = i;
                    break;
                }
            }
            if (memoryTypeIndex == UINT32_MAX) {
                throw std::runtime_error(std::string("[VoxelGridNode] Failed to find memory for ") + bufferName);
            }
            allocInfo.memoryTypeIndex = memoryTypeIndex;

            result = vkAllocateMemory(vulkanDevice->device, &allocInfo, nullptr, &memory);
            if (result != VK_SUCCESS) {
                throw std::runtime_error(std::string("[VoxelGridNode] Failed to allocate ") + bufferName);
            }
            vkBindBufferMemory(vulkanDevice->device, buffer, memory, 0);

            // Upload via staging buffer
            VkBufferCreateInfo stagingInfo{};
            stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            stagingInfo.size = size;
            stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VkBuffer stagingBuffer;
            vkCreateBuffer(vulkanDevice->device, &stagingInfo, nullptr, &stagingBuffer);

            VkMemoryRequirements stagingMemReq;
            vkGetBufferMemoryRequirements(vulkanDevice->device, stagingBuffer, &stagingMemReq);

            VkMemoryAllocateInfo stagingAllocInfo{};
            stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            stagingAllocInfo.allocationSize = stagingMemReq.size;

            VkMemoryPropertyFlags stagingProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
                if ((stagingMemReq.memoryTypeBits & (1 << i)) &&
                    (memProperties.memoryTypes[i].propertyFlags & stagingProps) == stagingProps) {
                    stagingAllocInfo.memoryTypeIndex = i;
                    break;
                }
            }

            VkDeviceMemory stagingMemory;
            vkAllocateMemory(vulkanDevice->device, &stagingAllocInfo, nullptr, &stagingMemory);
            vkBindBufferMemory(vulkanDevice->device, stagingBuffer, stagingMemory, 0);

            void* mappedData;
            vkMapMemory(vulkanDevice->device, stagingMemory, 0, size, 0, &mappedData);
            std::memcpy(mappedData, data, size);
            vkUnmapMemory(vulkanDevice->device, stagingMemory);

            // Copy from staging to device
            VkCommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAllocInfo.commandPool = commandPool;
            cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAllocInfo.commandBufferCount = 1;

            VkCommandBuffer cmdBuffer;
            vkAllocateCommandBuffers(vulkanDevice->device, &cmdAllocInfo, &cmdBuffer);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(cmdBuffer, &beginInfo);
            VkBufferCopy copyRegion{};
            copyRegion.size = size;
            vkCmdCopyBuffer(cmdBuffer, stagingBuffer, buffer, 1, &copyRegion);
            vkEndCommandBuffer(cmdBuffer);

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmdBuffer;

            vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(vulkanDevice->queue);

            vkFreeCommandBuffers(vulkanDevice->device, commandPool, 1, &cmdBuffer);
            vkDestroyBuffer(vulkanDevice->device, stagingBuffer, nullptr);
            vkFreeMemory(vulkanDevice->device, stagingMemory, nullptr);

            std::cout << "[VoxelGridNode] Uploaded " << bufferName << " (" << size << " bytes)" << std::endl;
        };

        // Upload compressed color buffer (binding 7)
        VkDeviceSize colorCompressedSize = octree.getCompressedColorSize();
        createAndUploadBuffer(compressedColorBuffer, compressedColorMemory,
                             octree.getCompressedColorData(),
                             colorCompressedSize,
                             "compressedColorBuffer");

        // Upload compressed normal buffer (binding 8)
        VkDeviceSize normalCompressedSize = octree.getCompressedNormalSize();
        createAndUploadBuffer(compressedNormalBuffer, compressedNormalMemory,
                             octree.getCompressedNormalData(),
                             normalCompressedSize,
                             "compressedNormalBuffer");

        // Register compressed buffers for memory tracking (Week 3 benchmarking)
        if (memoryLogger_) {
            if (colorCompressedSize > 0) {
                memoryLogger_->RegisterBufferAllocation("CompressedColors (DXT1)", colorCompressedSize);
            }
            if (normalCompressedSize > 0) {
                memoryLogger_->RegisterBufferAllocation("CompressedNormals (DXT)", normalCompressedSize);
            }
        }

        std::cout << "[VoxelGridNode] DXT compressed buffers uploaded ("
                  << octree.getCompressedBrickCount() << " bricks)" << std::endl;

        // ====================================================================
        // Create and upload brick grid lookup buffer
        // Maps (brickX, brickY, brickZ) -> brickIndex for hardware RT
        // ====================================================================
        {
            const uint32_t bricksPerAxis = octreeData->bricksPerAxis;
            const uint32_t totalGridSlots = bricksPerAxis * bricksPerAxis * bricksPerAxis;
            const VkDeviceSize lookupBufferSize = totalGridSlots * sizeof(uint32_t);

            // Build lookup table from brickGridToBrickView mapping
            std::vector<uint32_t> lookupData(totalGridSlots, 0xFFFFFFFF);  // 0xFFFFFFFF = empty

            for (const auto& [key, brickIdx] : octreeData->root->brickGridToBrickView) {
                // Decode grid key: brickX | (brickY << 10) | (brickZ << 20)
                uint32_t brickX = key & 0x3FF;
                uint32_t brickY = (key >> 10) & 0x3FF;
                uint32_t brickZ = (key >> 20) & 0x3FF;

                // Linear index into lookup buffer (XYZ order matching shader access)
                uint32_t linearIdx = brickX +
                                    brickY * bricksPerAxis +
                                    brickZ * bricksPerAxis * bricksPerAxis;

                if (linearIdx < totalGridSlots) {
                    lookupData[linearIdx] = brickIdx;
                }
            }

            createAndUploadBuffer(brickGridLookupBuffer, brickGridLookupMemory,
                                 lookupData.data(), lookupBufferSize,
                                 "brickGridLookupBuffer");

            std::cout << "[VoxelGridNode] Brick grid lookup buffer: "
                      << totalGridSlots << " slots (" << lookupBufferSize << " bytes), "
                      << octreeData->root->brickGridToBrickView.size() << " populated bricks"
                      << std::endl;
        }
    } else {
        std::cout << "[VoxelGridNode] No compressed data available (compression not enabled)" << std::endl;
    }

    // Create and upload OctreeConfig UBO with scale parameters
    {
        OctreeConfig config{};
        config.esvoMaxScale = 22;  // ESVO_MAX_SCALE constant (always 22)
        config.userMaxLevels = maxLevels;
        config.brickDepthLevels = brickDepth;
        config.brickSize = 1 << brickDepth;  // 2^3 = 8

        // Derived scale values (matching LaineKarrasOctree formulas)
        config.minESVOScale = config.esvoMaxScale - config.userMaxLevels + 1;  // 22 - 7 + 1 = 16

        // brickESVOScale = scale where userScale == brickUserScale
        // brickUserScale = maxLevels - brickDepthLevels = 7 - 3 = 4
        // esvoScale = esvoMaxScale - (maxLevels - 1 - userScale) = 22 - (7 - 1 - 4) = 22 - 2 = 20
        int brickUserScale = config.userMaxLevels - config.brickDepthLevels;
        config.brickESVOScale = config.esvoMaxScale - (config.userMaxLevels - 1 - brickUserScale);  // 20

        config.bricksPerAxis = octreeData->bricksPerAxis;

        // Grid bounds
        config.gridMinX = 0.0f;
        config.gridMinY = 0.0f;
        config.gridMinZ = 0.0f;
        config.gridMaxX = static_cast<float>(resolution);
        config.gridMaxY = static_cast<float>(resolution);
        config.gridMaxZ = static_cast<float>(resolution);

        // Compute Transformation Matrices
        // Define the grid's transform in world space
        // TEST: Use fixed 10x10x10 world size instead of resolution-based
        // This makes the grid smaller in world space for easier camera navigation
        // Local Space is defined as [0, 1] unit cube
        constexpr float WORLD_GRID_SIZE = 10.0f;  // World units per axis
        glm::vec3 gridScale(WORLD_GRID_SIZE);
        glm::vec3 gridTranslation(0.0f);
        glm::vec3 gridRotation(0.0f); // Euler angles if needed

        // Model Matrix: Local [0,1] -> World
        // Scale to fixed world size (not resolution-based)
        glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), gridScale);
        glm::mat4 translateMat = glm::translate(glm::mat4(1.0f), gridTranslation);
        // Rotation would go here

        config.localToWorld = translateMat * scaleMat;
        config.worldToLocal = glm::inverse(config.localToWorld);

        std::cout << "[VoxelGridNode] World grid size: " << WORLD_GRID_SIZE << "^3 units" << std::endl;

        std::cout << "[VoxelGridNode] OctreeConfig: esvoMaxScale=" << config.esvoMaxScale
                  << ", userMaxLevels=" << config.userMaxLevels
                  << ", brickDepthLevels=" << config.brickDepthLevels
                  << ", minESVOScale=" << config.minESVOScale
                  << ", brickESVOScale=" << config.brickESVOScale
                  << ", bricksPerAxis=" << config.bricksPerAxis << std::endl;

        // Create UBO buffer
        VkBufferCreateInfo configBufferInfo{};
        configBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        configBufferInfo.size = sizeof(OctreeConfig);
        configBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        configBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateBuffer(vulkanDevice->device, &configBufferInfo, nullptr, &octreeConfigBuffer);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("[VoxelGridNode] Failed to create octree config buffer");
        }

        // Allocate device-local memory
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(vulkanDevice->device, octreeConfigBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;

        const VkPhysicalDeviceMemoryProperties& memProperties = vulkanDevice->gpuMemoryProperties;
        uint32_t memoryTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((memReq.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memoryTypeIndex = i;
                break;
            }
        }
        if (memoryTypeIndex == UINT32_MAX) {
            throw std::runtime_error("[VoxelGridNode] Failed to find device-local memory for config buffer");
        }
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        result = vkAllocateMemory(vulkanDevice->device, &allocInfo, nullptr, &octreeConfigMemory);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("[VoxelGridNode] Failed to allocate config buffer memory");
        }
        vkBindBufferMemory(vulkanDevice->device, octreeConfigBuffer, octreeConfigMemory, 0);

        // Upload via staging buffer
        VkBufferCreateInfo stagingInfo{};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = sizeof(OctreeConfig);
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer stagingBuffer;
        vkCreateBuffer(vulkanDevice->device, &stagingInfo, nullptr, &stagingBuffer);

        VkMemoryRequirements stagingMemReq;
        vkGetBufferMemoryRequirements(vulkanDevice->device, stagingBuffer, &stagingMemReq);

        VkMemoryAllocateInfo stagingAllocInfo{};
        stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        stagingAllocInfo.allocationSize = stagingMemReq.size;

        VkMemoryPropertyFlags stagingProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((stagingMemReq.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & stagingProps) == stagingProps) {
                stagingAllocInfo.memoryTypeIndex = i;
                break;
            }
        }

        VkDeviceMemory stagingMemory;
        vkAllocateMemory(vulkanDevice->device, &stagingAllocInfo, nullptr, &stagingMemory);
        vkBindBufferMemory(vulkanDevice->device, stagingBuffer, stagingMemory, 0);

        void* data;
        vkMapMemory(vulkanDevice->device, stagingMemory, 0, sizeof(OctreeConfig), 0, &data);
        std::memcpy(data, &config, sizeof(OctreeConfig));
        vkUnmapMemory(vulkanDevice->device, stagingMemory);

        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        vkAllocateCommandBuffers(vulkanDevice->device, &cmdAllocInfo, &cmdBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuffer, &beginInfo);
        VkBufferCopy copyRegion{};
        copyRegion.size = sizeof(OctreeConfig);
        vkCmdCopyBuffer(cmdBuffer, stagingBuffer, octreeConfigBuffer, 1, &copyRegion);
        vkEndCommandBuffer(cmdBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vulkanDevice->queue);

        vkFreeCommandBuffers(vulkanDevice->device, commandPool, 1, &cmdBuffer);
        vkDestroyBuffer(vulkanDevice->device, stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice->device, stagingMemory, nullptr);

        std::cout << "[VoxelGridNode] OctreeConfig UBO uploaded (" << sizeof(OctreeConfig) << " bytes)" << std::endl;
    }

    } // End of legacy path block
#endif // LEGACY MANUAL CREATION PATH

    // ========================================================================
    // Debug capture buffer (used by cacher path)
    // ========================================================================

    // Create ray trace buffer for per-ray traversal capture
    // Each ray captures up to 64 steps * 48 bytes + 16 byte header = 3088 bytes/ray
    // 256 rays = ~790KB buffer, reasonable for debug capture
    constexpr uint32_t RAY_TRACE_CAPACITY = 256;
    constexpr uint32_t DEBUG_BINDING_INDEX = 4;  // Matches shader binding 4
    debugCaptureResource_ = std::make_unique<Debug::DebugCaptureResource>(
        vulkanDevice->device,
        *vulkanDevice->gpu,  // VulkanDevice stores VkPhysicalDevice* as 'gpu'
        RAY_TRACE_CAPACITY,
        "RayTraversal",
        DEBUG_BINDING_INDEX,
        true  // hostVisible for direct readback
    );

    if (!debugCaptureResource_->IsValid()) {
        NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] WARNING: Failed to create ray trace buffer");
    } else {
        NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] Created ray trace buffer: " +
                      std::to_string(RAY_TRACE_CAPACITY) + " rays, buffer=" +
                      std::to_string(reinterpret_cast<uint64_t>(debugCaptureResource_->GetBuffer())));
    }

    // Output resources
    std::cout << "!!!! [VoxelGridNode::CompileImpl] OUTPUTTING NEW RESOURCES !!!!" << std::endl;
    std::cout << "  NEW octreeNodesBuffer=" << octreeNodesBuffer << ", octreeBricksBuffer=" << octreeBricksBuffer
              << ", octreeMaterialsBuffer=" << octreeMaterialsBuffer << ", octreeConfigBuffer=" << octreeConfigBuffer << std::endl;

    // Output octree buffers
    ctx.Out(VoxelGridNodeConfig::OCTREE_NODES_BUFFER, octreeNodesBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_BRICKS_BUFFER, octreeBricksBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_MATERIALS_BUFFER, octreeMaterialsBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_CONFIG_BUFFER, octreeConfigBuffer);

    // Output compressed buffers (optional - only if compression is enabled)
    if (compressedColorBuffer != VK_NULL_HANDLE) {
        ctx.Out(VoxelGridNodeConfig::COMPRESSED_COLOR_BUFFER, compressedColorBuffer);
        std::cout << "  COMPRESSED_COLOR_BUFFER=" << compressedColorBuffer << std::endl;
    }
    if (compressedNormalBuffer != VK_NULL_HANDLE) {
        ctx.Out(VoxelGridNodeConfig::COMPRESSED_NORMAL_BUFFER, compressedNormalBuffer);
        std::cout << "  COMPRESSED_NORMAL_BUFFER=" << compressedNormalBuffer << std::endl;
    }
    if (brickGridLookupBuffer != VK_NULL_HANDLE) {
        ctx.Out(VoxelGridNodeConfig::BRICK_GRID_LOOKUP_BUFFER, brickGridLookupBuffer);
        std::cout << "  BRICK_GRID_LOOKUP_BUFFER=" << brickGridLookupBuffer << std::endl;
    }

    // Output cached scene data for downstream nodes (AccelerationStructureNode)
    // This provides readonly access to the complete scene for building AS
    if (cachedSceneData_) {
        ctx.Out(VoxelGridNodeConfig::VOXEL_SCENE_DATA, cachedSceneData_.get());
        std::cout << "  VOXEL_SCENE_DATA=" << cachedSceneData_.get() << std::endl;
    }

    // Output debug capture buffer with IDebugCapture interface attached
    // When connected with SlotRole::Debug, the gatherer will auto-collect it
    if (debugCaptureResource_ && debugCaptureResource_->IsValid()) {
        ctx.OutWithInterface(VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER,
                            debugCaptureResource_->GetBuffer(),
                            static_cast<Debug::IDebugCapture*>(debugCaptureResource_.get()));
        std::cout << "  DEBUG_CAPTURE_BUFFER=" << debugCaptureResource_->GetBuffer() << std::endl;
    }

    std::cout << "!!!! [VoxelGridNode::CompileImpl] OUTPUTS SET !!!!" << std::endl;

    NODE_LOG_INFO("Uploaded octree buffers successfully");

    // Print memory summary for Week 3 benchmarking
    if (memoryLogger_) {
        std::cout << "\n========================================\n";
        std::cout << "[VoxelGridNode] GPU MEMORY SUMMARY\n";
        std::cout << "========================================\n";
        std::cout << memoryLogger_->GetMemorySummary();
        std::cout << "Total GPU Memory: " << memoryLogger_->GetTotalTrackedMemoryMB() << " MB\n";
        std::cout << "========================================\n" << std::flush;
    } else {
        std::cout << "[VoxelGridNode] WARNING: memoryLogger_ is null, cannot print memory summary\n" << std::flush;
    }

    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] COMPLETED");
}

void VoxelGridNode::ExecuteImpl(TypedExecuteContext& ctx) {
    NODE_LOG_DEBUG("[VoxelGridNode::ExecuteImpl] ENTERED with taskIndex=" + std::to_string(ctx.taskIndex));
    // Re-output persistent resources every frame for variadic connections
    // When swapchain recompiles, descriptor gatherer re-queries these outputs

    NODE_LOG_INFO("=== VoxelGridNode::ExecuteImpl START ===");
    NODE_LOG_INFO("  octreeNodesBuffer handle: " + std::to_string(reinterpret_cast<uint64_t>(octreeNodesBuffer)));
    NODE_LOG_INFO("  octreeBricksBuffer handle: " + std::to_string(reinterpret_cast<uint64_t>(octreeBricksBuffer)));
    NODE_LOG_INFO("  octreeMaterialsBuffer handle: " + std::to_string(reinterpret_cast<uint64_t>(octreeMaterialsBuffer)));

    ctx.Out(VoxelGridNodeConfig::OCTREE_NODES_BUFFER, octreeNodesBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_BRICKS_BUFFER, octreeBricksBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_MATERIALS_BUFFER, octreeMaterialsBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_CONFIG_BUFFER, octreeConfigBuffer);

    // Re-output compressed buffers (optional)
    if (compressedColorBuffer != VK_NULL_HANDLE) {
        ctx.Out(VoxelGridNodeConfig::COMPRESSED_COLOR_BUFFER, compressedColorBuffer);
    }
    if (compressedNormalBuffer != VK_NULL_HANDLE) {
        ctx.Out(VoxelGridNodeConfig::COMPRESSED_NORMAL_BUFFER, compressedNormalBuffer);
    }
    if (brickGridLookupBuffer != VK_NULL_HANDLE) {
        ctx.Out(VoxelGridNodeConfig::BRICK_GRID_LOOKUP_BUFFER, brickGridLookupBuffer);
    }

    // Re-output cached scene data for downstream nodes
    if (cachedSceneData_) {
        ctx.Out(VoxelGridNodeConfig::VOXEL_SCENE_DATA, cachedSceneData_.get());
    }

    // Re-output debug capture buffer (with interface)
    if (debugCaptureResource_ && debugCaptureResource_->IsValid()) {
        // Reset write index before each frame to allow fresh capture
        debugCaptureResource_->ResetWriteIndex();
        ctx.OutWithInterface(VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER,
                            debugCaptureResource_->GetBuffer(),
                            static_cast<Debug::IDebugCapture*>(debugCaptureResource_.get()));
    }

    NODE_LOG_INFO("=== VoxelGridNode::ExecuteImpl END ===");
    NODE_LOG_DEBUG("[VoxelGridNode::ExecuteImpl] COMPLETED");
}

void VoxelGridNode::DestroyOctreeBuffers() {
    if (!vulkanDevice) return;

    // If using cached data, just release the shared_ptr - cacher owns the resources
    if (cachedSceneData_) {
        NODE_LOG_DEBUG("VoxelGridNode: Releasing cached scene data (cacher owns resources)");
        cachedSceneData_.reset();
        // Reset all buffer handles (we don't own them)
        octreeNodesBuffer = VK_NULL_HANDLE;
        octreeBricksBuffer = VK_NULL_HANDLE;
        octreeMaterialsBuffer = VK_NULL_HANDLE;
        octreeConfigBuffer = VK_NULL_HANDLE;
        compressedColorBuffer = VK_NULL_HANDLE;
        compressedNormalBuffer = VK_NULL_HANDLE;
        brickGridLookupBuffer = VK_NULL_HANDLE;
        LogCleanupProgress("cached scene data released");
        return;  // Don't destroy anything - cacher manages resources
    }

    // Legacy path: manually destroy buffers we own

    // Destroy nodes buffer and memory
    if (octreeNodesBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeNodesBuffer, nullptr);
        octreeNodesBuffer = VK_NULL_HANDLE;
        LogCleanupProgress("octreeNodesBuffer destroyed");
    }

    if (octreeNodesMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeNodesMemory, nullptr);
        octreeNodesMemory = VK_NULL_HANDLE;
        LogCleanupProgress("octreeNodesMemory freed");
    }

    // Destroy bricks buffer and memory
    if (octreeBricksBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeBricksBuffer, nullptr);
        octreeBricksBuffer = VK_NULL_HANDLE;
        LogCleanupProgress("octreeBricksBuffer destroyed");
    }

    if (octreeBricksMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeBricksMemory, nullptr);
        octreeBricksMemory = VK_NULL_HANDLE;
        LogCleanupProgress("octreeBricksMemory freed");
    }

    // Destroy materials buffer and memory
    if (octreeMaterialsBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeMaterialsBuffer, nullptr);
        octreeMaterialsBuffer = VK_NULL_HANDLE;
        LogCleanupProgress("octreeMaterialsBuffer destroyed");
    }

    if (octreeMaterialsMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeMaterialsMemory, nullptr);
        octreeMaterialsMemory = VK_NULL_HANDLE;
        LogCleanupProgress("octreeMaterialsMemory freed");
    }

    // Destroy config buffer and memory
    if (octreeConfigBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeConfigBuffer, nullptr);
        octreeConfigBuffer = VK_NULL_HANDLE;
        LogCleanupProgress("octreeConfigBuffer destroyed");
    }

    if (octreeConfigMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeConfigMemory, nullptr);
        octreeConfigMemory = VK_NULL_HANDLE;
        LogCleanupProgress("octreeConfigMemory freed");
    }

    // Destroy compressed color buffer and memory
    if (compressedColorBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, compressedColorBuffer, nullptr);
        compressedColorBuffer = VK_NULL_HANDLE;
        LogCleanupProgress("compressedColorBuffer destroyed");
    }

    if (compressedColorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, compressedColorMemory, nullptr);
        compressedColorMemory = VK_NULL_HANDLE;
        LogCleanupProgress("compressedColorMemory freed");
    }

    // Destroy compressed normal buffer and memory
    if (compressedNormalBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, compressedNormalBuffer, nullptr);
        compressedNormalBuffer = VK_NULL_HANDLE;
        LogCleanupProgress("compressedNormalBuffer destroyed");
    }

    if (compressedNormalMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, compressedNormalMemory, nullptr);
        compressedNormalMemory = VK_NULL_HANDLE;
        LogCleanupProgress("compressedNormalMemory freed");
    }

    // Destroy brick grid lookup buffer and memory
    if (brickGridLookupBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, brickGridLookupBuffer, nullptr);
        brickGridLookupBuffer = VK_NULL_HANDLE;
        LogCleanupProgress("brickGridLookupBuffer destroyed");
    }

    if (brickGridLookupMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, brickGridLookupMemory, nullptr);
        brickGridLookupMemory = VK_NULL_HANDLE;
        LogCleanupProgress("brickGridLookupMemory freed");
    }
}

void VoxelGridNode::LogCleanupProgress(const std::string& stage) {
    NODE_LOG_DEBUG("[VoxelGridNode::Cleanup] " + stage);
}

void VoxelGridNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[VoxelGridNode::CleanupImpl] Destroying octree buffers");

    // CRITICAL: Release GPU resources (QueryPools) BEFORE device operations.
    // The logger object stays alive for parent log extraction, but its
    // VkQueryPool handles must be destroyed while VkDevice is still valid.
    if (memoryLogger_) {
        memoryLogger_->ReleaseGPUResources();
    }
    LogCleanupProgress("memoryLogger GPU resources released");

    if (!vulkanDevice) {
        NODE_LOG_DEBUG("[VoxelGridNode::CleanupImpl] Device unavailable, skipping cleanup");
        return;
    }

    vkDeviceWaitIdle(vulkanDevice->device);
    DestroyOctreeBuffers();

    // Clean up debug capture resource (it owns its own Vulkan resources)
    debugCaptureResource_.reset();
    LogCleanupProgress("debugCaptureResource destroyed");

    NODE_LOG_INFO("[VoxelGridNode::CleanupImpl] Cleanup complete");
}

#if 0  // ========================================================================
       // LEGACY HELPER METHODS - Now handled by VoxelSceneCacher
       // ========================================================================
       // These methods are no longer used because VoxelSceneCacher handles:
       // - GenerateProceduralScene: Scene generation via SceneGeneratorFactory
       // - UploadOctreeBuffers: Legacy octree buffer upload (superseded by ESVO)
       // - UploadESVOBuffers: ESVO buffer upload
       // - ExtractNodeData: Helper for legacy UploadOctreeBuffers
       // ========================================================================

void VoxelGridNode::GenerateProceduralScene(VoxelGrid& grid) {
    std::cout << "[VoxelGridNode] GenerateProceduralScene: sceneType=\"" << sceneType << "\" (length=" << sceneType.length() << ")" << std::endl;
    NODE_LOG_INFO("Generating procedural scene: " + sceneType);

    // Create generator from factory
    auto generator = SceneGeneratorFactory::Create(sceneType);

    if (!generator) {
        // Check for "test" pattern as special case
        if (sceneType == "test") {
            std::cout << "[VoxelGridNode] Generating test pattern (all solid)" << std::endl;
            grid.Clear();
            for (uint32_t z = 0; z < resolution; ++z) {
                for (uint32_t y = 0; y < resolution; ++y) {
                    for (uint32_t x = 0; x < resolution; ++x) {
                        grid.Set(x, y, z, 1);  // Material ID 1
                    }
                }
            }
            NODE_LOG_INFO("Generated test pattern (all solid voxels)");
            return;
        }

        // Unknown scene type - fall back to cornell
        NODE_LOG_WARNING("Unknown scene type '" + sceneType + "', falling back to 'cornell'");
        std::cout << "[VoxelGridNode] Unknown scene type '" << sceneType << "', using cornell" << std::endl;
        generator = SceneGeneratorFactory::Create("cornell");

        if (!generator) {
            // This should never happen with built-in generators
            NODE_LOG_ERROR("Failed to create fallback cornell generator");
            return;
        }
    }

    // Build params from node config
    SceneGeneratorParams params;
    params.resolution = resolution;
    params.seed = GetParameterValue<uint32_t>("seed", 42u);
    params.noiseScale = GetParameterValue<float>("noise_scale", 4.0f);
    params.densityThreshold = GetParameterValue<float>("density_threshold", 0.5f);
    params.octaves = GetParameterValue<uint32_t>("octaves", 4u);
    params.persistence = GetParameterValue<float>("persistence", 0.5f);
    params.streetWidth = GetParameterValue<uint32_t>("street_width", 0u);
    params.blockCount = GetParameterValue<uint32_t>("block_count", 4u);
    params.buildingDensity = GetParameterValue<float>("building_density", 0.4f);
    params.heightVariance = GetParameterValue<float>("height_variance", 0.8f);
    params.blockSize = GetParameterValue<uint32_t>("block_size", 8u);
    params.cellCount = GetParameterValue<uint32_t>("cell_count", 8u);
    params.wallThickness = GetParameterValue<float>("wall_thickness", 0.3f);

    std::cout << "[VoxelGridNode] Using generator: " << generator->GetName()
              << " (" << generator->GetDescription() << ")" << std::endl;
    NODE_LOG_INFO("Using generator: " + generator->GetName());

    // Generate scene
    generator->Generate(grid, params);

    // Validate density
    auto [minDensity, maxDensity] = generator->GetExpectedDensityRange();
    float actualDensity = grid.GetDensityPercent();

    std::cout << "[VoxelGridNode] Generated density: " << actualDensity << "% (expected: "
              << minDensity << "-" << maxDensity << "%)" << std::endl;

    if (actualDensity < minDensity || actualDensity > maxDensity) {
        NODE_LOG_WARNING("Scene density " + std::to_string(actualDensity) +
                        "% outside expected range [" + std::to_string(minDensity) +
                        ", " + std::to_string(maxDensity) + "]");
    }
}

/// Extracts node data pointer and size from octree based on format.
/// Returns <size, data, formatName>.
static std::tuple<VkDeviceSize, const void*, std::string>
ExtractNodeData(const SparseVoxelOctree& octree) {
    if (octree.GetNodeFormat() == VIXEN::RenderGraph::NodeFormat::ESVO) {
        const auto& esvoNodes = octree.GetESVONodes();
        VkDeviceSize size = esvoNodes.size() * sizeof(VIXEN::RenderGraph::ESVONode);
        std::cout << "[VoxelGridNode::ExtractNodeData] ESVO format: " << esvoNodes.size()
                  << " nodes (8 bytes) = " << size << " bytes" << std::endl;
        if (!esvoNodes.empty()) {
            std::cout << "[VoxelGridNode::ExtractNodeData] Root ESVO node: descriptor0=0x" << std::hex
                      << esvoNodes[0].descriptor0 << ", descriptor1=0x" << esvoNodes[0].descriptor1
                      << std::dec << std::endl;
        }
        return std::make_tuple(size, esvoNodes.data(), "ESVO");
    }

    const auto& legacyNodes = octree.GetNodes();
    VkDeviceSize size = legacyNodes.size() * sizeof(VIXEN::RenderGraph::OctreeNode);
    std::cout << "[VoxelGridNode::ExtractNodeData] Legacy format: " << legacyNodes.size()
              << " nodes (40 bytes) = " << size << " bytes" << std::endl;
    return std::make_tuple(size, legacyNodes.data(), "Legacy");
}

void VoxelGridNode::UploadOctreeBuffers(const SparseVoxelOctree& octree) {
    auto [nodesBufferSize, nodesData, formatName] = ExtractNodeData(octree);
    const auto& bricks = octree.GetBricks();

    VkDeviceSize bricksBufferSize = bricks.size() * sizeof(VoxelBrick);

    NODE_LOG_INFO("Uploading octree: " +
                  std::to_string(nodesBufferSize) + " bytes (nodes), " +
                  std::to_string(bricksBufferSize) + " bytes (bricks)");

    // === CREATE NODES BUFFER ===
    VkBufferCreateInfo nodesBufferInfo{};
    nodesBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    nodesBufferInfo.size = nodesBufferSize;
    nodesBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    nodesBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(vulkanDevice->device, &nodesBufferInfo, nullptr, &octreeNodesBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create octree nodes buffer: " + std::to_string(result));
    }

    // Allocate device-local memory for nodes
    VkMemoryRequirements nodesMemReq;
    vkGetBufferMemoryRequirements(vulkanDevice->device, octreeNodesBuffer, &nodesMemReq);

    VkMemoryAllocateInfo nodesAllocInfo{};
    nodesAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    nodesAllocInfo.allocationSize = nodesMemReq.size;

    const VkPhysicalDeviceMemoryProperties& memProperties = vulkanDevice->gpuMemoryProperties;
    uint32_t memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((nodesMemReq.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("[VoxelGridNode] Failed to find device-local memory for octree nodes");
    }

    nodesAllocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(vulkanDevice->device, &nodesAllocInfo, nullptr, &octreeNodesMemory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to allocate octree nodes memory: " + std::to_string(result));
    }

    vkBindBufferMemory(vulkanDevice->device, octreeNodesBuffer, octreeNodesMemory, 0);

    // === CREATE BRICKS BUFFER ===
    VkBufferCreateInfo bricksBufferInfo{};
    bricksBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bricksBufferInfo.size = bricksBufferSize;
    bricksBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bricksBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = vkCreateBuffer(vulkanDevice->device, &bricksBufferInfo, nullptr, &octreeBricksBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create octree bricks buffer: " + std::to_string(result));
    }

    // Allocate device-local memory for bricks
    VkMemoryRequirements bricksMemReq;
    vkGetBufferMemoryRequirements(vulkanDevice->device, octreeBricksBuffer, &bricksMemReq);

    VkMemoryAllocateInfo bricksAllocInfo{};
    bricksAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bricksAllocInfo.allocationSize = bricksMemReq.size;

    memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((bricksMemReq.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("[VoxelGridNode] Failed to find device-local memory for octree bricks");
    }

    bricksAllocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(vulkanDevice->device, &bricksAllocInfo, nullptr, &octreeBricksMemory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to allocate octree bricks memory: " + std::to_string(result));
    }

    vkBindBufferMemory(vulkanDevice->device, octreeBricksBuffer, octreeBricksMemory, 0);

    // === CREATE MATERIALS BUFFER ===
    // TODO: Extract materials from octree. For now, create minimal default material palette.
    struct GPUMaterial {
        float albedo[3];
        float roughness;
        float metallic;
        float emissive;
        float padding[2];
    };

    // Cornell Box material palette (matching CornellBoxGenerator IDs)
    std::vector<GPUMaterial> defaultMaterials(21);
    // Material 0: Default white diffuse
    defaultMaterials[0] = {{0.8f, 0.8f, 0.8f}, 0.8f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 1: Red (left wall)
    defaultMaterials[1] = {{0.75f, 0.1f, 0.1f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 2: Green (right wall)
    defaultMaterials[2] = {{0.1f, 0.75f, 0.1f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 3: White (back wall)
    defaultMaterials[3] = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 4: White (floor)
    defaultMaterials[4] = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 5: White (ceiling)
    defaultMaterials[5] = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 6: Light gray (checker floor)
    defaultMaterials[6] = {{0.7f, 0.7f, 0.7f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 7: Dark gray (checker floor)
    defaultMaterials[7] = {{0.3f, 0.3f, 0.3f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 8-9: Reserved
    defaultMaterials[8] = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    defaultMaterials[9] = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 10: Left cube (beige diffuse)
    defaultMaterials[10] = {{0.8f, 0.7f, 0.5f}, 0.8f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 11: Right cube (light blue)
    defaultMaterials[11] = {{0.4f, 0.6f, 0.8f}, 0.7f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 12-18: Reserved
    for (uint32_t i = 12; i < 19; ++i) {
        defaultMaterials[i] = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    }
    // Material 19: DEBUG corner marker (bright magenta)
    defaultMaterials[19] = {{1.0f, 0.0f, 1.0f}, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f}};
    // Material 20: Ceiling light (emissive white)
    defaultMaterials[20] = {{1.0f, 1.0f, 0.9f}, 0.0f, 0.0f, 5.0f, {0.0f, 0.0f}};

    // DEBUG: Print first few materials
    std::cout << "[VoxelGridNode] DEBUG: Material palette (first 3):" << std::endl;
    for (int i = 0; i < 3 && i < defaultMaterials.size(); ++i) {
        std::cout << "  Mat[" << i << "]: albedo=(" << defaultMaterials[i].albedo[0] << ", "
                  << defaultMaterials[i].albedo[1] << ", " << defaultMaterials[i].albedo[2]
                  << "), r=" << defaultMaterials[i].roughness << std::endl;
    }

    VkDeviceSize materialsBufferSize = defaultMaterials.size() * sizeof(GPUMaterial);

    VkBufferCreateInfo materialsBufferInfo{};
    materialsBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    materialsBufferInfo.size = materialsBufferSize;
    materialsBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    materialsBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = vkCreateBuffer(vulkanDevice->device, &materialsBufferInfo, nullptr, &octreeMaterialsBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create octree materials buffer: " + std::to_string(result));
    }

    // Allocate device-local memory for materials
    VkMemoryRequirements materialsMemReq;
    vkGetBufferMemoryRequirements(vulkanDevice->device, octreeMaterialsBuffer, &materialsMemReq);

    VkMemoryAllocateInfo materialsAllocInfo{};
    materialsAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    materialsAllocInfo.allocationSize = materialsMemReq.size;

    memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((materialsMemReq.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("[VoxelGridNode] Failed to find device-local memory for octree materials");
    }

    materialsAllocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(vulkanDevice->device, &materialsAllocInfo, nullptr, &octreeMaterialsMemory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to allocate octree materials memory: " + std::to_string(result));
    }

    vkBindBufferMemory(vulkanDevice->device, octreeMaterialsBuffer, octreeMaterialsMemory, 0);

    // === UPLOAD DATA VIA STAGING BUFFER ===

    // Upload nodes
    if (nodesBufferSize > 0) {
        // Create staging buffer for nodes
        VkBufferCreateInfo nodesStagingInfo{};
        nodesStagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        nodesStagingInfo.size = nodesBufferSize;
        nodesStagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        nodesStagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer nodesStagingBuffer;
        vkCreateBuffer(vulkanDevice->device, &nodesStagingInfo, nullptr, &nodesStagingBuffer);

        VkMemoryRequirements nodesStagingMemReq;
        vkGetBufferMemoryRequirements(vulkanDevice->device, nodesStagingBuffer, &nodesStagingMemReq);

        VkMemoryAllocateInfo nodesStagingAllocInfo{};
        nodesStagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        nodesStagingAllocInfo.allocationSize = nodesStagingMemReq.size;

        memoryTypeIndex = UINT32_MAX;
        VkMemoryPropertyFlags stagingProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((nodesStagingMemReq.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & stagingProps) == stagingProps) {
                memoryTypeIndex = i;
                break;
            }
        }

        nodesStagingAllocInfo.memoryTypeIndex = memoryTypeIndex;

        VkDeviceMemory nodesStagingMemory;
        vkAllocateMemory(vulkanDevice->device, &nodesStagingAllocInfo, nullptr, &nodesStagingMemory);
        vkBindBufferMemory(vulkanDevice->device, nodesStagingBuffer, nodesStagingMemory, 0);

        // Copy data to staging buffer (use nodesData pointer determined earlier)
        void* data;
        vkMapMemory(vulkanDevice->device, nodesStagingMemory, 0, nodesBufferSize, 0, &data);
        std::memcpy(data, nodesData, nodesBufferSize);  // Changed: nodes.data() → nodesData
        vkUnmapMemory(vulkanDevice->device, nodesStagingMemory);

        // Copy from staging to device buffer
        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        vkAllocateCommandBuffers(vulkanDevice->device, &cmdAllocInfo, &cmdBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = nodesBufferSize;
        vkCmdCopyBuffer(cmdBuffer, nodesStagingBuffer, octreeNodesBuffer, 1, &copyRegion);

        vkEndCommandBuffer(cmdBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vulkanDevice->queue);

        vkFreeCommandBuffers(vulkanDevice->device, commandPool, 1, &cmdBuffer);
        vkDestroyBuffer(vulkanDevice->device, nodesStagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice->device, nodesStagingMemory, nullptr);
    }

    // Upload bricks
    if (bricksBufferSize > 0) {
        // Create staging buffer for bricks
        VkBufferCreateInfo bricksStagingInfo{};
        bricksStagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bricksStagingInfo.size = bricksBufferSize;
        bricksStagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bricksStagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer bricksStagingBuffer;
        vkCreateBuffer(vulkanDevice->device, &bricksStagingInfo, nullptr, &bricksStagingBuffer);

        VkMemoryRequirements bricksStagingMemReq;
        vkGetBufferMemoryRequirements(vulkanDevice->device, bricksStagingBuffer, &bricksStagingMemReq);

        VkMemoryAllocateInfo bricksStagingAllocInfo{};
        bricksStagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        bricksStagingAllocInfo.allocationSize = bricksStagingMemReq.size;

        memoryTypeIndex = UINT32_MAX;
        VkMemoryPropertyFlags stagingProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((bricksStagingMemReq.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & stagingProps) == stagingProps) {
                memoryTypeIndex = i;
                break;
            }
        }

        bricksStagingAllocInfo.memoryTypeIndex = memoryTypeIndex;

        VkDeviceMemory bricksStagingMemory;
        vkAllocateMemory(vulkanDevice->device, &bricksStagingAllocInfo, nullptr, &bricksStagingMemory);
        vkBindBufferMemory(vulkanDevice->device, bricksStagingBuffer, bricksStagingMemory, 0);

        // Copy data to staging buffer
        void* data;
        vkMapMemory(vulkanDevice->device, bricksStagingMemory, 0, bricksBufferSize, 0, &data);
        std::memcpy(data, bricks.data(), bricksBufferSize);
        vkUnmapMemory(vulkanDevice->device, bricksStagingMemory);

        // Copy from staging to device buffer
        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        vkAllocateCommandBuffers(vulkanDevice->device, &cmdAllocInfo, &cmdBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = bricksBufferSize;
        vkCmdCopyBuffer(cmdBuffer, bricksStagingBuffer, octreeBricksBuffer, 1, &copyRegion);

        vkEndCommandBuffer(cmdBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vulkanDevice->queue);

        vkFreeCommandBuffers(vulkanDevice->device, commandPool, 1, &cmdBuffer);
        vkDestroyBuffer(vulkanDevice->device, bricksStagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice->device, bricksStagingMemory, nullptr);
    }

    // Upload materials
    if (materialsBufferSize > 0) {
        // Create staging buffer for materials
        VkBufferCreateInfo materialsStagingInfo{};
        materialsStagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        materialsStagingInfo.size = materialsBufferSize;
        materialsStagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        materialsStagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer materialsStagingBuffer;
        vkCreateBuffer(vulkanDevice->device, &materialsStagingInfo, nullptr, &materialsStagingBuffer);

        VkMemoryRequirements materialsStagingMemReq;
        vkGetBufferMemoryRequirements(vulkanDevice->device, materialsStagingBuffer, &materialsStagingMemReq);

        VkMemoryAllocateInfo materialsStagingAllocInfo{};
        materialsStagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        materialsStagingAllocInfo.allocationSize = materialsStagingMemReq.size;

        memoryTypeIndex = UINT32_MAX;
        VkMemoryPropertyFlags stagingProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((materialsStagingMemReq.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & stagingProps) == stagingProps) {
                memoryTypeIndex = i;
                break;
            }
        }

        materialsStagingAllocInfo.memoryTypeIndex = memoryTypeIndex;

        VkDeviceMemory materialsStagingMemory;
        vkAllocateMemory(vulkanDevice->device, &materialsStagingAllocInfo, nullptr, &materialsStagingMemory);
        vkBindBufferMemory(vulkanDevice->device, materialsStagingBuffer, materialsStagingMemory, 0);

        // Copy data to staging buffer
        void* data;
        vkMapMemory(vulkanDevice->device, materialsStagingMemory, 0, materialsBufferSize, 0, &data);
        std::memcpy(data, defaultMaterials.data(), materialsBufferSize);
        vkUnmapMemory(vulkanDevice->device, materialsStagingMemory);

        // Copy from staging to device buffer
        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        vkAllocateCommandBuffers(vulkanDevice->device, &cmdAllocInfo, &cmdBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = materialsBufferSize;
        vkCmdCopyBuffer(cmdBuffer, materialsStagingBuffer, octreeMaterialsBuffer, 1, &copyRegion);

        vkEndCommandBuffer(cmdBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vulkanDevice->queue);

        vkFreeCommandBuffers(vulkanDevice->device, commandPool, 1, &cmdBuffer);
        vkDestroyBuffer(vulkanDevice->device, materialsStagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice->device, materialsStagingMemory, nullptr);
    }

    NODE_LOG_INFO("Uploaded octree buffers to GPU");
}

void VoxelGridNode::UploadESVOBuffers(const Vixen::SVO::Octree& octree, const VoxelGrid& grid) {
    if (!octree.root) {
        throw std::runtime_error("[VoxelGridNode] ESVO octree has no root block");
    }

    const auto& rootBlock = *octree.root;
    const auto& childDescriptors = rootBlock.childDescriptors;
    const auto& brickViews = rootBlock.brickViews;

    // ============================================================================
    // ESVO NODES BUFFER (binding 1)
    // ============================================================================
    // ChildDescriptor is 64 bits = uvec2 in GLSL
    // Layout matches SVOTypes.h:
    //   Part 1 (32 bits): childPointer:15, farBit:1, validMask:8, leafMask:8
    //   Part 2 (32 bits): contourPointer:24, contourMask:8

    VkDeviceSize nodesBufferSize = childDescriptors.size() * sizeof(SVO::ChildDescriptor);
    std::cout << "[VoxelGridNode::UploadESVOBuffers] Uploading " << childDescriptors.size()
              << " ESVO nodes (" << nodesBufferSize << " bytes)" << std::endl;

    if (childDescriptors.empty()) {
        // Create minimal buffer with single empty node
        std::vector<SVO::ChildDescriptor> emptyNode(1);
        emptyNode[0] = {};  // Zero-initialized
        nodesBufferSize = sizeof(SVO::ChildDescriptor);

        // Log warning
        std::cout << "[VoxelGridNode::UploadESVOBuffers] WARNING: Empty descriptors, creating placeholder" << std::endl;
    }

    // ============================================================================
    // SPARSE BRICK DATA BUFFER (binding 2) + BRICK BASE INDEX (binding 6)
    // ============================================================================
    // NEW ARCHITECTURE: Sparse brick storage with topological indexing
    //
    // Instead of dense grid-ordered bricks, we now:
    // 1. Build compact brickData from actual brickViews (sparse)
    // 2. Compute brickBaseIndex[nodeId] for nodes at brickESVOScale
    // 3. GPU uses validMask/leafMask + countLeavesBefore to find brickIndex
    //
    // This matches the ESVO paper's efficient sparse representation.

    const size_t voxelsPerBrick = 512;  // 8^3
    const int bricksPerAxis = octree.bricksPerAxis;
    const int brickSideLength = octree.brickSideLength;

    // Get grid data and resolution for direct voxel extraction
    const auto& gridData = grid.GetData();
    uint32_t gridRes = grid.GetResolution();

    std::cout << "[VoxelGridNode::UploadESVOBuffers] Building sparse brick data from "
              << brickViews.size() << " brick views (bricksPerAxis=" << bricksPerAxis << ")" << std::endl;
    std::cout << "[VoxelGridNode::UploadESVOBuffers] brickGridToBrickView has "
              << rootBlock.brickGridToBrickView.size() << " entries" << std::endl;

    // ============================================================================
    // Step 1: Build sparse brickData in brickViews order (NO reordering)
    // ============================================================================
    // SIMPLE APPROACH: Upload brick data in the original brickViews order.
    // The brickGridToBrickView mapping provides (gridCoord) -> brickViewIndex.
    // We store this mapping in brickBaseIndex buffer for direct lookup.

    std::vector<uint32_t> sparseBrickData;
    sparseBrickData.reserve(brickViews.size() * voxelsPerBrick);

    size_t nonZeroVoxels = 0;

    // Upload brick data in brickViews order (sparse index = brickViewIndex)
    for (size_t viewIdx = 0; viewIdx < brickViews.size(); ++viewIdx) {
        const auto& view = brickViews[viewIdx];
        const glm::ivec3 gridOrigin = view.getLocalGridOrigin();

        // Extract voxel data for this brick directly from grid
        for (int z = 0; z < brickSideLength; ++z) {
            for (int y = 0; y < brickSideLength; ++y) {
                for (int x = 0; x < brickSideLength; ++x) {
                    int worldX = gridOrigin.x + x;
                    int worldY = gridOrigin.y + y;
                    int worldZ = gridOrigin.z + z;

                    uint32_t materialId = 0;
                    if (worldX >= 0 && worldX < static_cast<int>(gridRes) &&
                        worldY >= 0 && worldY < static_cast<int>(gridRes) &&
                        worldZ >= 0 && worldZ < static_cast<int>(gridRes)) {
                        size_t gridIdx = static_cast<size_t>(worldZ) * gridRes * gridRes +
                                         static_cast<size_t>(worldY) * gridRes + worldX;
                        materialId = static_cast<uint32_t>(gridData[gridIdx]);
                        if (materialId != 0) nonZeroVoxels++;
                    }

                    sparseBrickData.push_back(materialId);
                }
            }
        }
    }

    std::cout << "[VoxelGridNode::UploadESVOBuffers] Built sparse brick array: "
              << brickViews.size() << " bricks, " << nonZeroVoxels << " non-zero voxels, "
              << (sparseBrickData.size() * sizeof(uint32_t)) << " bytes" << std::endl;

    // ============================================================================
    // SPARSE BRICK ARCHITECTURE: Brick indices are now stored directly in leaf
    // descriptors via setBrickIndex(). The shader reads getBrickIndex(leafDescriptor).

    std::cout << "[VoxelGridNode::UploadESVOBuffers] SPARSE BRICK ARCHITECTURE: "
              << "brickIndex now embedded in leaf descriptors" << std::endl;

    VkDeviceSize bricksBufferSize = sparseBrickData.size() * sizeof(uint32_t);
    if (bricksBufferSize == 0) {
        // Create minimal buffer if no bricks
        sparseBrickData.resize(voxelsPerBrick, 0);
        bricksBufferSize = voxelsPerBrick * sizeof(uint32_t);
    }

    NODE_LOG_INFO("Uploading ESVO buffers: " +
                  std::to_string(nodesBufferSize) + " bytes (nodes), " +
                  std::to_string(bricksBufferSize) + " bytes (sparse bricks)");

    const VkPhysicalDeviceMemoryProperties& memProperties = vulkanDevice->gpuMemoryProperties;

    // ============================================================================
    // CREATE NODES BUFFER
    // ============================================================================
    VkBufferCreateInfo nodesBufferInfo{};
    nodesBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    nodesBufferInfo.size = nodesBufferSize;
    nodesBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    nodesBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(vulkanDevice->device, &nodesBufferInfo, nullptr, &octreeNodesBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create ESVO nodes buffer: " + std::to_string(result));
    }

    // Allocate device-local memory for nodes
    VkMemoryRequirements nodesMemReq;
    vkGetBufferMemoryRequirements(vulkanDevice->device, octreeNodesBuffer, &nodesMemReq);

    VkMemoryAllocateInfo nodesAllocInfo{};
    nodesAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    nodesAllocInfo.allocationSize = nodesMemReq.size;

    uint32_t memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((nodesMemReq.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("[VoxelGridNode] Failed to find device-local memory for ESVO nodes");
    }
    nodesAllocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(vulkanDevice->device, &nodesAllocInfo, nullptr, &octreeNodesMemory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to allocate ESVO nodes memory: " + std::to_string(result));
    }
    vkBindBufferMemory(vulkanDevice->device, octreeNodesBuffer, octreeNodesMemory, 0);

    // Register nodes buffer for memory tracking
    if (memoryLogger_) {
        memoryLogger_->RegisterBufferAllocation("OctreeNodes", nodesBufferSize);
    }

    // ============================================================================
    // CREATE BRICKS BUFFER
    // ============================================================================
    VkDeviceSize actualBricksBufferSize = bricksBufferSize > 0 ? bricksBufferSize : sizeof(uint32_t);

    VkBufferCreateInfo bricksBufferInfo{};
    bricksBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bricksBufferInfo.size = actualBricksBufferSize;
    bricksBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bricksBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = vkCreateBuffer(vulkanDevice->device, &bricksBufferInfo, nullptr, &octreeBricksBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create ESVO bricks buffer: " + std::to_string(result));
    }

    VkMemoryRequirements bricksMemReq;
    vkGetBufferMemoryRequirements(vulkanDevice->device, octreeBricksBuffer, &bricksMemReq);

    VkMemoryAllocateInfo bricksAllocInfo{};
    bricksAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bricksAllocInfo.allocationSize = bricksMemReq.size;

    memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((bricksMemReq.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("[VoxelGridNode] Failed to find device-local memory for ESVO bricks");
    }
    bricksAllocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(vulkanDevice->device, &bricksAllocInfo, nullptr, &octreeBricksMemory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to allocate ESVO bricks memory: " + std::to_string(result));
    }
    vkBindBufferMemory(vulkanDevice->device, octreeBricksBuffer, octreeBricksMemory, 0);

    // Register bricks buffer for memory tracking (uncompressed - 4 bytes per voxel)
    if (memoryLogger_) {
        memoryLogger_->RegisterBufferAllocation("OctreeBricks (uncompressed)", actualBricksBufferSize);
    }

    // ============================================================================
    // CREATE MATERIALS BUFFER (same as legacy - material palette)
    // ============================================================================
    struct GPUMaterial {
        float albedo[3];
        float roughness;
        float metallic;
        float emissive;
        float padding[2];
    };

    // Cornell Box material palette
    std::vector<GPUMaterial> defaultMaterials(21);
    defaultMaterials[0] = {{0.8f, 0.8f, 0.8f}, 0.8f, 0.0f, 0.0f, {0.0f, 0.0f}};   // Default white
    defaultMaterials[1] = {{0.75f, 0.1f, 0.1f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};  // Red (left wall)
    defaultMaterials[2] = {{0.1f, 0.75f, 0.1f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};  // Green (right wall)
    defaultMaterials[3] = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};   // White (back wall)
    defaultMaterials[4] = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};   // White (floor)
    defaultMaterials[5] = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};   // White (ceiling)
    defaultMaterials[6] = {{0.7f, 0.7f, 0.7f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};   // Light gray
    defaultMaterials[7] = {{0.3f, 0.3f, 0.3f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};   // Dark gray
    defaultMaterials[8] = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};   // Reserved
    defaultMaterials[9] = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};   // Reserved
    defaultMaterials[10] = {{0.8f, 0.7f, 0.5f}, 0.8f, 0.0f, 0.0f, {0.0f, 0.0f}};  // Left cube (beige)
    defaultMaterials[11] = {{0.4f, 0.6f, 0.8f}, 0.7f, 0.0f, 0.0f, {0.0f, 0.0f}};  // Right cube (blue)
    for (uint32_t i = 12; i < 19; ++i) {
        defaultMaterials[i] = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    }
    defaultMaterials[19] = {{1.0f, 0.0f, 1.0f}, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f}};  // Debug marker
    defaultMaterials[20] = {{1.0f, 1.0f, 0.9f}, 0.0f, 0.0f, 5.0f, {0.0f, 0.0f}};  // Ceiling light

    VkDeviceSize materialsBufferSize = defaultMaterials.size() * sizeof(GPUMaterial);

    VkBufferCreateInfo materialsBufferInfo{};
    materialsBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    materialsBufferInfo.size = materialsBufferSize;
    materialsBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    materialsBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = vkCreateBuffer(vulkanDevice->device, &materialsBufferInfo, nullptr, &octreeMaterialsBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create materials buffer: " + std::to_string(result));
    }

    VkMemoryRequirements materialsMemReq;
    vkGetBufferMemoryRequirements(vulkanDevice->device, octreeMaterialsBuffer, &materialsMemReq);

    VkMemoryAllocateInfo materialsAllocInfo{};
    materialsAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    materialsAllocInfo.allocationSize = materialsMemReq.size;

    memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((materialsMemReq.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("[VoxelGridNode] Failed to find device-local memory for materials");
    }
    materialsAllocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(vulkanDevice->device, &materialsAllocInfo, nullptr, &octreeMaterialsMemory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to allocate materials memory: " + std::to_string(result));
    }
    vkBindBufferMemory(vulkanDevice->device, octreeMaterialsBuffer, octreeMaterialsMemory, 0);

    // Register materials buffer for memory tracking
    if (memoryLogger_) {
        memoryLogger_->RegisterBufferAllocation("Materials", materialsBufferSize);
    }

    // ============================================================================
    // UPLOAD DATA VIA STAGING BUFFERS
    // ============================================================================
    VkMemoryPropertyFlags stagingProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    auto uploadBuffer = [&](VkBuffer dstBuffer, const void* srcData, VkDeviceSize size) {
        if (size == 0) return;

        // Create staging buffer
        VkBufferCreateInfo stagingInfo{};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = size;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer stagingBuffer;
        vkCreateBuffer(vulkanDevice->device, &stagingInfo, nullptr, &stagingBuffer);

        VkMemoryRequirements stagingMemReq;
        vkGetBufferMemoryRequirements(vulkanDevice->device, stagingBuffer, &stagingMemReq);

        VkMemoryAllocateInfo stagingAllocInfo{};
        stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        stagingAllocInfo.allocationSize = stagingMemReq.size;

        uint32_t stagingMemTypeIdx = UINT32_MAX;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((stagingMemReq.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & stagingProps) == stagingProps) {
                stagingMemTypeIdx = i;
                break;
            }
        }
        stagingAllocInfo.memoryTypeIndex = stagingMemTypeIdx;

        VkDeviceMemory stagingMemory;
        vkAllocateMemory(vulkanDevice->device, &stagingAllocInfo, nullptr, &stagingMemory);
        vkBindBufferMemory(vulkanDevice->device, stagingBuffer, stagingMemory, 0);

        // Copy data to staging
        void* mappedData;
        vkMapMemory(vulkanDevice->device, stagingMemory, 0, size, 0, &mappedData);
        std::memcpy(mappedData, srcData, size);
        vkUnmapMemory(vulkanDevice->device, stagingMemory);

        // Copy from staging to device
        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        vkAllocateCommandBuffers(vulkanDevice->device, &cmdAllocInfo, &cmdBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmdBuffer, stagingBuffer, dstBuffer, 1, &copyRegion);

        vkEndCommandBuffer(cmdBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vulkanDevice->queue);

        vkFreeCommandBuffers(vulkanDevice->device, commandPool, 1, &cmdBuffer);
        vkDestroyBuffer(vulkanDevice->device, stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice->device, stagingMemory, nullptr);
    };

    // Upload nodes
    if (!childDescriptors.empty()) {
        uploadBuffer(octreeNodesBuffer, childDescriptors.data(), nodesBufferSize);
    }

    // Upload sparse bricks
    if (!sparseBrickData.empty()) {
        uploadBuffer(octreeBricksBuffer, sparseBrickData.data(), sparseBrickData.size() * sizeof(uint32_t));
    }

    // Upload materials
    uploadBuffer(octreeMaterialsBuffer, defaultMaterials.data(), materialsBufferSize);

    NODE_LOG_INFO("Uploaded ESVO buffers to GPU (sparse brick architecture)");
    std::cout << "[VoxelGridNode::UploadESVOBuffers] Upload complete - sparse bricks: "
              << brickViews.size() << std::endl;
}
#endif // LEGACY HELPER METHODS

// ============================================================================
// CACHER REGISTRATION
// ============================================================================

void VoxelGridNode::RegisterVoxelSceneCacher() {
    // Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register VoxelSceneCacher (idempotent - safe to call multiple times)
    if (!mainCacher.IsRegistered(typeid(CashSystem::VoxelSceneData))) {
        mainCacher.RegisterCacher<
            CashSystem::VoxelSceneCacher,
            CashSystem::VoxelSceneData,
            CashSystem::VoxelSceneCreateInfo
        >(
            typeid(CashSystem::VoxelSceneData),
            "VoxelScene",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("VoxelGridNode: Registered VoxelSceneCacher");
    }

    // Cache the cacher reference for use throughout node lifetime
    voxelSceneCacher_ = mainCacher.GetCacher<
        CashSystem::VoxelSceneCacher,
        CashSystem::VoxelSceneData,
        CashSystem::VoxelSceneCreateInfo
    >(typeid(CashSystem::VoxelSceneData), device);

    if (voxelSceneCacher_) {
        NODE_LOG_INFO("VoxelGridNode: VoxelScene cache ready");
    }
}

// ============================================================================
// CACHER GET-OR-CREATE
// ============================================================================

void VoxelGridNode::CreateSceneViaCacher() {
    if (!voxelSceneCacher_) {
        throw std::runtime_error("[VoxelGridNode] VoxelSceneCacher not registered");
    }

    // Build cache parameters from node config
    CashSystem::VoxelSceneCreateInfo params;
    params.sceneType = CashSystem::StringToSceneType(sceneType);
    params.resolution = resolution;
    params.density = 0.5f;  // Default density (some generators use this)
    params.seed = 42;       // Fixed seed for reproducibility

    NODE_LOG_INFO("VoxelGridNode: Requesting scene via cacher: type=" + sceneType +
                  ", resolution=" + std::to_string(resolution));

    // Call GetOrCreate - cacher handles scene gen, octree build, compression, GPU upload
    cachedSceneData_ = voxelSceneCacher_->GetOrCreate(params);

    if (!cachedSceneData_ || !cachedSceneData_->IsValid()) {
        throw std::runtime_error("[VoxelGridNode] Failed to get or create cached scene data");
    }

    // Extract buffer handles from cached data for outputs
    octreeNodesBuffer = cachedSceneData_->esvoNodesBuffer;
    octreeBricksBuffer = cachedSceneData_->brickDataBuffer;
    octreeMaterialsBuffer = cachedSceneData_->materialsBuffer;
    octreeConfigBuffer = cachedSceneData_->octreeConfigBuffer;
    compressedColorBuffer = cachedSceneData_->compressedColorsBuffer;
    compressedNormalBuffer = cachedSceneData_->compressedNormalsBuffer;
    brickGridLookupBuffer = cachedSceneData_->brickGridLookupBuffer;

    // Note: Memory is owned by cachedSceneData_, not these individual members
    // We set the memory handles to NULL_HANDLE to indicate we don't own them
    octreeNodesMemory = VK_NULL_HANDLE;
    octreeBricksMemory = VK_NULL_HANDLE;
    octreeMaterialsMemory = VK_NULL_HANDLE;
    octreeConfigMemory = VK_NULL_HANDLE;
    compressedColorMemory = VK_NULL_HANDLE;
    compressedNormalMemory = VK_NULL_HANDLE;
    brickGridLookupMemory = VK_NULL_HANDLE;

    NODE_LOG_INFO("VoxelGridNode: Scene created via cacher: " +
                  std::to_string(cachedSceneData_->nodeCount) + " nodes, " +
                  std::to_string(cachedSceneData_->brickCount) + " bricks, " +
                  std::to_string(cachedSceneData_->solidVoxelCount) + " voxels");

    std::cout << "[VoxelGridNode] Scene created via cacher:" << std::endl;
    std::cout << "  esvoNodesBuffer=" << octreeNodesBuffer << std::endl;
    std::cout << "  brickDataBuffer=" << octreeBricksBuffer << std::endl;
    std::cout << "  materialsBuffer=" << octreeMaterialsBuffer << std::endl;
    std::cout << "  octreeConfigBuffer=" << octreeConfigBuffer << std::endl;
    std::cout << "  compressedColorsBuffer=" << compressedColorBuffer << std::endl;
    std::cout << "  compressedNormalsBuffer=" << compressedNormalBuffer << std::endl;
    std::cout << "  brickGridLookupBuffer=" << brickGridLookupBuffer << std::endl;
}

} // namespace Vixen::RenderGraph
