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

    // IMPORTANT: Do NOT destroy/recreate wrappers on every compile!
    // The descriptor gatherer caches VkBuffer handles at compile time.
    // If we destroy and recreate wrappers, the cached handles become stale,
    // causing "Invalid VkBuffer Object" validation errors.
    //
    // Instead, we only create wrappers if they don't exist or are invalid.
    // The same VkBuffer handles persist across recompiles.

    // Get command pool
    commandPool = ctx.In(VoxelGridNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[VoxelGridNode] COMMAND_POOL is null");
    }

    // Create GPU performance logger using centralized GPUQueryManager from VulkanDevice
    // Sprint 6.3 Phase 0: All nodes share the same query manager to prevent slot conflicts
    auto* queryMgrPtr = static_cast<GPUQueryManager*>(vulkanDevice->GetQueryManager());
    if (queryMgrPtr) {
        // Wrap raw pointer in shared_ptr with no-op deleter (VulkanDevice owns the manager)
        auto queryManager = std::shared_ptr<GPUQueryManager>(queryMgrPtr, [](GPUQueryManager*){});

        memoryLogger_ = std::make_shared<GPUPerformanceLogger>("VoxelGrid_Memory", queryManager);
        memoryLogger_->SetEnabled(true);
        memoryLogger_->SetTerminalOutput(false);

        if (nodeLogger) {
            nodeLogger->AddChild(memoryLogger_);
        }

        if (memoryLogger_->IsTimingSupported()) {
            NODE_LOG_INFO("[VoxelGrid_Memory] GPU performance timing enabled");
        } else {
            NODE_LOG_WARNING("[VoxelGrid_Memory] GPU timing not supported on this device");
        }
    } else {
        NODE_LOG_WARNING("[VoxelGrid_Memory] GPUQueryManager not available from VulkanDevice");
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

    // Create ray trace buffer for per-ray traversal capture (only if not already valid)
    // Each ray captures up to 64 steps * 48 bytes + 16 byte header = 3088 bytes/ray
    // 256 rays = ~790KB buffer, reasonable for debug capture
    // Uses RayTraceBuffer directly - has conversion_type = VkBuffer for auto descriptor extraction
    // NOTE: These buffers are REQUIRED by shaders (binding 4 and 8) - failure to create them
    // will cause VK_ERROR_DEVICE_LOST when the shader tries to access null buffers.
    constexpr uint32_t RAY_TRACE_CAPACITY = 256;

    // Only create if not already valid - preserves VkBuffer handles across recompiles
    if (!debugCaptureResource_ || !debugCaptureResource_->IsValid()) {
        debugCaptureResource_ = std::make_unique<Debug::RayTraceBuffer>(RAY_TRACE_CAPACITY);
        if (!debugCaptureResource_->Create(vulkanDevice->device, *vulkanDevice->gpu)) {
            NODE_LOG_ERROR("[VoxelGridNode::CompileImpl] FATAL: Failed to create ray trace buffer (binding 4)");
            throw std::runtime_error("[VoxelGridNode] Failed to create ray trace buffer - shader binding 4 would be null");
        }
        NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] Created ray trace buffer: " +
                      std::to_string(RAY_TRACE_CAPACITY) + " rays, buffer=" +
                      std::to_string(reinterpret_cast<uint64_t>(debugCaptureResource_->GetVkBuffer())));
    } else {
        NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] Reusing existing ray trace buffer: buffer=" +
                      std::to_string(reinterpret_cast<uint64_t>(debugCaptureResource_->GetVkBuffer())));
    }

    // Create shader counters buffer for avgVoxelsPerRay metrics (only if not already valid)
    // Uses ShaderCountersBuffer directly - has conversion_type = VkBuffer for auto descriptor extraction
    if (!shaderCountersResource_ || !shaderCountersResource_->IsValid()) {
        shaderCountersResource_ = std::make_unique<Debug::ShaderCountersBuffer>();
        if (!shaderCountersResource_->Create(vulkanDevice->device, *vulkanDevice->gpu)) {
            NODE_LOG_ERROR("[VoxelGridNode::CompileImpl] FATAL: Failed to create shader counters buffer (binding 8)");
            throw std::runtime_error("[VoxelGridNode] Failed to create shader counters buffer - shader binding 8 would be null");
        }
        NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] Created shader counters buffer: " +
                      std::to_string(reinterpret_cast<uint64_t>(shaderCountersResource_->GetVkBuffer())));
    } else {
        NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] Reusing existing shader counters buffer: buffer=" +
                      std::to_string(reinterpret_cast<uint64_t>(shaderCountersResource_->GetVkBuffer())));
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

    // Output debug capture buffer (wrapper with conversion_type = VkBuffer)
    // Resource system automatically extracts VkBuffer for descriptor binding
    if (debugCaptureResource_ && debugCaptureResource_->IsValid()) {
        ctx.Out(VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER, debugCaptureResource_.get());
        NODE_LOG_DEBUG("  DEBUG_CAPTURE_BUFFER (wrapper)=" + std::to_string(reinterpret_cast<uint64_t>(debugCaptureResource_->GetVkBuffer())));
    }

    // Output shader counters buffer (wrapper with conversion_type = VkBuffer)
    // Resource system automatically extracts VkBuffer for descriptor binding
    // FIX APPLIED: VoxelGridNodeConfig.h now includes full ShaderCountersBuffer.h
    // so HasConversionType_v<Debug::ShaderCountersBuffer> correctly returns true.
    // See: HacknPlan #61 for debugging history.
    if (shaderCountersResource_ && shaderCountersResource_->IsValid()) {
        ctx.Out(VoxelGridNodeConfig::SHADER_COUNTERS_BUFFER, shaderCountersResource_.get());
        NODE_LOG_DEBUG("  SHADER_COUNTERS_BUFFER (wrapper)=" + std::to_string(reinterpret_cast<uint64_t>(shaderCountersResource_->GetVkBuffer())));
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

    // Skip execution if resources were cleaned up (pending recompile)
    // This guards against dispatching with null/invalid buffer handles
    if (!cachedSceneData_ || !shaderCountersResource_ || !shaderCountersResource_->IsValid() || !debugCaptureResource_ || !debugCaptureResource_->IsValid()) {
        NODE_LOG_DEBUG("[VoxelGridNode::ExecuteImpl] Skipping - resources not available (pending recompile)");
        return;
    }

    NODE_LOG_INFO("=== VoxelGridNode::ExecuteImpl START ===");

    // Buffers are stored in cachedSceneData_, accessed directly for output
    // Validate buffer handles before outputting (guards against destroyed buffers)
    if (cachedSceneData_ && cachedSceneData_->esvoNodesBuffer != VK_NULL_HANDLE) {
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
    } else {
        NODE_LOG_WARNING("[VoxelGridNode::ExecuteImpl] Core buffers are null - skipping output");
        return;
    }

    // Re-output cached scene data for downstream nodes
    if (cachedSceneData_) {
        ctx.Out(VoxelGridNodeConfig::VOXEL_SCENE_DATA, cachedSceneData_.get());
    }

    // Re-output debug capture buffer (wrapper with conversion_type = VkBuffer)
    if (debugCaptureResource_ && debugCaptureResource_->IsValid()) {
        // Reset buffer before each frame to allow fresh capture
        debugCaptureResource_->Reset(vulkanDevice->device);
        ctx.Out(VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER, debugCaptureResource_.get());
    }

    // Re-output shader counters buffer (wrapper with conversion_type = VkBuffer)
    if (shaderCountersResource_ && shaderCountersResource_->IsValid()) {
        // Reset counters before each frame
        shaderCountersResource_->Reset(vulkanDevice->device);
        ctx.Out(VoxelGridNodeConfig::SHADER_COUNTERS_BUFFER, shaderCountersResource_.get());
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

const Debug::GPUShaderCounters* VoxelGridNode::ReadShaderCounters() {
    if (!vulkanDevice || vulkanDevice->device == VK_NULL_HANDLE) {
        return nullptr;
    }

    if (!shaderCountersResource_ || !shaderCountersResource_->IsValid()) {
        return nullptr;
    }

    // Read data from GPU (uses mapped memory, so fast)
    uint32_t count = shaderCountersResource_->Read(vulkanDevice->device);
    if (count == 0) {
        return nullptr;
    }

    // Return pointer to the counter data
    return &shaderCountersResource_->GetCounters();
}

void VoxelGridNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[VoxelGridNode::CleanupImpl] Destroying octree buffers");

    // CRITICAL: Release GPU resources (QueryPools) BEFORE device operations.
    // GPU resources (QueryPools) will be automatically released by GPUQueryManager destructor
    LogCleanupProgress("memoryLogger GPU resources will auto-release");

    if (!vulkanDevice) {
        NODE_LOG_DEBUG("[VoxelGridNode::CleanupImpl] Device unavailable, skipping cleanup");
        return;
    }

    vkDeviceWaitIdle(vulkanDevice->device);
    DestroyOctreeBuffers();

    // CRITICAL FIX: Clear output Resources BEFORE destroying wrapper objects.
    // The Resource objects contain descriptorExtractor_ lambdas that capture
    // pointers to our wrapper objects (debugCaptureResource_, shaderCountersResource_).
    // If we destroy the wrappers without clearing Resources first, those lambdas
    // will hold dangling pointers, causing use-after-free when downstream nodes
    // call GetDescriptorHandle() during recompilation.
    //
    // This fixes validation errors:
    //   "vkUpdateDescriptorSets(): Invalid VkBuffer Object" (stale handles)
    //   "storage buffer descriptor using buffer VkBuffer 0x0" (freed memory)
    constexpr uint32_t DEBUG_CAPTURE_INDEX = VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER_Slot::index;
    constexpr uint32_t SHADER_COUNTERS_INDEX = VoxelGridNodeConfig::SHADER_COUNTERS_BUFFER_Slot::index;

    if (Resource* debugRes = GetOutput(DEBUG_CAPTURE_INDEX, 0)) {
        debugRes->Clear();
        NODE_LOG_DEBUG("[VoxelGridNode::CleanupImpl] Cleared DEBUG_CAPTURE_BUFFER resource");
    }
    if (Resource* countersRes = GetOutput(SHADER_COUNTERS_INDEX, 0)) {
        countersRes->Clear();
        NODE_LOG_DEBUG("[VoxelGridNode::CleanupImpl] Cleared SHADER_COUNTERS_BUFFER resource");
    }

    // Clean up debug capture resource
    if (debugCaptureResource_ && debugCaptureResource_->IsValid()) {
        debugCaptureResource_->Destroy(vulkanDevice->device);
    }
    debugCaptureResource_.reset();
    LogCleanupProgress("debugCaptureResource destroyed");

    // Clean up shader counters resource
    if (shaderCountersResource_ && shaderCountersResource_->IsValid()) {
        shaderCountersResource_->Destroy(vulkanDevice->device);
    }
    shaderCountersResource_.reset();
    LogCleanupProgress("shaderCountersResource destroyed");

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
