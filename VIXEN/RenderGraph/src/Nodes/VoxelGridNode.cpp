#include "Nodes/VoxelGridNode.h"
#include "Data/SceneGenerator.h"
#include "Data/VoxelOctree.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include <cmath>
#include <cstring>

using VIXEN::RenderGraph::VoxelGrid;
using VIXEN::RenderGraph::SparseVoxelOctree;
using VIXEN::RenderGraph::CornellBoxGenerator;
using VIXEN::RenderGraph::CaveSystemGenerator;
using VIXEN::RenderGraph::UrbanGridGenerator;
using VIXEN::RenderGraph::OctreeNode;
using VIXEN::RenderGraph::VoxelBrick;

namespace Vixen::RenderGraph {

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
    VulkanDevicePtr devicePtr = ctx.In(VoxelGridNodeConfig::VULKAN_DEVICE_IN);
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
    VoxelGrid grid(resolution);
    GenerateProceduralScene(grid);

    size_t voxelCount = resolution * resolution * resolution;
    NODE_LOG_INFO("Generated " + std::to_string(voxelCount) + " voxels, density=" +
                  std::to_string(grid.GetDensityPercent()) + "%");

    // Build sparse voxel octree
    SparseVoxelOctree octree;
    octree.BuildFromGrid(grid.GetData(), resolution);

    NODE_LOG_INFO("Built octree: " +
                  std::to_string(octree.GetNodes().size()) + " nodes, " +
                  std::to_string(octree.GetBricks().size()) + " bricks, " +
                  "compression=" + std::to_string(octree.GetCompressionRatio(resolution)) + ":1");

    // Upload octree buffers
    UploadOctreeBuffers(octree);

    // Output resources
    std::cout << "!!!! [VoxelGridNode::CompileImpl] OUTPUTTING NEW RESOURCES !!!!" << std::endl;
    std::cout << "  NEW octreeNodesBuffer=" << octreeNodesBuffer << ", octreeBricksBuffer=" << octreeBricksBuffer << ", octreeMaterialsBuffer=" << octreeMaterialsBuffer << std::endl;

    // Output octree buffers
    ctx.Out(VoxelGridNodeConfig::OCTREE_NODES_BUFFER, octreeNodesBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_BRICKS_BUFFER, octreeBricksBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_MATERIALS_BUFFER, octreeMaterialsBuffer);

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

    NODE_LOG_INFO("=== VoxelGridNode::ExecuteImpl END ===");
    NODE_LOG_DEBUG("[VoxelGridNode::ExecuteImpl] COMPLETED");
}

void VoxelGridNode::CleanupImpl(TypedCleanupContext& ctx) {
    std::cout << "!!!! [VoxelGridNode::CleanupImpl] DESTROYING RESOURCES !!!!" << std::endl;
    std::cout << "  octreeNodesBuffer=" << octreeNodesBuffer << ", octreeBricksBuffer=" << octreeBricksBuffer << ", octreeMaterialsBuffer=" << octreeMaterialsBuffer << std::endl;
    NODE_LOG_INFO("VoxelGridNode cleanup");

    if (!vulkanDevice) {
        return;
    }

    vkDeviceWaitIdle(vulkanDevice->device);

    // Cleanup octree buffers
    if (octreeNodesBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeNodesBuffer, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed octreeNodesBuffer" << std::endl;
        octreeNodesBuffer = VK_NULL_HANDLE;
    }

    if (octreeNodesMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeNodesMemory, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed octreeNodesMemory" << std::endl;
        octreeNodesMemory = VK_NULL_HANDLE;
    }

    if (octreeBricksBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeBricksBuffer, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed octreeBricksBuffer" << std::endl;
        octreeBricksBuffer = VK_NULL_HANDLE;
    }

    if (octreeBricksMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeBricksMemory, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed octreeBricksMemory" << std::endl;
        octreeBricksMemory = VK_NULL_HANDLE;
    }

    if (octreeMaterialsBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeMaterialsBuffer, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed octreeMaterialsBuffer" << std::endl;
        octreeMaterialsBuffer = VK_NULL_HANDLE;
    }

    if (octreeMaterialsMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeMaterialsMemory, nullptr);
        std::cout << "  [VoxelGridNode::CleanupImpl] Destroyed octreeMaterialsMemory" << std::endl;
        octreeMaterialsMemory = VK_NULL_HANDLE;
    }

    std::cout << "!!!! [VoxelGridNode::CleanupImpl] CLEANUP COMPLETE !!!!" << std::endl;
}

void VoxelGridNode::GenerateProceduralScene(VoxelGrid& grid) {
    NODE_LOG_INFO("Generating procedural scene: " + sceneType);

    if (sceneType == "cornell") {
        CornellBoxGenerator::Generate(grid);
    } else if (sceneType == "cave") {
        CaveSystemGenerator::Generate(grid);
    } else if (sceneType == "urban") {
        UrbanGridGenerator::Generate(grid);
    } else {
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

void VoxelGridNode::UploadOctreeBuffers(const SparseVoxelOctree& octree) {
    const auto& nodes = octree.GetNodes();
    const auto& bricks = octree.GetBricks();

    VkDeviceSize nodesBufferSize = nodes.size() * sizeof(OctreeNode);
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
    // Material 12-19: Reserved
    for (uint32_t i = 12; i < 20; ++i) {
        defaultMaterials[i] = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    }
    // Material 20: Ceiling light (emissive white)
    defaultMaterials[20] = {{1.0f, 1.0f, 0.9f}, 0.0f, 0.0f, 5.0f, {0.0f, 0.0f}};

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

        // Copy data to staging buffer
        void* data;
        vkMapMemory(vulkanDevice->device, nodesStagingMemory, 0, nodesBufferSize, 0, &data);
        std::memcpy(data, nodes.data(), nodesBufferSize);
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

} // namespace Vixen::RenderGraph
