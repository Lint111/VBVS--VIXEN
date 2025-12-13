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

    // Create memory tracking logger (enabled for benchmarking, terminal output disabled)
    memoryLogger_ = std::make_shared<GPUPerformanceLogger>("VoxelGrid_Memory", vulkanDevice, 1);
    memoryLogger_->SetEnabled(true);  // Enabled for benchmark data collection
    memoryLogger_->SetTerminalOutput(false);  // Disabled for clean terminal output
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

    // Output octree buffers from cached scene data
    VkBuffer octreeNodesBuffer = cachedSceneData_->esvoNodesBuffer;
    VkBuffer octreeBricksBuffer = cachedSceneData_->brickDataBuffer;
    VkBuffer octreeMaterialsBuffer = cachedSceneData_->materialsBuffer;
    VkBuffer octreeConfigBuffer = cachedSceneData_->octreeConfigBuffer;

    // Output resources
    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] OUTPUTTING NEW RESOURCES");
    NODE_LOG_DEBUG("  NEW octreeNodesBuffer=" + std::to_string(reinterpret_cast<uint64_t>(octreeNodesBuffer)) + ", octreeBricksBuffer=" + std::to_string(reinterpret_cast<uint64_t>(octreeBricksBuffer))
              + ", octreeMaterialsBuffer=" + std::to_string(reinterpret_cast<uint64_t>(octreeMaterialsBuffer)) + ", octreeConfigBuffer=" + std::to_string(reinterpret_cast<uint64_t>(octreeConfigBuffer)));

    ctx.Out(VoxelGridNodeConfig::OCTREE_NODES_BUFFER, octreeNodesBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_BRICKS_BUFFER, octreeBricksBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_MATERIALS_BUFFER, octreeMaterialsBuffer);
    ctx.Out(VoxelGridNodeConfig::OCTREE_CONFIG_BUFFER, octreeConfigBuffer);

    // Output compressed buffers (optional - only if compression is enabled)
    VkBuffer compressedColorBuffer = cachedSceneData_->compressedColorsBuffer;
    VkBuffer compressedNormalBuffer = cachedSceneData_->compressedNormalsBuffer;
    VkBuffer brickGridLookupBuffer = cachedSceneData_->brickGridLookupBuffer;

    if (compressedColorBuffer != VK_NULL_HANDLE) {
        ctx.Out(VoxelGridNodeConfig::COMPRESSED_COLOR_BUFFER, compressedColorBuffer);
        NODE_LOG_DEBUG("  COMPRESSED_COLOR_BUFFER=" + std::to_string(reinterpret_cast<uint64_t>(compressedColorBuffer)));
    }
    if (compressedNormalBuffer != VK_NULL_HANDLE) {
        ctx.Out(VoxelGridNodeConfig::COMPRESSED_NORMAL_BUFFER, compressedNormalBuffer);
        NODE_LOG_DEBUG("  COMPRESSED_NORMAL_BUFFER=" + std::to_string(reinterpret_cast<uint64_t>(compressedNormalBuffer)));
    }
    if (brickGridLookupBuffer != VK_NULL_HANDLE) {
        ctx.Out(VoxelGridNodeConfig::BRICK_GRID_LOOKUP_BUFFER, brickGridLookupBuffer);
        NODE_LOG_DEBUG("  BRICK_GRID_LOOKUP_BUFFER=" + std::to_string(reinterpret_cast<uint64_t>(brickGridLookupBuffer)));
    }

    // Output cached scene data for downstream nodes (AccelerationStructureNode)
    // This provides readonly access to the complete scene for building AS
    if (cachedSceneData_) {
        ctx.Out(VoxelGridNodeConfig::VOXEL_SCENE_DATA, cachedSceneData_.get());
        NODE_LOG_DEBUG("  VOXEL_SCENE_DATA=" + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_.get())));
    }

    // Output debug capture buffer with IDebugCapture interface attached
    // When connected with SlotRole::Debug, the gatherer will auto-collect it
    if (debugCaptureResource_ && debugCaptureResource_->IsValid()) {
        ctx.OutWithInterface(VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER,
                            debugCaptureResource_->GetBuffer(),
                            static_cast<Debug::IDebugCapture*>(debugCaptureResource_.get()));
        NODE_LOG_DEBUG("  DEBUG_CAPTURE_BUFFER=" + std::to_string(reinterpret_cast<uint64_t>(debugCaptureResource_->GetBuffer())));
    }

    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] OUTPUTS SET");

    NODE_LOG_INFO("Uploaded octree buffers successfully");

    // Print memory summary for Week 3 benchmarking
    if (memoryLogger_) {
        NODE_LOG_INFO("GPU MEMORY SUMMARY:\n" + memoryLogger_->GetMemorySummary() +
                     "Total GPU Memory: " + std::to_string(static_cast<int>(memoryLogger_->GetTotalTrackedMemoryMB())) + " MB");
    } else {
        NODE_LOG_WARNING("[VoxelGridNode] WARNING: memoryLogger_ is null, cannot print memory summary");
    }

    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] COMPLETED");
}

