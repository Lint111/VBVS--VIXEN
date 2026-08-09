#include "Nodes/VoxelGridNode.h"
#include "Core/NodeRegistration.h"
#include "SceneGenerator.h"
#include "Data/VoxelOctree.h" // Legacy - will be removed
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "Core/RenderGraph.h"
#include "Core/TaskProfiles/SimpleTaskProfile.h"  // Sprint 6.5: Profile integration
#include "MainCacher.h"
#include "VoxelSceneCacher.h"
#include <algorithm>  // round 11: std::min for pixel-ring valid-slot clamp
#include <cmath>
#include <cstdlib>  // Task 0.2: std::getenv("VIXEN_DEBUG_CAPTURE")
#include <cstring>
#include <fstream>
#include <iostream>  // [FarFieldCount] boot-summary print (round-3 fix item 3)
#include <span>

// New SVO library integration
#include "SVOBuilder.h"
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"   // For GaiaVoxelWorld entity-based voxel storage
#include "VoxelComponents.h"  // For GaiaVoxel::Material component

using Vixen::SVO::VoxelGrid;
using Vixen::SVO::SceneGeneratorFactory;
using Vixen::SVO::SceneGeneratorParams;
using Vixen::SVO::ISceneGenerator;
using Vixen::SVO::VoxelDataCache;

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

    // Sprint 6.5: Register compile-time task profile for cost estimation
    std::string profileId = GetInstanceName() + "_compile";
    compileProfile_ = GetOrCreateProfile<SimpleTaskProfile>(profileId, profileId, "pipeline");
    if (compileProfile_) {
        RegisterPhaseProfile(VirtualTaskPhase::Compile, compileProfile_);
        NODE_LOG_INFO("[VoxelGridNode] Registered compile profile: " + profileId);
    }

    NODE_LOG_DEBUG("[VoxelGridNode::SetupImpl] COMPLETED");
}

void VoxelGridNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[VoxelGridNode::CompileImpl] ENTERED with taskIndex=" + std::to_string(ctx.taskIndex));
    NODE_LOG_INFO("=== VoxelGridNode::CompileImpl START ===");

    // Sprint 6.5: Start compile timing (RAII - records on scope exit)
    auto compileSample = compileProfile_ ? compileProfile_->Sample() : ITaskProfile::Sampler(nullptr);

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

    // memoryLogger_ here is used ONLY for GPUPerformanceLogger's memory-tracking API
    // (RegisterBufferAllocation/GetMemorySummary — see PrintGPUMemorySummary below), never for
    // GPU timestamp recording: this node never calls BeginFrame/RecordDispatchStart/
    // RecordDispatchEnd on it. Passing a real GPUQueryManager here therefore allocated a query
    // slot that was reset/written by nobody — GPUQueryManager::AllAllocatedSlotsReset requires
    // EVERY allocated slot to have been reset before it will read back ANY slot's results, so
    // this dead slot permanently starved every other consumer sharing the manager (e.g.
    // ComputeDispatchNode's raymarch timing), pinning PerfCsvWriter's esvo_traverse_shade_ms
    // column at 0 forever. Pass nullptr instead — the constructor handles that gracefully
    // (no slot allocated, all timing methods no-op) and the memory-tracking API is unaffected.
    memoryLogger_ = std::make_shared<GPUPerformanceLogger>("VoxelGrid_Memory", nullptr);
    memoryLogger_->SetEnabled(true);
    memoryLogger_->SetTerminalOutput(false);

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
        // IDebugCapture identity: DescriptorResourceGathererNode::ProcessSlot detects this
        // resource as debug-capturable via GetInterface<IDebugCapture>() (now implemented
        // directly by RayTraceBuffer) and DebugBufferReaderNode uses GetDebugName() for its
        // export filename/log lines and GetBindingIndex() for its own diagnostics.
        debugCaptureResource_->SetDebugName(GetInstanceName() + "_rayTrace");
        debugCaptureResource_->SetBindingIndex(4u);
        // Task 0.2 (Baked-Content Perf Audit D2): captureEnabled_ defaults false (RayTraceBuffer.h)
        // so DebugBufferReaderNode's every-Nth-frame vkWaitForFences(UINT64_MAX) drain + export
        // stays off during perf benches; VIXEN_DEBUG_CAPTURE=1 re-enables it for an actual
        // debugging session (same knob BuildRenderGraph.cpp uses for the companion AUTO_EXPORT
        // parameter -- both must agree, since DebugBufferReaderNode checks IsCaptureEnabled() in
        // addition to its own AUTO_EXPORT gate).
        debugCaptureResource_->SetCaptureEnabled(std::getenv("VIXEN_DEBUG_CAPTURE") != nullptr);
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

        // Output CPU voxel world (owned by cachedSceneData_) for CPU-side queries (e.g. picking).
        // Lives as long as the cached scene data; published as a plain pointer (null-safe).
        // Validity is identical on cache-miss and cache-hit: the world is part of the cached
        // resource, so a hit returns the same populated VoxelSceneData a fresh build would.
        Vixen::GaiaVoxel::GaiaVoxelWorld* voxelWorld = cachedSceneData_->voxelWorld.get();
        ctx.Out(VoxelGridNodeConfig::VOXEL_WORLD, voxelWorld);
        NODE_LOG_INFO("[VoxelGridNode::CompileImpl] VOXEL_WORLD output ptr=" +
                      std::to_string(reinterpret_cast<uint64_t>(voxelWorld)) +
                      (voxelWorld ? " (valid - CPU picking queries available)" : " (NULL - no CPU world on cached scene!)"));
    }

    // Output debug capture buffer (wrapper with conversion_type = VkBuffer). conversion_type
    // only makes the resource system auto-extract VkBuffer for descriptor binding — it does NOT
    // register RayTraceBuffer's IDebugCapture interface (a separate, unrelated mechanism), so a
    // plain ctx.Out() here left DescriptorResourceGathererNode::ProcessSlot's
    // GetInterface<IDebugCapture>() check always failing and the whole ray-trace debug-export
    // pipeline silently dead. OutWithInterface does both in one call (see its doc comment).
    if (debugCaptureResource_ && debugCaptureResource_->IsValid()) {
        ctx.OutWithInterface(VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER, debugCaptureResource_.get(),
                             static_cast<Debug::IDebugCapture*>(debugCaptureResource_.get()));
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

    NODE_LOG_DEBUG("=== VoxelGridNode::ExecuteImpl START ===");

    // Buffers are stored in cachedSceneData_, accessed directly for output
    // Validate buffer handles before outputting (guards against destroyed buffers)
    if (cachedSceneData_ && cachedSceneData_->esvoNodesBuffer != VK_NULL_HANDLE) {
        NODE_LOG_DEBUG("  octreeNodesBuffer handle: " + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->esvoNodesBuffer)));
        NODE_LOG_DEBUG("  octreeBricksBuffer handle: " + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->brickDataBuffer)));
        NODE_LOG_DEBUG("  octreeMaterialsBuffer handle: " + std::to_string(reinterpret_cast<uint64_t>(cachedSceneData_->materialsBuffer)));

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
        // Re-output CPU voxel world (owned by cachedSceneData_) so variadic consumers
        // (e.g. picking) re-resolve it on recompile. Null-safe.
        ctx.Out(VoxelGridNodeConfig::VOXEL_WORLD, cachedSceneData_->voxelWorld.get());
    }

    // Re-output debug capture buffer (wrapper with conversion_type = VkBuffer). See CompileImpl's
    // comment above: OutWithInterface is required (not plain ctx.Out()) so the resource keeps
    // being detected as debug-capturable by DescriptorResourceGathererNode every frame.
    if (debugCaptureResource_ && debugCaptureResource_->IsValid()) {
        // Reset buffer before each frame to allow fresh capture
        debugCaptureResource_->Reset(vulkanDevice->device);
        ctx.OutWithInterface(VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER, debugCaptureResource_.get(),
                             static_cast<Debug::IDebugCapture*>(debugCaptureResource_.get()));
    }

    // Re-output shader counters buffer (wrapper with conversion_type = VkBuffer)
    if (shaderCountersResource_ && shaderCountersResource_->IsValid()) {
        // Reset counters before each frame
        shaderCountersResource_->Reset(vulkanDevice->device);
        ctx.Out(VoxelGridNodeConfig::SHADER_COUNTERS_BUFFER, shaderCountersResource_.get());
    }

    NODE_LOG_DEBUG("=== VoxelGridNode::ExecuteImpl END ===");
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

    // W-COMPOSED round-3 fix item 3 (mandatory observability, 3rd request):
    // unconditional per-boot far-field-cutoff counter, printed the same way
    // BodyOctreeSceneNode's [BrickDataHash] does -- std::cout, not the
    // per-node logger (which is disabled by default for these nodes) or the
    // GPU trace-hook route (proven unwired for the live app).
    if (debugCaptureResource_ && debugCaptureResource_->IsValid()) {
        // Round 12 instrument fix (a): every FarField* counter below is a
        // boot-lifetime accumulation (RayTraceBuffer::Reset() never clears
        // them, by design -- see TraceBufferHeader's "Never reset" comments).
        // Batch 11 found bare "n=" prints being misread as per-frame values
        // (347,400 > screen pixel count, an impossibility per-frame). Fix:
        // divide by the boot's frame count at print time, labeled. Frame
        // count comes straight from VIXEN_EXIT_AFTER_FRAMES (the boot
        // contract always sets it for these gates) rather than plumbing
        // VulkanApplicationBase::frameCounter_ through RenderGraph -- it's
        // the smaller, already-available number and CleanupImpl only runs
        // once all frames have completed.
        uint64_t exitAfterFrames = 0;
        if (const char* env = std::getenv("VIXEN_EXIT_AFTER_FRAMES")) {
            exitAfterFrames = std::strtoull(env, nullptr, 10);
        }
        const double frames = exitAfterFrames > 0 ? static_cast<double>(exitAfterFrames) : 1.0;
        auto perFrame = [frames](uint32_t n) { return static_cast<double>(n) / frames; };

        const uint32_t farFieldCandidates = debugCaptureResource_->ReadFarFieldCandidates(vulkanDevice->device);
        std::cout << "[FarFieldCandidates] n=" << farFieldCandidates
                   << " (" << perFrame(farFieldCandidates) << "/frame over " << exitAfterFrames << " frames)" << std::endl;
        const uint32_t farFieldCount = debugCaptureResource_->ReadFarFieldCount(vulkanDevice->device);
        std::cout << "[FarFieldCount] n=" << farFieldCount
                   << " (" << perFrame(farFieldCount) << "/frame over " << exitAfterFrames << " frames)" << std::endl;

        // Round-6 blocker-1 probe: min/max of both far-field gate operands.
        // Not a count -- no per-frame division applies.
        const auto ranges = debugCaptureResource_->ReadFarFieldRanges(vulkanDevice->device);
        std::cout << "[FarFieldGateLhs] min=" << ranges.lhsMin << " max=" << ranges.lhsMax << std::endl;
        // BATCH 38: the ENTRY dispatch gate's own LHS range. The line above is the
        // mid-march safety net + RT twin only (batch-37: blind to the entry path).
        const auto entryRange = debugCaptureResource_->ReadEntryGateRange(vulkanDevice->device);
        std::cout << "[EntryGateLhs] min=" << entryRange.lhsMin << " max=" << entryRange.lhsMax << std::endl;
        std::cout << "[FarFieldGateRhs] min=" << ranges.rhsMin << " max=" << ranges.rhsMax << std::endl;

        // Round-6 blocker-2 probe: raw RT TLAS candidate-loop entries.
        const uint32_t rtLoopEntries = debugCaptureResource_->ReadRtLoopEntries(vulkanDevice->device);
        std::cout << "[RtLoopEntries] n=" << rtLoopEntries
                   << " (" << perFrame(rtLoopEntries) << "/frame over " << exitAfterFrames << " frames)" << std::endl;

        // Round-17 probe: octree-3-only candidate-loop breakdown -- isolates
        // whether the far body's ~4 candidates/frame gap is (a) the TLAS/BLAS
        // not proposing octree-3 candidates at all, (b) the tCellEnter>=bestT
        // early-continue eating them, or (c) genuine gate-reach parity.
        const auto oct3Stats = debugCaptureResource_->ReadFarFieldOct3Stats(vulkanDevice->device);
        std::cout << "[RtLoopEntriesOct3] n=" << oct3Stats.loopEntries
                   << " (" << perFrame(oct3Stats.loopEntries) << "/frame over " << exitAfterFrames << " frames)" << std::endl;
        std::cout << "[FarFieldGateRejectOct3] n=" << oct3Stats.gateReject
                   << " (" << perFrame(oct3Stats.gateReject) << "/frame over " << exitAfterFrames << " frames)" << std::endl;
        std::cout << "[FarFieldCandidatesOct3] n=" << oct3Stats.candidatesReachingGate
                   << " (" << perFrame(oct3Stats.candidatesReachingGate) << "/frame over " << exitAfterFrames << " frames)" << std::endl;

        // Batch-24 FARGEN: rect-scoped generation funnel over the far clusters
        // c1∪c2 (x363-390,y239-260) -- rectRays (invocation census) vs
        // rectCellEntries (candidate/cell entries) vs rectGateCross (gate
        // passes) localizes where those pixels' candidates die relative to
        // ESVO's reference lit count.
        const auto rectStats = debugCaptureResource_->ReadFarFieldRectStats(vulkanDevice->device);
        std::cout << "[FarFieldRectRays] n=" << rectStats.rays
                   << " (" << perFrame(rectStats.rays) << "/frame over " << exitAfterFrames << " frames)" << std::endl;
        std::cout << "[FarFieldRectCellEntries] n=" << rectStats.cellEntries
                   << " (" << perFrame(rectStats.cellEntries) << "/frame over " << exitAfterFrames << " frames)" << std::endl;
        std::cout << "[FarFieldRectGateCross] n=" << rectStats.gateCross
                   << " (" << perFrame(rectStats.gateCross) << "/frame over " << exitAfterFrames << " frames)" << std::endl;

        // Batch-25 JOB 2: 8-bucket FarFieldGateLhs histogram, same rect scope,
        // edges around the 0.9375 gate threshold.
        const auto lhsHist = debugCaptureResource_->ReadFarFieldRectLhsHistogram(vulkanDevice->device);
        std::cout << "[FarFieldRectLhsHistogram] <0.25=" << lhsHist.buckets[0]
                   << " <0.5=" << lhsHist.buckets[1]
                   << " <0.75=" << lhsHist.buckets[2]
                   << " <0.9375=" << lhsHist.buckets[3]
                   << " <1.25=" << lhsHist.buckets[4]
                   << " <2=" << lhsHist.buckets[5]
                   << " <4=" << lhsHist.buckets[6]
                   << " >=4=" << lhsHist.buckets[7] << std::endl;

        // Batch-27 JOB 2: ESVO's own cutoff criterion, SAME rect scope, SAME
        // boot -- side-by-side with FarFieldGateLhs/Rhs above. lhs/rhs here are
        // LOCAL/NORMALIZED octree-space (tv_max, scale_exp2); contrast with the
        // world-space FarFieldGate operands.
        const auto esvoOps = debugCaptureResource_->ReadEsvoCutoffOperands(vulkanDevice->device);
        std::cout << "[EsvoCutoffLhs] min=" << esvoOps.lhsMin << " max=" << esvoOps.lhsMax << std::endl;
        std::cout << "[EsvoCutoffRhs] min=" << esvoOps.rhsMin << " max=" << esvoOps.rhsMax << std::endl;
        std::cout << "[EsvoCutoffLhsHistogram] <0.25=" << esvoOps.histogram[0]
                   << " <0.5=" << esvoOps.histogram[1]
                   << " <0.75=" << esvoOps.histogram[2]
                   << " <0.9375=" << esvoOps.histogram[3]
                   << " <1.25=" << esvoOps.histogram[4]
                   << " <2=" << esvoOps.histogram[5]
                   << " <4=" << esvoOps.histogram[6]
                   << " >=4=" << esvoOps.histogram[7] << std::endl;
        std::cout << "[EsvoCutoffCrossLevel] min=" << esvoOps.crossLevelMin
                   << " max=" << esvoOps.crossLevelMax << std::endl;

        // Batch-29 JOB 3: rect-agnostic 8-bucket histogram of the LEVEL
        // mipPolicyLevel resolved to at every descendToNodeOrdinal call
        // (VIXEN_MIP_POLICY only -- all-zero on a flag-off boot, since the
        // shader never calls recordPolicyLevel in that build).
        const auto policyLevels = debugCaptureResource_->ReadPolicyLevelHistogram(vulkanDevice->device);
        std::cout << "[PolicyLevelHistogram] L0=" << policyLevels.buckets[0]
                   << " L1=" << policyLevels.buckets[1]
                   << " L2=" << policyLevels.buckets[2]
                   << " L3=" << policyLevels.buckets[3]
                   << " L4=" << policyLevels.buckets[4]
                   << " L5=" << policyLevels.buckets[5]
                   << " L6=" << policyLevels.buckets[6]
                   << " L7plus=" << policyLevels.buckets[7] << std::endl;

        // Batch-29 JOB 4: rect-scoped attribution across ESVO's five
        // shadeFromMipSample call sites (batch-28b validator's "make
        // attribution measured" ask) -- see recordEsvoMipArm's header
        // comment (SceneBindings.glsl) for the arm index -> call site map.
        // Batch-30 stream B adds policyLevel (arm 5, VIXEN_MIP_POLICY only) --
        // the streaming-grace(1)->policy(5) shift is the gate signal.
        const auto mipArms = debugCaptureResource_->ReadEsvoMipArmStats(vulkanDevice->device);
        std::cout << "[EsvoMipArmHits] tierCrossSubpixel=" << mipArms.hits[0]
                   << " streamingGrace=" << mipArms.hits[1]
                   << " deliberateLod=" << mipArms.hits[2]
                   << " tierCrossChildMiss=" << mipArms.hits[3]
                   << " hopExhausted=" << mipArms.hits[4]
                   << " policyLevel=" << mipArms.hits[5] << std::endl;

        // Batch-32 JOB 1: level-sensitive far-field counter -- min/max/mean of
        // the LEVEL that actually fed a shaded far pixel (recorded at the mip-
        // sample call site, both twins), so a level-selection change CAN move
        // this even though the FarField*/Esvo*/Rect* population counters
        // around the policy branch can't (they sit outside the ifdef).
        const auto sampledLevel = debugCaptureResource_->ReadFarFieldSampledLevelStats(vulkanDevice->device);
        const double sampledLevelMean = sampledLevel.count > 0
            ? static_cast<double>(sampledLevel.sum) / static_cast<double>(sampledLevel.count)
            : 0.0;
        std::cout << "[FarFieldSampledLevel] min=" << sampledLevel.min
                   << " max=" << sampledLevel.max
                   << " mean=" << sampledLevelMean
                   << " n=" << sampledLevel.count
                   << " (" << perFrame(sampledLevel.count) << "/frame over " << exitAfterFrames << " frames)" << std::endl;
        // Standing rule (batch-13 postmortem): min seeded 0xFFFFFFFF at
        // Create(); a real min of 0 is only credible with count > 0.
        if (sampledLevel.count == 0u && sampledLevel.min != 0xFFFFFFFFu) {
            std::cout << "[FarFieldSampledLevel] SEEDED-SANITY WARNING: "
                          "count=0 but min!=0xFFFFFFFF -- looks like an "
                          "unseeded/stale atomicMin. Verify Create() seeds this field."
                       << std::endl;
        }

        // Batch-33 JOB 2: [FarFieldSampleIntensity] -- luminance of the
        // shaded mip color at the SAME call site as FarFieldSampledLevel
        // (shares its count). Distinguishes "level chosen" from "sample
        // value" -- the batch-32 open ruling needs this: ESVO dimmed under
        // policy (rect mean 244.3->149.9) while FarFieldSampledLevel/count
        // stayed flat.
        const auto sampleIntensity = debugCaptureResource_->ReadFarFieldSampleIntensityStats(vulkanDevice->device);
        std::cout << "[FarFieldSampleIntensity] min=" << sampleIntensity.min
                   << " max=" << sampleIntensity.max
                   << " mean=" << sampleIntensity.mean
                   << " n=" << sampledLevel.count << std::endl;

        // Batch-35: [PolicyEntryDispatch] -- proves the entry-point dispatch
        // inversion happened. mip = rays that resolved via the mip ladder
        // directly at instance entry (no march); march = rays that fell
        // through to the exact per-cell march (genuine detail at entry, OR
        // an admitted-but-empty entry cell -- these two populations are
        // conflated in `march` by construction). All-zero on a flag-off/
        // no-VIXEN_MIP_POLICY boot -- the shader never calls
        // recordPolicyEntryDispatch there.
        // Batch-39: emptyEntry splits out the admitted-but-empty-entry-cell
        // subset of `march` (entryPolicyAdmits==true but entryLocalBrickIdx
        // was 0xFFFFFFFF) so it's distinguishable from genuine detail-regime
        // rays. Additive -- mip/march themselves are unchanged.
        const auto entryDispatch = debugCaptureResource_->ReadPolicyEntryDispatchStats(vulkanDevice->device);
        std::cout << "[PolicyEntryDispatch] mip=" << entryDispatch.mip
                   << " march=" << entryDispatch.march
                   << " emptyEntry=" << entryDispatch.emptyEntry << std::endl;

        // Regime-3 (cosmic accumulation) first slice, deep-field-mip-policy design doc:
        // entry = rays that took the accumulation walk (VIXEN_REGIME3 && footprint >=
        // K*cell at entry dispatch); earlyOut = subset that hit the T~eps early-out.
        // All-zero on a flag-off/no-VIXEN_REGIME3 boot -- the shader never calls
        // recordRegime3Entry/recordRegime3EarlyOut there.
        const auto regime3 = debugCaptureResource_->ReadRegime3Stats(vulkanDevice->device);
        std::cout << "[Regime3] entry=" << regime3.entry
                   << " earlyOut=" << regime3.earlyOut << std::endl;

        // Compositing-slice part 1 (walkCov source audit): min/max of walkCov
        // (readMipSample(SEM_SDF).y) and the level it was sampled at, from the
        // regime-3 walk's sample call site. All-zero/seeded-min on a flag-off/
        // no-VIXEN_REGIME3 boot -- the shader never calls recordWalkCov there.
        const auto walkCov = debugCaptureResource_->ReadWalkCovStats(vulkanDevice->device);
        std::cout << "[WalkCov] covMin=" << walkCov.covMin
                   << " covMax=" << walkCov.covMax
                   << " levelMin=" << walkCov.levelMin
                   << " levelMax=" << walkCov.levelMax << std::endl;

        // Round-7 blocker-1 probe: mip-resolve success/fail -- discriminates
        // hypothesis (a) "the mip resolve fails" from (b)/(c) "resolves fine
        // but is lost/ignored downstream".
        const auto mipStats = debugCaptureResource_->ReadFarFieldMipStats(vulkanDevice->device);
        std::cout << "[FarFieldMipSuccess] n=" << mipStats.success
                   << " (" << perFrame(mipStats.success) << "/frame over " << exitAfterFrames << " frames)" << std::endl;
        std::cout << "[FarFieldMipFail] n=" << mipStats.fail
                   << " (" << perFrame(mipStats.fail) << "/frame over " << exitAfterFrames << " frames)" << std::endl;

        // Round 13 probe: splits farFieldMipFail into "descendToNodeOrdinal
        // never reached the brick level" vs "reached it but shadeFromMipSample
        // found no coverage". descentFail==mipFail => root cause is IN the
        // descent (missing child / farBit / wrong cell-octant-depth inputs),
        // not in mip sampling.
        const uint32_t descentFail = debugCaptureResource_->ReadFarFieldDescentFail(vulkanDevice->device);
        std::cout << "[FarFieldDescentFail] n=" << descentFail
                   << " (" << perFrame(descentFail) << "/frame over " << exitAfterFrames << " frames)" << std::endl;

        // Round 13 probe #2: min/max hop-depth (depth - level) at the point of
        // descent failure -- localizes whether failures cluster near the root
        // (small hops) or near the brick (hops close to farDepth).
        const auto descentFailLevels = debugCaptureResource_->ReadFarFieldDescentFailLevelRange(vulkanDevice->device);
        std::cout << "[FarFieldDescentFailLevel] min=" << descentFailLevels.first
                   << " max=" << descentFailLevels.second << std::endl;
        // Standing rule (batch-13 postmortem): every min-tracker is seeded
        // 0xFFFFFFFF at Create(); a real min of 0 is only credible when max is
        // ALSO near 0 (root-adjacent failures cluster tight). min==0 with a
        // much larger max is the unseeded-atomicMin signature -- flag it loud
        // instead of silently trusting a fabricated reading.
        if (descentFailLevels.first == 0u && descentFailLevels.second > 2u) {
            std::cout << "[FarFieldDescentFailLevel] SEEDED-SANITY WARNING: "
                          "min=0 but max=" << descentFailLevels.second
                       << " -- looks like an unseeded atomicMin, not a real "
                          "root-adjacent failure. Verify Create() seeds this field."
                       << std::endl;
        }

        const uint32_t rejectedByBounds = debugCaptureResource_->ReadFarFieldRejectedByBounds(vulkanDevice->device);
        std::cout << "[FarFieldRejectedByBounds] n=" << rejectedByBounds
                   << " (" << perFrame(rejectedByBounds) << "/frame over " << exitAfterFrames << " frames)" << std::endl;

        const uint32_t farFieldWon = debugCaptureResource_->ReadFarFieldWon(vulkanDevice->device);
        std::cout << "[FarFieldWon] n=" << farFieldWon
                   << " (" << perFrame(farFieldWon) << "/frame over " << exitAfterFrames << " frames)" << std::endl;

        // Round 9: per-pixel TERMINAL far-field count (does the pixel's FINAL
        // rendered HitRecord carry HITRECORD_FLAG_FAR_FIELD, not just "won a
        // per-instance-loop compare" like farFieldWon above).
        const uint32_t farFieldTerminal = debugCaptureResource_->ReadFarFieldTerminal(vulkanDevice->device);
        std::cout << "[FarFieldTerminal] n=" << farFieldTerminal
                   << " (" << perFrame(farFieldTerminal) << "/frame over " << exitAfterFrames << " frames)" << std::endl;

        // Batch 10: splits farFieldMipSuccess into "a real SEM_COLOR mip
        // sample was resolved" vs "fell through to the flat grey vec3(0.5)
        // placeholder" (MipFallback.glsl's shadeFromMipSample colorSample.y
        // branch). colorFallback==farFieldCount => bake-side gap (no SEM_COLOR
        // mip coverage at all); colorResolved==farFieldCount => resolve is fine,
        // sizes the contrast gap instead.
        const auto colorStats = debugCaptureResource_->ReadFarFieldColorStats(vulkanDevice->device);
        std::cout << "[FarFieldColorResolved] n=" << colorStats.resolved
                   << " (" << perFrame(colorStats.resolved) << "/frame over " << exitAfterFrames << " frames)" << std::endl;
        std::cout << "[FarFieldColorFallback] n=" << colorStats.fallback
                   << " (" << perFrame(colorStats.fallback) << "/frame over " << exitAfterFrames << " frames)" << std::endl;

        // Round 11: dispatch attribution. TraceWorld is the ONLY far-field-
        // carrying call path on either backend (its AnyHit twins, used by
        // TraceWorldShadow/ShadowRayTrace.comp/HitAccumCellShade.comp, contain
        // no far-field logic). TraceWorld's only two callers: index 0 =
        // primary march (BodyInstanceRayMarch.comp), index 1 = "not-primary"
        // (ProbeGather.comp / TraceWorld's non-primary-march caller -- round
        // 12 instrument fix (b): renamed from "probe" to the honest label;
        // SceneBindings compiles into 16 translation units and TraceWorld has
        // three callers total, so "probe" overclaimed what index 1 means).
        const auto tagStats = debugCaptureResource_->ReadFarFieldByTagStats(vulkanDevice->device);
        std::cout << "[FarFieldCandidatesByTag] primary=" << tagStats.candidates[0]
                   << " (" << perFrame(tagStats.candidates[0]) << "/frame) not-primary=" << tagStats.candidates[1]
                   << " (" << perFrame(tagStats.candidates[1]) << "/frame) over " << exitAfterFrames << " frames" << std::endl;
        std::cout << "[FarFieldCountByTag] primary=" << tagStats.count[0]
                   << " (" << perFrame(tagStats.count[0]) << "/frame) not-primary=" << tagStats.count[1]
                   << " (" << perFrame(tagStats.count[1]) << "/frame) over " << exitAfterFrames << " frames" << std::endl;
        std::cout << "[FarFieldColorResolvedByTag] primary=" << tagStats.colorResolved[0]
                   << " (" << perFrame(tagStats.colorResolved[0]) << "/frame) not-primary=" << tagStats.colorResolved[1]
                   << " (" << perFrame(tagStats.colorResolved[1]) << "/frame) over " << exitAfterFrames << " frames" << std::endl;
        std::cout << "[FarFieldColorFallbackByTag] primary=" << tagStats.colorFallback[0]
                   << " (" << perFrame(tagStats.colorFallback[0]) << "/frame) not-primary=" << tagStats.colorFallback[1]
                   << " (" << perFrame(tagStats.colorFallback[1]) << "/frame) over " << exitAfterFrames << " frames" << std::endl;

        // Round 12 instrument fix (b): the 32-slot pixel decode ring is
        // REMOVED. It decodes the TAIL of atomicAdd dispatch order (last-32
        // writes before wrap), not a spatial sample of the far-field
        // population -- batch 11 confirmed the "16x3 patch" it showed was a
        // ring artifact, not real geometry. ByTag counters above remain the
        // honest source of attribution; a spatial read now comes from
        // localized_diff.py's --region mode against an actual HUD capture,
        // not this ring.

    }

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

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::VoxelGridNodeType);
