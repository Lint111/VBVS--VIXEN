#include "Nodes/VoxelGridNode.h"
#include "Data/SceneGenerator.h"
#include "Data/VoxelOctree.h" // Legacy - will be removed
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
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
using VIXEN::RenderGraph::CornellBoxGenerator;
using VIXEN::RenderGraph::CaveSystemGenerator;
using VIXEN::RenderGraph::UrbanGridGenerator;
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
};
static_assert(sizeof(OctreeConfig) == 64, "OctreeConfig must be 64 bytes for std140 alignment");

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

    // Generate procedural voxel scene
    std::cout << "[VoxelGridNode] Generating voxel grid: resolution=" << resolution << ", sceneType=" << sceneType << std::endl;
    VoxelGrid grid(resolution);
    GenerateProceduralScene(grid);

    size_t voxelCount = resolution * resolution * resolution;
    std::cout << "[VoxelGridNode] Generated " << voxelCount << " voxels, density=" << grid.GetDensityPercent() << "%" << std::endl;
    NODE_LOG_INFO("Generated " + std::to_string(voxelCount) + " voxels, density=" +
                  std::to_string(grid.GetDensityPercent()) + "%");

    // Build sparse voxel octree using LaineKarrasOctree with GaiaVoxelWorld
    std::cout << "[VoxelGridNode] Building ESVO octree via LaineKarrasOctree..." << std::endl;

    // 1. Create GaiaVoxelWorld to store voxel entities
    GaiaVoxel::GaiaVoxelWorld voxelWorld;

    // 2. Populate voxelWorld from VoxelGrid data using batched sequential creation
    //    Note: Gaia ECS doesn't support concurrent structural changes, so we use single-threaded batching
    const auto& data = grid.GetData();

    // First pass: count solid voxels and collect requests
    std::vector<GaiaVoxel::VoxelCreationRequest> requests;
    requests.reserve(data.size() / 4);  // Estimate ~25% solid

    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                size_t idx = static_cast<size_t>(z) * resolution * resolution +
                             static_cast<size_t>(y) * resolution + x;
                if (data[idx] != 0) {
                    glm::vec3 pos(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                    GaiaVoxel::ComponentQueryRequest components[] = {
                        GaiaVoxel::Material{data[idx]}
                    };
                    requests.push_back(GaiaVoxel::VoxelCreationRequest{pos, components});
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
              << ", octreeMaterialsBuffer=" << octreeMaterialsBuffer << ", octreeConfigBuffer=" << octreeConfigBuffer
              << ", brickBaseIndexBuffer=" << brickBaseIndexBuffer << std::endl;

    // Output octree buffers
    ctx.Out(VoxelGridNodeConfig::OCTREE_NODES_BUFFER, octreeNodesBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_BRICKS_BUFFER, octreeBricksBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_MATERIALS_BUFFER, octreeMaterialsBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_CONFIG_BUFFER, octreeConfigBuffer);
    ctx.Out(VoxelGridNodeConfig::BRICK_BASE_INDEX_BUFFER, brickBaseIndexBuffer);

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
    ctx.Out(VoxelGridNodeConfig::BRICK_BASE_INDEX_BUFFER, brickBaseIndexBuffer);

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

    // Destroy brick base index buffer and memory
    if (brickBaseIndexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, brickBaseIndexBuffer, nullptr);
        brickBaseIndexBuffer = VK_NULL_HANDLE;
        LogCleanupProgress("brickBaseIndexBuffer destroyed");
    }

    if (brickBaseIndexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, brickBaseIndexMemory, nullptr);
        brickBaseIndexMemory = VK_NULL_HANDLE;
        LogCleanupProgress("brickBaseIndexMemory freed");
    }
}

void VoxelGridNode::LogCleanupProgress(const std::string& stage) {
    NODE_LOG_DEBUG("[VoxelGridNode::Cleanup] " + stage);
}

void VoxelGridNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[VoxelGridNode::CleanupImpl] Destroying octree buffers");

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

