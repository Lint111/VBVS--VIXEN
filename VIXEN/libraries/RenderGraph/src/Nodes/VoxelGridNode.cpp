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

// ============================================================================
// CACHER REGISTRATION
// ============================================================================
// Note: Legacy helper methods (GenerateProceduralScene, UploadOctreeBuffers,
// UploadESVOBuffers, ExtractNodeData) removed - handled by VoxelSceneCacher.
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