void VoxelGridNode::ExecuteImpl(TypedExecuteContext& ctx) {
    NODE_LOG_DEBUG("[VoxelGridNode::ExecuteImpl] ENTERED with taskIndex=" + std::to_string(ctx.taskIndex));
    // Re-output persistent resources every frame for variadic connections
    // When swapchain recompiles, descriptor gatherer re-queries these outputs

    NODE_LOG_INFO("=== VoxelGridNode::ExecuteImpl START ===");

    // Buffers are stored in cachedSceneData_, accessed directly for output
    if (cachedSceneData_) {
        NODE_LOG_INFO("  octreeNodesBuffer handle: " + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->esvoNodesBuffer)));
        NODE_LOG_INFO("  octreeBricksBuffer handle: " + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->brickDataBuffer)));
        NODE_LOG_INFO("  octreeMaterialsBuffer handle: " + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->materialsBuffer)));

        ctx.Out(VoxelGridNodeConfig::OCTREE_NODES_BUFFER, cachedSceneData_->esvoNodesBuffer);
        ctx.Out(VoxelGridNodeConfig::OCTREE_BRICKS_BUFFER, cachedSceneData_->brickDataBuffer);
        ctx.Out(VoxelGridNodeConfig::OCTREE_MATERIALS_BUFFER, cachedSceneData_->materialsBuffer);
        ctx.Out(VoxelGridNodeConfig::OCTREE_CONFIG_BUFFER, cachedSceneData_->octreeConfigBuffer);

        // Re-output compressed buffers (optional)
        if (cachedSceneData_->compressedColorsBuffer != VK_NULL_HANDLE) {
            ctx.Out(VoxelGridNodeConfig::COMPRESSED_COLOR_BUFFER, cachedSceneData_->compressedColorsBuffer);
        }
        if (cachedSceneData_->compressedNormalsBuffer != VK_NULL_HANDLE) {
            ctx.Out(VoxelGridNodeConfig::COMPRESSED_NORMAL_BUFFER, cachedSceneData_->compressedNormalsBuffer);
        }
        if (cachedSceneData_->brickGridLookupBuffer != VK_NULL_HANDLE) {
            ctx.Out(VoxelGridNodeConfig::BRICK_GRID_LOOKUP_BUFFER, cachedSceneData_->brickGridLookupBuffer);
        }
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
    // Cacher owns all GPU resources. We only release the shared_ptr reference.
    // When cachedSceneData_ is destroyed, the cacher decrements its reference count
    // and manages resource cleanup automatically.
    if (cachedSceneData_) {
        NODE_LOG_DEBUG("VoxelGridNode: Releasing cached scene data (cacher owns resources)");
        cachedSceneData_.reset();
        LogCleanupProgress("cached scene data released");
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

    NODE_LOG_INFO("VoxelGridNode: Scene created via cacher: " +
                  std::to_string(cachedSceneData_->nodeCount) + " nodes, " +
                  std::to_string(cachedSceneData_->brickCount) + " bricks, " +
                  std::to_string(cachedSceneData_->solidVoxelCount) + " voxels");

    NODE_LOG_DEBUG("[VoxelGridNode] Scene created via cacher:");
    NODE_LOG_DEBUG("  esvoNodesBuffer=" + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->esvoNodesBuffer)));
    NODE_LOG_DEBUG("  brickDataBuffer=" + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->brickDataBuffer)));
    NODE_LOG_DEBUG("  materialsBuffer=" + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->materialsBuffer)));
    NODE_LOG_DEBUG("  octreeConfigBuffer=" + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->octreeConfigBuffer)));
    NODE_LOG_DEBUG("  compressedColorsBuffer=" + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->compressedColorsBuffer)));
    NODE_LOG_DEBUG("  compressedNormalsBuffer=" + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->compressedNormalsBuffer)));
    NODE_LOG_DEBUG("  brickGridLookupBuffer=" + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->brickGridLookupBuffer)));
}

} // namespace Vixen::RenderGraph