void VoxelGridNode::GenerateProceduralScene(VoxelGrid& grid) {
    std::cout << "[VoxelGridNode] GenerateProceduralScene: sceneType=\"" << sceneType << "\" (length=" << sceneType.length() << ")" << std::endl;
    NODE_LOG_INFO("Generating procedural scene: " + sceneType);

    if (sceneType == "cornell") {
        std::cout << "[VoxelGridNode] Matched 'cornell' - calling CornellBoxGenerator::Generate" << std::endl;
        CornellBoxGenerator::Generate(grid);
    } else if (sceneType == "cave") {
        std::cout << "[VoxelGridNode] Matched 'cave'" << std::endl;
        CaveSystemGenerator::Generate(grid);
    } else if (sceneType == "urban") {
        std::cout << "[VoxelGridNode] Matched 'urban'" << std::endl;
        UrbanGridGenerator::Generate(grid);
    } else {
        std::cout << "[VoxelGridNode] NO MATCH - using default test pattern (all solid)" << std::endl;
        // Default: test pattern (all solid)
        for (uint32_t z = 0; z < resolution; ++z) {
            for (uint32_t y = 0; y < resolution; ++y) {
                for (uint32_t x = 0; x < resolution; ++x) {
                    grid.Set(x, y, z, 1);  // Material ID 1 (not 255 - would be out of bounds)
                }
            }
        }
        NODE_LOG_INFO("Generated test pattern (all solid voxels)");
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

void VoxelGridNode::UploadESVOBuffers(const SVO::Octree& octree, const VoxelGrid& grid) {
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
    // Step 2: Build DENSE brick grid lookup: brickGridLookup[bx + by*N + bz*N*N] -> brickViewIndex
    // ============================================================================
    // UNIFIED APPROACH: Use local brick grid coordinates for lookup.
    // This is position-based (not topology-based) so it's view-independent.
    //
    // Key: linearized brick coordinate (bx + by*bricksPerAxis + bz*bricksPerAxis²)
    // Value: brickViewIndex (sparse index into brick data), or 0xFFFFFFFF if empty
    //
    // GPU shader computes brick coords from ESVO octant position in [0,1] local space.

    size_t brickGridLookupSize = static_cast<size_t>(bricksPerAxis) * bricksPerAxis * bricksPerAxis;
    std::vector<uint32_t> brickGridLookup(brickGridLookupSize, 0xFFFFFFFFu);

    size_t populatedBricks = 0;

    // Populate from brickGridToBrickView mapping (created in LaineKarrasOctree::rebuild)
    for (const auto& [gridKey, brickViewIdx] : rootBlock.brickGridToBrickView) {
        if (brickViewIdx >= brickViews.size()) continue;

        // Decode grid key: bx | (by << 10) | (bz << 20)
        uint32_t bx = gridKey & 0x3FF;
        uint32_t by = (gridKey >> 10) & 0x3FF;
        uint32_t bz = (gridKey >> 20) & 0x3FF;

        if (bx >= static_cast<uint32_t>(bricksPerAxis) ||
            by >= static_cast<uint32_t>(bricksPerAxis) ||
            bz >= static_cast<uint32_t>(bricksPerAxis)) {
            continue;  // Out of bounds
        }

        // Linearize: bx + by*N + bz*N*N
        size_t linearIdx = bx + by * bricksPerAxis + bz * bricksPerAxis * bricksPerAxis;
        if (linearIdx < brickGridLookup.size()) {
            brickGridLookup[linearIdx] = brickViewIdx;
            populatedBricks++;
        }
    }

    std::cout << "[VoxelGridNode::UploadESVOBuffers] Built brickGridLookup table: "
              << brickGridLookupSize << " entries (dense), "
              << populatedBricks << " populated bricks" << std::endl;

    // Debug: Print first few valid entries
    std::cout << "[VoxelGridNode::UploadESVOBuffers] Sample brickGridLookup entries:" << std::endl;
    int printed = 0;
    for (size_t i = 0; i < brickGridLookup.size() && printed < 10; ++i) {
        if (brickGridLookup[i] != 0xFFFFFFFFu) {
            uint32_t bz = static_cast<uint32_t>(i / (bricksPerAxis * bricksPerAxis));
            uint32_t by = static_cast<uint32_t>((i / bricksPerAxis) % bricksPerAxis);
            uint32_t bx = static_cast<uint32_t>(i % bricksPerAxis);
            std::cout << "  [brick(" << bx << "," << by << "," << bz << ")] -> sparse index " << brickGridLookup[i] << std::endl;
            printed++;
        }
    }

    // Use brickGridLookup as the base index buffer
    std::vector<uint32_t>& brickBaseIndex = brickGridLookup;

    VkDeviceSize bricksBufferSize = sparseBrickData.size() * sizeof(uint32_t);
    if (bricksBufferSize == 0) {
        // Create minimal buffer if no bricks
        sparseBrickData.resize(voxelsPerBrick, 0);
        bricksBufferSize = voxelsPerBrick * sizeof(uint32_t);
    }

    VkDeviceSize baseIndexBufferSize = brickBaseIndex.size() * sizeof(uint32_t);

    NODE_LOG_INFO("Uploading ESVO buffers: " +
                  std::to_string(nodesBufferSize) + " bytes (nodes), " +
                  std::to_string(bricksBufferSize) + " bytes (sparse bricks), " +
                  std::to_string(baseIndexBufferSize) + " bytes (base indices)");

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

    // ============================================================================
    // CREATE BRICK BASE INDEX BUFFER (binding 6)
    // ============================================================================
    VkBufferCreateInfo baseIndexBufferInfo{};
    baseIndexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    baseIndexBufferInfo.size = baseIndexBufferSize;
    baseIndexBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    baseIndexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = vkCreateBuffer(vulkanDevice->device, &baseIndexBufferInfo, nullptr, &brickBaseIndexBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to create brick base index buffer: " + std::to_string(result));
    }

    VkMemoryRequirements baseIndexMemReq;
    vkGetBufferMemoryRequirements(vulkanDevice->device, brickBaseIndexBuffer, &baseIndexMemReq);

    VkMemoryAllocateInfo baseIndexAllocInfo{};
    baseIndexAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    baseIndexAllocInfo.allocationSize = baseIndexMemReq.size;

    memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((baseIndexMemReq.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("[VoxelGridNode] Failed to find device-local memory for brick base index");
    }
    baseIndexAllocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(vulkanDevice->device, &baseIndexAllocInfo, nullptr, &brickBaseIndexMemory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[VoxelGridNode] Failed to allocate brick base index memory: " + std::to_string(result));
    }
    vkBindBufferMemory(vulkanDevice->device, brickBaseIndexBuffer, brickBaseIndexMemory, 0);

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

    // Upload brick base indices
    if (!brickBaseIndex.empty()) {
        uploadBuffer(brickBaseIndexBuffer, brickBaseIndex.data(), baseIndexBufferSize);
    }

    NODE_LOG_INFO("Uploaded ESVO buffers to GPU (sparse brick architecture)");
    std::cout << "[VoxelGridNode::UploadESVOBuffers] Upload complete - sparse bricks: "
              << brickViews.size() << ", grid lookup entries: " << brickGridLookupSize << std::endl;
}

} // namespace Vixen::RenderGraph
