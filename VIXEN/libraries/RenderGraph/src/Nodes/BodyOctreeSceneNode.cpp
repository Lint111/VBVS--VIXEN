#include "Nodes/BodyOctreeSceneNode.h"
#include "Core/NodeRegistration.h"  // M3: VIXEN_REGISTER_NODE self-registration
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "VulkanDevice.h"
#include "Memory/BatchedUploader.h"  // Inc1 M2: ResourceManagement::InvalidUploadHandle
#include "MipBake.h"  // Lazy-Procedural-Delta-Baseline Inc0 M1: ConcatenateSdfWithMips
#include "ResidencyDefault.h"  // Lazy-Procedural-Delta-Baseline Inc0 M2: DeriveResidencyDefault

#include <algorithm>
#include <cctype>    // std::isspace for whitespace-safe boolean env flags
#include <cstdio>    // std::snprintf (BrickDataHash log line)
#include <cstdlib>   // std::getenv
#include <cstring>
#include <iostream>  // FIX 5: std::cout -- BrickDataHash bypasses the disabled per-node logger
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>  // W-RTQUERY Slice A: glm::translate/glm::scale for TLAS instance transforms

// W-RTQUERY Slice A: per-brick-AABB TLAS build (EnsureRtQueryTlasBuilt/DestroyRtQueryTlas below).

// Local helper — mirrors InstanceBufferNode.cpp's FindSuitableMemoryType. Defined
// here (static, TU-local) to avoid inline-COMDAT selection clashes across TUs.
static uint32_t FindSuitableMemoryType(
    const VkPhysicalDeviceMemoryProperties& memProps,
    uint32_t typeFilter,
    VkMemoryPropertyFlags required,
    const char* context)
{
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    throw std::runtime_error(
        std::string("[BodyOctreeSceneNode] No suitable memory type found for ") + context);
}

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// Ring size = frames-in-flight (the value CURRENT_FRAME_INDEX cycles through).
const uint32_t BodyOctreeSceneNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

namespace {

bool envFlagEnabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return false;
    for (; *value != '\0'; ++value) {
        if (!std::isspace(static_cast<unsigned char>(*value))) return true;
    }
    return false;
}

// Map a body kind index [0,kKindCount) to a material id in the default palette
// (BuildDefaultMaterialPalette in ShellOctreeGpu.h fills 1=red, 2=green, 3=white...).
inline uint32_t MaterialIdForKind(uint32_t kind) {
    return kind + 1u;  // 1, 2, 3
}

// Create one host-visible/host-coherent buffer and upload `bytes` into it.
// Mirrors InstanceBufferNode::CreateBuffer. `usage` is the buffer usage flags.
void CreateHostBuffer(VulkanDevice* device,
                      VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      const void* data,
                      VkBuffer& outBuffer,
                      VkDeviceMemory& outMemory,
                      const char* context)
{
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) {
        throw std::runtime_error(std::string("[BodyOctreeSceneNode] vkCreateBuffer failed for ") + context);
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vkDevice, outBuffer, &req);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = FindSuitableMemoryType(
        memProps, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        context);

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        throw std::runtime_error(std::string("[BodyOctreeSceneNode] vkAllocateMemory failed for ") + context);
    }

    if (vkBindBufferMemory(vkDevice, outBuffer, outMemory, 0) != VK_SUCCESS) {
        vkFreeMemory(vkDevice, outMemory, nullptr);
        vkDestroyBuffer(vkDevice, outBuffer, nullptr);
        outMemory = VK_NULL_HANDLE;
        outBuffer = VK_NULL_HANDLE;
        throw std::runtime_error(std::string("[BodyOctreeSceneNode] vkBindBufferMemory failed for ") + context);
    }

    if (data != nullptr && size > 0) {
        void* mapped = nullptr;
        if (vkMapMemory(vkDevice, outMemory, 0, size, 0, &mapped) != VK_SUCCESS) {
            throw std::runtime_error(std::string("[BodyOctreeSceneNode] vkMapMemory failed for ") + context);
        }
        std::memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(vkDevice, outMemory);
    }
}

// W-RTQUERY Slice A: buffer + VK_KHR_buffer_device_address memory flag, for AS
// build inputs (AABB/instance geometry, scratch, AS storage). CreateHostBuffer above is
// host-visible-only and never requests VK_MEMORY_ALLOCATE_FLAGS_INFO's DEVICE_ADDRESS_BIT
// -- AS build inputs need vkGetBufferDeviceAddressKHR to resolve, which requires the
// allocation itself to have been made with that flag (VUID-vkGetBufferDeviceAddress-buffer-02601).
// Mirrors AccelerationStructureCacher::BuildBLAS/BuildTLAS's own alloc shape (AllocateBufferTracked
// there; hand-rolled here — see EnsureRtQueryTlasBuilt's header comment for why this node
// doesn't route through that cacher).
void CreateDeviceAddressBuffer(VulkanDevice* device,
                                VkDeviceSize size,
                                VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags memProps,
                                VkBuffer& outBuffer,
                                VkDeviceMemory& outMemory,
                                const char* context)
{
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) {
        throw std::runtime_error(std::string("[BodyOctreeSceneNode] vkCreateBuffer failed for ") + context);
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vkDevice, outBuffer, &req);

    VkPhysicalDeviceMemoryProperties devMemProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &devMemProps);

    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext           = &flagsInfo;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = FindSuitableMemoryType(devMemProps, req.memoryTypeBits, memProps, context);

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        throw std::runtime_error(std::string("[BodyOctreeSceneNode] vkAllocateMemory failed for ") + context);
    }

    if (vkBindBufferMemory(vkDevice, outBuffer, outMemory, 0) != VK_SUCCESS) {
        vkFreeMemory(vkDevice, outMemory, nullptr);
        vkDestroyBuffer(vkDevice, outBuffer, nullptr);
        outMemory = VK_NULL_HANDLE;
        outBuffer = VK_NULL_HANDLE;
        throw std::runtime_error(std::string("[BodyOctreeSceneNode] vkBindBufferMemory failed for ") + context);
    }
}

VkDeviceAddress GetBufferDeviceAddress(VulkanDevice* device, VkBuffer buffer) {
    VkBufferDeviceAddressInfo info{};
    info.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return vkGetBufferDeviceAddress(device->device, &info);
}

// W-BRICKMAP Slice 2 boot-data-variation theory test: stable 64-bit hash over raw bytes,
// so per-boot octree/SDF-build data can be compared boot-to-boot in the log. FNV-1a — no
// crypto need, just a cheap stable fingerprint for a one-shot boot-time log line.
uint64_t Fnv1a64(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 0xcbf29ce484222325ull;  // FNV offset basis
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ull;  // FNV prime
    }
    return hash;
}

template <typename T>
uint64_t Fnv1a64(const std::vector<T>& v) {
    return Fnv1a64(v.data(), v.size() * sizeof(T));
}

}  // namespace

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> BodyOctreeSceneNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::unique_ptr<NodeInstance>(
        new BodyOctreeSceneNode(instanceName, const_cast<BodyOctreeSceneNodeType*>(this)));
}

// ============================================================================
// BODY OCTREE SCENE NODE
// ============================================================================

BodyOctreeSceneNode::BodyOctreeSceneNode(const std::string& instanceName, NodeType* nodeType)
    : TypedNode<BodyOctreeSceneNodeConfig>(instanceName, nodeType)
{
    wholesaleAdmissionEnabled_ = envFlagEnabled("VIXEN_WHOLESALE_ADMISSION");
    if (envFlagEnabled("VIXEN_WHOLESALE_ADMISSION_SURFACE")) {
        residencyRequested_ = true;
    }
    NODE_LOG_INFO("[BodyOctreeSceneNode] constructor");
}

void BodyOctreeSceneNode::SetInstances(std::vector<Vixen::SVO::BodyInstanceGpu> instances) {
    // Stash the new list. ExecuteImpl uploads it each frame into the current ring slot.
    // Do NOT call MarkNeedsRecompile for a steady same-size update — the per-tick recompile
    // cascade was the race root cause. Growth is the ONE case that NEEDS it: the ring is sized
    // by CompileImpl's EnsureRingAllocated, so a list that no longer fits the current ring must
    // force a recompile or the per-frame upload silently truncates (see ExecuteImpl's clamp).
    instances_     = std::move(instances);
    instanceCount_ = static_cast<int32_t>(instances_.size());
    const VkDeviceSize neededBytes = static_cast<VkDeviceSize>(
        instances_.size() * sizeof(Vixen::SVO::BodyInstanceGpu));
    if (neededBytes > instanceRingCapacity_) {
        NODE_LOG_INFO("[BodyOctreeSceneNode] SetInstances: needed " + std::to_string(neededBytes) +
                      "B exceeds ring capacity " + std::to_string(instanceRingCapacity_) +
                      "B — requesting recompile to grow the ring");
        MarkNeedsRecompile();
    }
    NODE_LOG_INFO("[BodyOctreeSceneNode] SetInstances: " +
                  std::to_string(instanceCount_) + " instances staged for next Execute");
}

void BodyOctreeSceneNode::SortInstancesFrontToBack(const glm::vec3& cameraPos) {
    // In-place; does NOT mark instanceCount_ dirty (count is unchanged by a reorder).
    // ExecuteImpl uploads whatever order instances_ is currently in — same seam as
    // SetInstances, just without replacing the list.
    //
    // Baked-Perf M5 Task 5.3: sort by each instance's TRUE occupied-region center
    // (traceBoundsMin/Max, Task 5.1), not worldPos (the body's full-cube min-CORNER,
    // despite BodyInstanceGpu's own field comment calling it "body centre" — see
    // InstanceSort.h's updated doc comment for the full derivation). This node has
    // concatenated_.configs available here (populated by EnsureOctreesBuilt/
    // Rematerialize before any SetInstances/Sort call), so it can compute the real
    // per-instance center instead of falling back to the plain worldPos-only overload.
    // Procedural-provider instances (no octreeIndex-indexed config; analytic SDF, not
    // ESVO) have no traceBounds to read — worldPos IS their true center for those
    // (bodyWorldPos/kWorldGridSize's own convention only applies to Stored/ESVO
    // instances), so the accessor below falls back to worldPos unchanged whenever
    // octreeIndex doesn't resolve to a valid config, keeping this a strict refinement.
    constexpr float kWorldGridSize = 10.0f;  // ShellOctreeGpu.h's fixed octree-local->world span
    const std::vector<Vixen::SVO::OctreeConfig>& configs = concatenated_.configs;
    Vixen::SVO::SortInstancesFrontToBack(instances_, cameraPos,
        [&configs](const Vixen::SVO::BodyInstanceGpu& inst) {
            const glm::vec3 worldPos(inst.worldPos[0], inst.worldPos[1], inst.worldPos[2]);
            if (inst.providerKind != 0u /* PROVIDER_STORED */ ||
                inst.octreeIndex >= configs.size()) {
                return worldPos;
            }
            return Vixen::SVO::traceBoundsWorldCenterOf(
                configs[inst.octreeIndex], worldPos, inst.renderScale, kWorldGridSize);
        });
}

void BodyOctreeSceneNode::SetBakeRecipe(std::vector<Vixen::SVO::Recipe::SdfInstruction> prog) {
    bakeRecipe_  = std::move(prog);
    recipeDirty_ = true;   // P2.3: if already compiled, ExecuteImpl re-materializes on the next frame;
                           //       if pre-Compile, CompileImpl bakes fresh and clears this.
    // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4: a genuinely new pool is being staged —
    // any previous explicit residency grant applied to the OLD pool, not this one. Clearing
    // the latch here (not inside Rematerialize) means DeriveResidencyDefaultIfUnset's
    // "only on the node's first-ever Compile" guard still governs whether re-derivation
    // actually happens; this just makes a pre-first-Compile SetBakeRecipe call behave like
    // a fresh boot (see the latch's own doc comment on the header for the full hazard).
    residencyExplicitlyRequested_ = false;
    NODE_LOG_INFO("[BodyOctreeSceneNode] SetBakeRecipe: " +
                  std::to_string(bakeRecipe_.size()) + " instructions — octree 0 will use recipe bake");
}

void BodyOctreeSceneNode::SetRecipePool(Vixen::SVO::ConcatenatedOctrees pool) {
    providedPool_ = std::move(pool);
    poolProvided_ = true;
    octreesBuilt_ = false;   // force EnsureOctreesBuilt to pick up the new pool
    recipeDirty_  = true;    // post-Compile: triggers Rematerialize on next Execute
    // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4: see SetBakeRecipe's comment above —
    // same latch-clear reasoning, mirrored for the pool-provided path.
    residencyExplicitlyRequested_ = false;
    NODE_LOG_INFO("[BodyOctreeSceneNode] SetRecipePool: " +
                  std::to_string(providedPool_.count) + " octrees staged");
}

bool BodyOctreeSceneNode::EditSourceBrickSdf(uint32_t brickId,
                                             const std::vector<float>& sdf512) {
    if (!Vixen::SVO::ApplyBrickSdfEdit(concatenated_, /*octreeIdx=*/0u, brickId,
                                       sdf512.data(), sdf512.size())) {
        return false;
    }
    dirtyBricks_.push_back(brickId);
    NODE_LOG_DEBUG("[BodyOctreeSceneNode] EditSourceBrickSdf: brick " +
                   std::to_string(brickId) + " edited + marked dirty (" +
                   std::to_string(dirtyBricks_.size()) + " pending)");
    return true;
}

void BodyOctreeSceneNode::SetOccupancyGrid(std::vector<float> concatenatedGrid) {
    // Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13: stash the blob; CreateOctreeBuffers
    // uploads it on the next (re)Compile. Does NOT set recipeDirty_ itself — the shader
    // splice that produces this blob already forces a shader recompile through the normal
    // registration-changed path (BuildRenderGraph.cpp's RegisterShaderBuilder re-runs), and
    // a bare buffer swap with no shader change would leave the OLD spliced getRecipeOccupancyGrid
    // switch's baked gridOffset literals pointing at the wrong blob layout — this setter is
    // only ever called from that same shader-build callback, never independently.
    occupancyGrid_ = std::move(concatenatedGrid);
    NODE_LOG_INFO("[BodyOctreeSceneNode] SetOccupancyGrid: " +
                  std::to_string(occupancyGrid_.size()) + " floats staged");
}

void BodyOctreeSceneNode::RequestBrickResidency(bool resident) {
    if (wholesaleAdmissionEnabled_ && envFlagEnabled("VIXEN_WHOLESALE_ADMISSION_SURFACE")) {
        resident = true;
    }
    // Stash only — mirrors SetBakeRecipe/SetRecipePool's dirty-flag pattern. ExecuteImpl
    // performs the actual BatchedUploader call next frame; never upload synchronously here.
    residencyRequested_  = resident;
    brickResidencyDirty_ = (resident != brickPoolUploaded_);
    // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4: an explicit call always wins over the
    // capability-derived default — latch it so DeriveResidencyDefaultIfUnset (first Compile
    // only) and any later Rematerialize both leave this value alone.
    residencyExplicitlyRequested_ = true;
    NODE_LOG_INFO(std::string("[BodyOctreeSceneNode] RequestBrickResidency: ") +
                  (resident ? "true" : "false") + " (dirty=" +
                  (brickResidencyDirty_ ? "true" : "false") + ")");
}

void BodyOctreeSceneNode::DeriveResidencyDefaultIfUnset() {
    // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4: capability-derived residency default.
    // Called exactly once — from CompileImpl, gated on "first Compile ever" (nodesBuffer_
    // still null) — never from Rematerialize, so a live grant survives an editor-toggle
    // rebuild with the camera unmoved (see the header's residencyExplicitlyRequested_ doc
    // comment for the full hazard this avoids).
    if (residencyExplicitlyRequested_) {
        NODE_LOG_INFO("[BodyOctreeSceneNode] DeriveResidencyDefault: skipped (explicit request already set)");
        return;
    }

    // Scope note (M2): this derivation gates only the binary concatenated_.bricks blob's
    // boot-time population (CreateOctreeBuffers/UploadBrickPool). The channelPool, nodes,
    // mips, lookup tables, and both shell-cache slots still upload whole at Compile — their
    // laziness is a future increment's paged pool, not this milestone.
    //
    // Pure logic lives in ResidencyDefault.h (mirrors ResidencyTrigger.h's own
    // dependency-free-function pattern) so it is unit-testable directly against a
    // ConcatenatedOctrees with no device/node involved.
    uint32_t mipCapableCount = 0;
    for (uint32_t i = 0; i < concatenated_.count; ++i) {
        if (Vixen::SVO::IsOctreeMipCapable(concatenated_, i)) {
            ++mipCapableCount;
        }
    }
    residencyRequested_ = Vixen::SVO::DeriveResidencyDefault(concatenated_);
    NODE_LOG_INFO("[BodyOctreeSceneNode] DeriveResidencyDefault: " +
                  std::to_string(mipCapableCount) + "/" + std::to_string(concatenated_.count) +
                  " octrees mip-capable -> residencyRequested_=" +
                  (residencyRequested_ ? "true (eager)" : "false (lazy)"));
}

void BodyOctreeSceneNode::SetupImpl(TypedSetupContext& /*ctx*/) {
    // Graph-scope initialization only (no input access).
    NODE_LOG_DEBUG("[BodyOctreeSceneNode] Setup (graph-scope initialization)");
}

void BodyOctreeSceneNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[BodyOctreeSceneNode] Compile START");

    VulkanDevice* devicePtr = ctx.In(BodyOctreeSceneNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[BodyOctreeSceneNode] VULKAN_DEVICE_IN is null");
    }
    SetDevice(devicePtr);

    // COMMAND_POOL is a required input (mirrors VoxelGridNode); validate it is present
    // even though host-coherent upload needs no transfer command buffer here.
    VkCommandPool commandPool = ctx.In(BodyOctreeSceneNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[BodyOctreeSceneNode] COMMAND_POOL is null");
    }

    // 1+2) Build the per-kind shell octrees + concatenate (once; cached).
    EnsureOctreesBuilt();

    // 3a) Octree GPU buffers — persistent across recompile (create only once).
    if (nodesBuffer_ == VK_NULL_HANDLE) {
        // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4: derive the capability-based
        // residency default exactly once — the node's first-ever Compile, before
        // CreateOctreeBuffers reads residencyRequested_ to decide whether to populate
        // bricksBuffer_ at creation. Deliberately NOT called from Rematerialize (a later
        // pool swap re-enters CompileImpl with nodesBuffer_ already valid, so this branch
        // is skipped) — see DeriveResidencyDefaultIfUnset's own doc comment.
        DeriveResidencyDefaultIfUnset();
        CreateOctreeBuffers(devicePtr);
    } else {
        NODE_LOG_INFO("[BodyOctreeSceneNode] Reusing persistent octree buffers across recompile");
    }

    // 3b) Instance SSBO ring — allocate once, persistent across recompile.
    // A placeholder capacity of at least 1 element ensures the ring is valid before
    // the first SetInstances call. EnsureRingAllocated grows it (behind vkDeviceWaitIdle)
    // if SetInstances is called with a larger list before the first Compile.
    {
        // Capacity: max of current instance list and a safe minimum (1 record).
        std::vector<Vixen::SVO::BodyInstanceGpu> placeholderList;
        if (instances_.empty()) {
            placeholderList.push_back(Vixen::SVO::BodyInstanceGpu{});
        }
        const std::vector<Vixen::SVO::BodyInstanceGpu>& toMeasure =
            instances_.empty() ? placeholderList : instances_;
        const std::vector<uint8_t> packed = Vixen::SVO::PackInstances(toMeasure);
        const VkDeviceSize needed = std::max<VkDeviceSize>(packed.size(), 1);
        EnsureRingAllocated(devicePtr, needed);
    }

    // 4) Publish outputs. INSTANCE_BUFFER emits ring slot 0 as a compile-time placeholder;
    //    ExecuteImpl overwrites it each frame with the current ring slot.
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER,         nodesBuffer_);
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_BRICKS_BUFFER,        bricksBuffer_);
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_MATERIALS_BUFFER,     materialsBuffer_);
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,        configBuffer_);
    ctx.Out(BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,             perFrame_.GetUniformBuffer(0));
    ctx.Out(BodyOctreeSceneNodeConfig::INSTANCE_COUNT,              instanceCount_);
    // Inc2 M3: SDF + lookup buffers (bindings 11/12). Always emitted — placeholder
    // for binary/Procedural, real data for Stored-SDF bodies.
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_SDF_BUFFER,           sdfBuffer_);
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_BRICKLOOKUP_BUFFER,   brickLookupBuffer_);
    // Sparse-Mip ESVO LOD Inc1 M3: mip pool buffer (binding 13). Always emitted —
    // placeholder for a tree that was never mip-baked, real data otherwise.
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_MIPPOOL_BUFFER,       mipPoolBuffer_);
    // Tiered-ESVO Inc2 M3: tier-ref table buffer (binding 15). Always emitted —
    // placeholder for a scene with no tier-crossing leaves, real data otherwise.
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_TIERREFTABLE_BUFFER,  tierRefTableBuffer_);
    // Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13: occupancy grid buffer (binding 16).
    // Always emitted — placeholder for a scene with no derivable procedural recipe grids.
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_OCCUPANCYGRID_BUFFER, occupancyGridBuffer_);
    // Surface-Shell ESVO cache — publish slot 0 as the compile-time placeholder;
    // ExecuteImpl re-emits slot [frame&1] each frame (the last committed cache).
    ctx.Out(BodyOctreeSceneNodeConfig::SHELL_DATA_BUFFER,           shellDataBuffer_[0]);
    ctx.Out(BodyOctreeSceneNodeConfig::SHELL_LOOKUP_BUFFER,         shellLookupBuffer_[0]);
    // W-RTQUERY Slice A: compile-time placeholder — VK_NULL_HANDLE until
    // EnsureRtQueryTlasBuilt runs (ExecuteImpl, once instances_ is known); re-emitted there.
    ctx.Out(BodyOctreeSceneNodeConfig::RTQUERY_TLAS,                rtQueryTlas_);
    // Raster-proxy B2 binder: allocation capacity is grow-only, so publish the
    // exact live count from the same shell read slot as the buffer handle.
    ctx.Out(BodyOctreeSceneNodeConfig::PROXY_AABB_BUFFER,            proxyAabbBuffer_[0]);
    ctx.Out(BodyOctreeSceneNodeConfig::PROXY_AABB_COUNT,             proxyAabbCount_[0]);

    NODE_LOG_INFO("[BodyOctreeSceneNode] Outputs published (octrees=" +
                  std::to_string(concatenated_.count) + ", instances=" +
                  std::to_string(instanceCount_) + ", ringSlots=" +
                  std::to_string(kRingSize) + ")");

    // A fresh compile already baked the current recipe — no pending re-materialize.
    recipeDirty_ = false;
}

void BodyOctreeSceneNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // P2.3: a runtime recipe edit (SetBakeRecipe after Compile) re-bakes + re-uploads here,
    // at the fence-waited safe point — never on the recompile cascade.
    bool octreeRepublished = false;
    if (recipeDirty_) {
        Rematerialize();
        recipeDirty_      = false;
        octreeRepublished = true;
    }

    // Inc1 M2: a residency request toggled since the last Execute is serviced here — the
    // same fence-waited safe point recipeDirty_ uses, never synchronously in the setter.
    const auto classifiedRegime = residencyRequested_
        ? Vixen::SVO::CellFootprintRegime::Surface
        : Vixen::SVO::CellFootprintRegime::MipHit;
    const bool wholesaleTransition = wholesaleAdmissionEnabled_ &&
        Vixen::SVO::AdvanceWholesaleAvailability(
            wholesaleAvailability_, classifiedRegime,
            static_cast<uint32_t>(Vixen::SVO::WholesalePayload::ChannelPool) |
            static_cast<uint32_t>(Vixen::SVO::WholesalePayload::BrickLookup));
    if (wholesaleTransition && wholesaleAvailability_.committedRegime ==
        Vixen::SVO::CellFootprintRegime::Surface) {
        brickResidencyDirty_ = true;
        if (wholesaleAvailability_.reusablePopulatedBytes != 0u) {
            PublishWholesaleReuse();
            brickResidencyDirty_ = false;
        }
    }
    if (wholesaleTransition) {
        NODE_LOG_INFO("[WholesaleAvailability] transition generation=" +
                      std::to_string(wholesaleAvailability_.generation) +
                      " desired=" + std::to_string(static_cast<uint32_t>(wholesaleAvailability_.desiredRegime)) +
                      " pendingMask=" + std::to_string(wholesaleAvailability_.pendingMask) +
                      " readyMask=" + std::to_string(wholesaleAvailability_.readyMask));
    }
    if (brickResidencyDirty_) {
        UploadBrickPool();
        brickResidencyDirty_ = false;
    }

    // Inc1 M4c: poll (non-blocking) for in-flight brick/config uploads queued by
    // UploadBrickPool above, on THIS or an earlier frame — a multi-frame latency between
    // "residency requested" and "brickResident actually visible on GPU" is expected and
    // correct (that's the whole point of not blocking); every frame checks, no frame waits.
    PollBrickUploadCompletion();

    // Per-frame ring index from FrameSyncNode (clamp via modulo for safety).
    const uint32_t rawFrame   = ctx.In(BodyOctreeSceneNodeConfig::CURRENT_FRAME_INDEX);
    const uint32_t frameIndex = rawFrame % kRingSize;

    // --- Surface-Shell ESVO cache double-buffer (§B/§C) ---
    // Render binds the CURRENT read slot [rawFrame&1] (last committed). A pending
    // CPU dirty list (a value edit that did NOT force a full Rematerialize) is
    // applied to the WRITE slot [(rawFrame+1)&1] here — a partial re-derive of only
    // the dirty bricks, then re-uploaded to that slot's GPU buffer. Because the two
    // GPU slots are DISTINCT VkBuffer objects, a render reading slot N and this
    // revalidate writing slot N+1 touch disjoint Resource*; the FrameSyncScheduler
    // (pointer-identity hazard keying) inserts no barrier between them.
    VulkanDevice* device = GetDevice();
    const uint32_t readSlot  = rawFrame & 1u;
    const uint32_t writeSlot = (rawFrame + 1u) & 1u;
    if (device && !octreeRepublished && !dirtyBricks_.empty() &&
        !shellCache_[writeSlot].perOctree.empty() && concatenated_.count > 0u) {
        // Value-edit revalidate for octree 0 (the primary Stored-SDF body): rewrite
        // only the dirty bricks' data in the WRITE slot's compact octree-0 region,
        // then re-upload that slot's GPU buffer. Distinct GPU slot => no barrier vs
        // the render reading the READ slot.
        Vixen::SVO::ShellPool& ws = shellCache_[writeSlot];
        Vixen::SVO::ShellDeriveResult& r0 = ws.perOctree[0];
        // The compact channelPool holds octree 0's shell bricks first (poolBrickBase
        // 0), so RevalidateShellBricks can rewrite them in-place in the compact pool.
        const uint32_t rewritten = Vixen::SVO::RevalidateShellBricks(
            concatenated_, /*octreeIdx=*/0u, r0, dirtyBricks_, ws.compact.channelPool);
        UploadShellSlot(device, writeSlot);
        ++shellRevalidateCount_;
        NODE_LOG_INFO("[BodyOctreeSceneNode] Shell revalidate: " +
                      std::to_string(rewritten) + " shell slots updated from " +
                      std::to_string(dirtyBricks_.size()) + " dirty bricks (write slot " +
                      std::to_string(writeSlot) + ")");
        dirtyBricks_.clear();
    }
    // Re-emit the CURRENT read slot so the render descriptor binds the committed cache.
    if (shellDataBuffer_[readSlot] != VK_NULL_HANDLE) {
        ctx.Out(BodyOctreeSceneNodeConfig::SHELL_DATA_BUFFER,   shellDataBuffer_[readSlot]);
        ctx.Out(BodyOctreeSceneNodeConfig::SHELL_LOOKUP_BUFFER, shellLookupBuffer_[readSlot]);
        ctx.Out(BodyOctreeSceneNodeConfig::PROXY_AABB_BUFFER,   proxyAabbBuffer_[readSlot]);
        ctx.Out(BodyOctreeSceneNodeConfig::PROXY_AABB_COUNT,    proxyAabbCount_[readSlot]);
    }

    // Build the packed byte representation of the current instance list.
    // If empty, produce a valid 1-element placeholder so the SSBO is always non-null.
    std::vector<Vixen::SVO::BodyInstanceGpu> toPack;
    if (instances_.empty()) {
        toPack.push_back(Vixen::SVO::BodyInstanceGpu{});  // zeroed placeholder
    } else {
        toPack = instances_;
    }
    const std::vector<uint8_t> packed = Vixen::SVO::PackInstances(toPack);

    // Upload into THIS frame's ring buffer (host-coherent: no flush needed).
    // The frame fence for frameIndex was waited before Execute fired, so this
    // slot is guaranteed not in flight.
    void* mapped = perFrame_.GetUniformBufferMapped(frameIndex);
    if (mapped && !packed.empty()) {
        const size_t copyBytes = std::min(static_cast<size_t>(instanceRingCapacity_),
                                          packed.size());
        std::memcpy(mapped, packed.data(), copyBytes);
    }

    // Emit THIS frame's buffer so the descriptor binds the just-written data.
    ctx.Out(BodyOctreeSceneNodeConfig::INSTANCE_BUFFER, perFrame_.GetUniformBuffer(frameIndex));

    // Honest count clamp: packed.size() can exceed instanceRingCapacity_ between a SetInstances
    // growth and the recompile that actually grows the ring (SetInstances now requests that
    // recompile, but it hasn't necessarily run by this Execute). The upload above already clamps
    // copyBytes to the ring's real capacity — clamp the emitted count the same way so the shader
    // never reads instance records past what was actually written (no robustBufferAccess on this
    // SSBO; an unclamped count was a latent OOB read).
    const int32_t ringCapacityCount =
        static_cast<int32_t>(instanceRingCapacity_ / sizeof(Vixen::SVO::BodyInstanceGpu));
    const int32_t emittedCount = std::min(instanceCount_, ringCapacityCount);
    if (emittedCount < instanceCount_) {
        static bool warnedOnce = false;
        if (!warnedOnce) {
            warnedOnce = true;
            NODE_LOG_WARNING("[BodyOctreeSceneNode] instanceCount_ (" + std::to_string(instanceCount_) +
                              ") exceeds ring capacity (" + std::to_string(ringCapacityCount) +
                              ") — clamping emitted INSTANCE_COUNT until the pending recompile grows the ring");
        }
    }
    // Re-emit the count (it may change each frame if SetInstances was called).
    ctx.Out(BodyOctreeSceneNodeConfig::INSTANCE_COUNT, emittedCount);

    // Re-emit the octree slots with the freshly-created handles after a re-materialize,
    // so GetOutput()->GetHandle() (and any per-frame descriptor re-bind) sees the new buffers.
    if (octreeRepublished) {
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER,       nodesBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_BRICKS_BUFFER,      bricksBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_MATERIALS_BUFFER,   materialsBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,      configBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_SDF_BUFFER,         sdfBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_BRICKLOOKUP_BUFFER, brickLookupBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_MIPPOOL_BUFFER,     mipPoolBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_TIERREFTABLE_BUFFER, tierRefTableBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_OCCUPANCYGRID_BUFFER, occupancyGridBuffer_);
        // Rematerialize re-derived + re-created the shell buffers (both slots) inside
        // CreateOctreeBuffers; re-emit slot 0 so the render re-binds the fresh cache.
        ctx.Out(BodyOctreeSceneNodeConfig::SHELL_DATA_BUFFER,         shellDataBuffer_[0]);
        ctx.Out(BodyOctreeSceneNodeConfig::SHELL_LOOKUP_BUFFER,       shellLookupBuffer_[0]);
        ctx.Out(BodyOctreeSceneNodeConfig::PROXY_AABB_BUFFER,         proxyAabbBuffer_[0]);
        ctx.Out(BodyOctreeSceneNodeConfig::PROXY_AABB_COUNT,          proxyAabbCount_[0]);
    }

    // W-RTQUERY Slice A: (re)build the per-brick-AABB TLAS once octree buffers AND the
    // current instance list are both known -- a no-op unless VIXEN_RTQUERY_TRAVERSAL is
    // set AND the device actually supports VK_KHR_ray_query (flag-on-without-capability
    // logs a warning once and stays on ESVO; see EnsureRtQueryTlasBuilt's own guard).
    // instanceCount_==0 || octreeRepublished-without-instances is handled inside (rebuild
    // is keyed on (instanceCount_, concatenated_.count) identity, not called unconditionally
    // every frame past the first successful build).
    if (device) {
        EnsureRtQueryTlasBuilt(device);
        ctx.Out(BodyOctreeSceneNodeConfig::RTQUERY_TLAS, rtQueryTlas_);
    }
}

void BodyOctreeSceneNode::CleanupImpl(TypedCleanupContext& ctx) {
    // CRITICAL: recompile must NOT free in-flight GPU objects (WSL/Dozen VM-panic trap).
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (the old '!= FinalTeardown' guard)
    // left stale handles that crashed the first post-recovery use/teardown (KI-004 class).
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[BodyOctreeSceneNode] Cleanup (recompile) - keeping persistent buffers");
        return;
    }

    NODE_LOG_INFO("[BodyOctreeSceneNode] Cleanup (final teardown) - destroying buffers");
    DestroyBuffers();
}

// ============================================================================
// Octree build + serialization (cached)
// ============================================================================

void BodyOctreeSceneNode::EnsureOctreesBuilt() {
    if (octreesBuilt_) {
        return;
    }

    // I4.1: a pre-baked pool from SetRecipePool takes priority over ALL built-in paths.
    if (poolProvided_) {
        concatenated_ = providedPool_;   // ponytail: shallow copy (vector data owned by providedPool_)
        octreesBuilt_ = true;
        NODE_LOG_INFO("[BodyOctreeSceneNode] Using provided recipe pool (" +
                      std::to_string(concatenated_.count) + " octrees, channelPool=" +
                      std::to_string(concatenated_.channelPool.size()) + "B)");
        return;
    }

    // VIXEN_STORED_SDF_DEMO: bake the 3 body kinds as Stored-SDF octrees so the
    // Stored-SDF shader path (formatId == STORED_SDF, bindings 11/12) can be A/B'd
    // against the Procedural path (default when env var is unset).
    // VIXEN_BRICKMAP_SCENE (W-BRICKMAP Slice 2 round 3): the coarse-grid DDA
    // backend now serves STORED_SDF only (see SceneBindings.glsl's
    // traverseCoarseGridInstancedSdf header) -- reuses this SAME bake so
    // BuildRenderGraph.cpp's VIXEN_BRICKMAP_SCENE block points instances at
    // real STORED_SDF octrees instead of the FORMAT_BINARY shells brickGridLookup
    // was never populated for.
    // Round 12 (far-field default-coef parity): VIXEN_BRICKMAP_SCENE bakes a 4TH
    // Stored-SDF octree (kind 3, plain sphere, same recipe as kind 2) so
    // BuildRenderGraph.cpp's brickmapBodies block can place a far body beyond the
    // true default-coef tier-crossing distance (596.75wu) -- the first 3 near
    // bodies (D≈507-539wu, see plan ledger's Batch 11 entry) can never cross it.
    // VIXEN_STORED_SDF_DEMO keeps the original 3-body bake unchanged.
    const bool isBrickmapScene = envFlagEnabled("VIXEN_BRICKMAP_SCENE");
    // Round-18 multi-distance close: +2 more far bodies (kinds 4/5, D~800/D~1200) past
    // the round-12 far body (kind 3), reusing the plain-sphere recipe.
    // Deep-field-mip-policy design-doc regime-3 divergence scene (2026-08-08): +1 more
    // body (kind 6), gated on its OWN env var (VIXEN_SPARSE_BODY, default off, orthogonal
    // to VIXEN_BRICKMAP_SCENE the same way VIXEN_BRICKMAP_SCENE is orthogonal to
    // VIXEN_STORED_SDF_DEMO) -- an env-unset boot must bake exactly the round-18 six
    // bodies, byte-identical to every prior batch's regression baseline. Kind 6 is a
    // SCATTERED SHELL (not a solid sphere like kinds 0-5): baked via a custom eval
    // passed straight to BakeSdfWorld below (bypassing SdfRecipes.h/evalSdf entirely --
    // safe because the Stored-SDF provider never re-evaluates the analytic recipe on
    // GPU, it only samples the baked grid; confirmed via SceneBindings.glsl's
    // formatId==FORMAT_STORED_SDF dispatch, no RECIPE_* branch is reachable from that
    // path). ~40% of the shell's brick-granular surface is punched out by a coarse
    // hash mask, so the octree's coarse mip levels see fractional occupancy (coverage
    // strictly between 0 and 1 per node, by MipSample.h's fraction-of-8-child-octants
    // definition) -- the nebula-over-galaxy case the design doc calls for, as opposed
    // to a solid body whose mips saturate to coverage=1.
    // ponytail: sparse body only ever asked for alongside the brickmap scene (the
    // divergence matrix is brickmap-based end to end) -- no standalone combination.
    const bool isSparseBody = isBrickmapScene && envFlagEnabled("VIXEN_SPARSE_BODY");
    // Batch 50 / KI-047 residual (VIXEN_SPARSE_BODY_OVERLAP, sol-b49-validation.md
    // V1.3): +1 more kind, a plain sphere placed directly behind the round-12
    // D~612wu body on the SAME camera ray (BuildRenderGraph.cpp's
    // overlapBehindCenter) -- gives TraceWorld.glsl a genuine same-pass second
    // candidate to populate secondColor with, unlike the existing near/FAR/NEBULA
    // kinds which sit on distinct, non-overlapping rays. Orthogonal to
    // VIXEN_SPARSE_BODY the same way NEBULA is orthogonal to FAR: gated only on
    // VIXEN_SPARSE_BODY being set too, so an overlap-off boot bakes exactly the
    // same kind count as before (byte-identical, criterion C1).
    const bool isSparseBodyOverlap = isSparseBody && envFlagEnabled("VIXEN_SPARSE_BODY_OVERLAP");
    const uint32_t sdfKindCount =
        (isBrickmapScene ? kKindCount + 3 : kKindCount) + (isSparseBody ? 1 : 0) +
        (isSparseBodyOverlap ? 1 : 0);
    if (envFlagEnabled("VIXEN_STORED_SDF_DEMO") || isBrickmapScene) {
        NODE_LOG_INFO("[BodyOctreeSceneNode] VIXEN_STORED_SDF_DEMO/VIXEN_BRICKMAP_SCENE/VIXEN_SPARSE_BODY: baking " +
                      std::to_string(sdfKindCount) + " Stored-SDF octrees");

        // Grid: n=64 → bricksPerAxis=8 (2^(log2(64)-brickDepth=3) = 2^3 = 8).
        // center=(32,32,32); radius 26 leaves a 6-voxel margin to the [0,64] walls.
        // bandVoxels=2.5 → HONEST narrow-band SDF (interior + far-exterior bricks are
        // unallocated). The renderer must traverse this sparse field correctly — see the
        // "ESVO-leaf-hit traversal" plan in the Inc2 design/plan docs (the next step).
        // (Live gate found the standalone marchStoredSdf flat sphere-trace mishandles the
        //  sparse edges; the fix is to reuse the ESVO traversal with an SDF leaf-hit, NOT
        //  to densify the data.)
        constexpr int   kSdfN          = 64;
        constexpr float kSdfCenter     = 32.0f;
        constexpr float kSdfRadius     = 26.0f;  // 6-voxel margin to the [0,64] walls
        constexpr float kSdfBand       = 2.5f;   // narrow band (honest sparse data)
        constexpr int   kSdfBrickDepth = 3;

        const glm::vec3 center(kSdfCenter, kSdfCenter, kSdfCenter);

        // Bake 3 Stored-SDF bodies:
        //   kind 0 — smooth sphere (left,   materialId=1 red)
        //   kind 1 — displaced sphere (centre, materialId=2 green; amp≈2.7, freq≈0.375)
        //   kind 2 — smooth sphere (right,  materialId=3 white)
        //
        // amp=2.7 ≈ 2.7 grid-voxels of displacement; freq=0.375 gives ≈3 sinusoidal
        // cycles across the [0,64] grid. The displaced body is visibly distinct from
        // the plain spheres without blowing out the narrow-band.
        struct SdfKind {
            uint32_t recipeId;
            float    displaceAmp;
            float    displaceFreq;
        };
        constexpr SdfKind kSdfKinds[kKindCount + 3] = {
            { Vixen::SVO::RECIPE_SPHERE,           0.0f, 0.0f   },  // kind 0: smooth
            { Vixen::SVO::RECIPE_DISPLACED_SPHERE, 2.7f, 0.375f },  // kind 1: displaced
            { Vixen::SVO::RECIPE_SPHERE,           0.0f, 0.0f   },  // kind 2: smooth
            { Vixen::SVO::RECIPE_SPHERE,           0.0f, 0.0f   },  // kind 3: smooth (VIXEN_BRICKMAP_SCENE far body, D~612wu)
            { Vixen::SVO::RECIPE_SPHERE,           0.0f, 0.0f   },  // kind 4: smooth (round-18 multi-distance, D~800wu)
            { Vixen::SVO::RECIPE_SPHERE,           0.0f, 0.0f   },  // kind 5: smooth (round-18 multi-distance, D~1200wu)
        };

        std::vector<Vixen::SVO::SdfBodyOctree> sdfOctrees;
        sdfOctrees.reserve(sdfKindCount);

        // Deep-field-mip-policy regime-3 scene: kind 6 (VIXEN_SPARSE_BODY only) is a
        // SCATTERED SHELL, not a solid recipe -- baked via a raw eval lambda straight
        // into BakeSdfWorld (same core BakeRecipeToSdfWorld itself calls), so it never
        // touches kSdfKinds/evalSdf/SdfRecipes.h. Shell: signed distance to a thin
        // spherical band (|len(p-center)-radius| - halfThickness), occupancy-gated the
        // same way every other body is (sd <= bandVoxels marks a brick occupied) --
        // BakeSdfWorld's own two-pass occupancy/dilation logic is untouched. On top of
        // the shell distance, a coarse hash mask pushes most of the shell's bricks
        // OUTSIDE the band (sd forced to a large positive value there), so only a
        // fraction of the shell's surface actually bakes as occupied.
        // ⚠ BATCH-41 LESSON (validator-measured): the mask must out-run SdfBake.h's
        // one-brick occupancy DILATION (SdfBake.h:176-201). The first cut masked
        // single bricks (keep 40%): every gap was exactly one brick wide, dilation
        // sealed ALL of them, and the baked body came out 98% dense -- coverage<1
        // never existed in the data. Fix: mask on 4-BRICK cells (gaps >= 4 bricks
        // survive a 1-brick skirt as >= 2-brick holes) and keep only ~25% of cells,
        // since the dilated skirt re-inflates whatever is kept (~(6/4)^2 on the
        // shell surface). Truth instrument: the baked pool BYTES ([BrickDataHash]
        // sizes:, printed unconditionally) must land well below a dense body's ~6.24MB.
        constexpr uint32_t kSparseBodyKind = kKindCount + 3;  // = 6
        constexpr uint32_t kSparseBodyOverlapKind = kKindCount + 4;  // = 7 (VIXEN_SPARSE_BODY_OVERLAP only)
        // Coarse occupancy tally (measured evidence tier, not behavioral inference):
        // counts every DISTINCT brick index sparseShellEval is asked to classify and
        // how many it keeps, giving an exact kept-brick fraction for the boot log
        // (a stronger claim than "the eval formula implies ~40%" -- this is the actual
        // fraction realized against the shell's true brick footprint, since a brick
        // near the shell's radius may be queried many times but only counts once).
        std::unordered_map<uint64_t, bool> sparseBrickSeen;
        auto sparseShellEval = [&](const glm::vec3& p) -> float {
            constexpr float kShellRadius    = kSdfRadius;       // same radius as the other far bodies
            constexpr float kShellHalfThick = 6.0f;             // grid voxels, thick enough to survive brickDepth=3 dilation
            constexpr int   kBrickSide      = 1 << kSdfBrickDepth;  // 8
            const glm::vec3 d = p - center;
            const float shellSd = std::fabs(glm::length(d) - kShellRadius) - kShellHalfThick;
            // Coarse per-brick hash mask (deterministic, no RNG state -- same brick
            // index always masks the same way, so the bake is reproducible run to
            // run, required by the determinism convention this repo holds sim/bake
            // code to elsewhere).
            // Source-faithful offline search (2026-08-09, batch-46): with the
            // bake's 26-neighbour dilation, a 6-brick cell at 25% re-densifies
            // too much. A 2-brick mask cell at 3% keep leaves fractional
            // level-2 nodes and fractional level-1 nodes on the +Z shell front.
            // The selected geometry realizes 111 active bricks (21.7% of 512)
            // in the source-faithful model.
            constexpr int kMaskCellBricks = 2;
            const int bx = static_cast<int>(p.x) / (kBrickSide * kMaskCellBricks);
            const int by = static_cast<int>(p.y) / (kBrickSide * kMaskCellBricks);
            const int bz = static_cast<int>(p.z) / (kBrickSide * kMaskCellBricks);
            uint32_t h = static_cast<uint32_t>(bx * 73856093) ^
                         static_cast<uint32_t>(by * 19349663) ^
                         static_cast<uint32_t>(bz * 83492791);
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            const bool brickMaskedOut = (h % 100u) >= 3u;  // keep 3% of mask cells (pre-dilation)
            if (shellSd <= kSdfBand) {
                const uint64_t brickKey = (static_cast<uint64_t>(static_cast<uint32_t>(bx)) << 42) ^
                                           (static_cast<uint64_t>(static_cast<uint32_t>(by)) << 21) ^
                                            static_cast<uint64_t>(static_cast<uint32_t>(bz));
                sparseBrickSeen[brickKey] = !brickMaskedOut;
            }
            return brickMaskedOut ? 1e6f : shellSd;
        };

        for (uint32_t k = 0; k < sdfKindCount; ++k) {
            Vixen::SVO::SdfBakeResult baked;
            // ponytail: recipe injection for octree 0 only; analytic path unchanged for k>0
            if (k == 0 && !bakeRecipe_.empty()) {
                NODE_LOG_INFO("[BodyOctreeSceneNode] octree 0: baking via recipe ("
                              + std::to_string(bakeRecipe_.size()) + " instructions)");
                baked = Vixen::SVO::BakeRecipeInstructionsToSdfWorld(
                    bakeRecipe_.data(), static_cast<uint32_t>(bakeRecipe_.size()),
                    center, kSdfN, kSdfBand, kSdfBrickDepth);
            } else if (k == kSparseBodyKind) {
                baked = Vixen::SVO::BakeSdfWorld(sparseShellEval, center, kSdfN, kSdfBand, kSdfBrickDepth);
            } else if (k == kSparseBodyOverlapKind) {
                // Plain solid sphere, same recipe/radius as kinds 3/4/5 -- the
                // "behind" body only needs to be a real, brick-resident SDF body
                // TraceWorld.glsl can actually hit; placement (not baked shape)
                // is what makes it overlap the D~612wu body's ray.
                Vixen::SVO::RecipeParams rp{};
                rp.radius = kSdfRadius;
                baked = Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, center, rp, kSdfN, kSdfBand);
            } else {
                const SdfKind& sk = kSdfKinds[k];
                Vixen::SVO::RecipeParams rp{};
                rp.radius       = kSdfRadius;
                rp.displaceAmp  = sk.displaceAmp;
                rp.displaceFreq = sk.displaceFreq;
                baked = Vixen::SVO::BakeRecipeToSdfWorld(sk.recipeId, center, rp, kSdfN, kSdfBand);
            }
            sdfOctrees.push_back(
                Vixen::SVO::BuildSdfBodyOctree(baked, kSdfBrickDepth));
        }

        if (isSparseBody && !sparseBrickSeen.empty()) {
            size_t kept = 0;
            for (const auto& [key, isKept] : sparseBrickSeen) {
                if (isKept) ++kept;
            }
            const double keptFraction = static_cast<double>(kept) / static_cast<double>(sparseBrickSeen.size());
            NODE_LOG_INFO("[SparseBodyCoverage] shellBricksSeen=" + std::to_string(sparseBrickSeen.size()) +
                          " kept=" + std::to_string(kept) +
                          " keptFraction=" + std::to_string(keptFraction) +
                          " (measured brick-granular occupancy; NOT the octree mip-node coverage --"
                          " see [Regime3]/[PolicyEntryDispatch] GPU counters for the runtime coverage signal)");
        }

        std::vector<const Vixen::SVO::SdfBodyOctree*> sdfPtrs;
        sdfPtrs.reserve(sdfOctrees.size());
        for (const Vixen::SVO::SdfBodyOctree& s : sdfOctrees) {
            sdfPtrs.push_back(&s);
        }

        // Lazy-Procedural-Delta-Baseline Inc0 M1 Task 2: bake mips alongside the
        // Stored-SDF concat so mip-fallback rendering is available for this demo
        // path too (the default binary-shell branch below stays on plain
        // Concatenate — binary trees have channelCount==0, mips are structurally
        // impossible for them per MipFallback.glsl).
        //
        // Deep-Field Mip Policy — anisotropic coarse mips: same flag
        // (VIXEN_MIP_ANISO_BAKE) RecipeBaker.h consults, so a boot exercising
        // this Stored-SDF demo path prints the same [MipAnisoPool] evidence.
        // Flag-off (default) keeps this call byte-identical to before —
        // ConcatenateSdfWithMips only, no mipAnisoPool attached, identity
        // hash unmoved.
        if (envFlagEnabled("VIXEN_MIP_ANISO_BAKE")) {
            concatenated_ = Vixen::SVO::ConcatenateSdfWithAniso(sdfPtrs);
            // Batch-32 JOB 2a: the [MipAnisoPool] print RecipeBaker.h emits
            // lives off this runtime path (BodyOctreeSceneNode's Stored-SDF
            // demo boot never calls into RecipeBaker) -- reuses the pool
            // ConcatenateSdfWithAniso just built rather than re-baking for
            // diagnostics, so a SCENE=1 boot under VIXEN_MIP_ANISO_BAKE=1
            // carries the same evidence tag in its log.
            std::printf("[MipAnisoPool] bodies=%zu totalPoolBytes=%zu source=BodyOctreeSceneNode runtime pool-load\n",
                        sdfPtrs.size(), concatenated_.mipAnisoPool.size());
        } else {
            concatenated_ = Vixen::SVO::ConcatenateSdfWithMips(sdfPtrs);
        }
        octreesBuilt_ = true;

        NODE_LOG_INFO("[BodyOctreeSceneNode] Stored-SDF: built " +
                      std::to_string(concatenated_.count) + " SDF octrees (channelPool=" +
                      std::to_string(concatenated_.channelPool.size()) + "B, lookup=" +
                      std::to_string(concatenated_.brickGridLookup.size()) + "B)");
        return;
    }

    // Default (binary shell octrees): Build one owning shell octree per kind.
    // ShellOctree is move-only (unique_ptr members) and OWNS its world/registry/octree,
    // so the cached vector keeps them alive for the node's lifetime — required because
    // Serialize() reads the world.
    shellOctrees_.clear();
    shellOctrees_.reserve(kKindCount);
    for (uint32_t k = 0; k < kKindCount; ++k) {
        shellOctrees_.push_back(
            Vixen::SVO::BuildShellOctree(kShellDepth, MaterialIdForKind(k)));
    }

    std::vector<const Vixen::SVO::ShellOctree*> ptrs;
    ptrs.reserve(shellOctrees_.size());
    for (const Vixen::SVO::ShellOctree& s : shellOctrees_) {
        ptrs.push_back(&s);
    }

    concatenated_ = Vixen::SVO::Concatenate(ptrs);
    octreesBuilt_ = true;

    NODE_LOG_INFO("[BodyOctreeSceneNode] Built " + std::to_string(concatenated_.count) +
                  " shell octrees (nodes=" + std::to_string(concatenated_.nodes.size()) +
                  "B, bricks=" + std::to_string(concatenated_.bricks.size()) +
                  "B, materials=" + std::to_string(concatenated_.materials.size()) + "B)");
}

void BodyOctreeSceneNode::CreateOctreeBuffers(VulkanDevice* device) {
    // nodes / bricks / materials SSBOs from the concatenated byte buffers.
    // Each buffer must be non-empty for a valid VkBuffer; pad to a minimum if a
    // table happened to be empty (defensive — shell octrees produce real data).
    const VkDeviceSize nodesSize =
        std::max<VkDeviceSize>(concatenated_.nodes.size(), 1);
    const VkDeviceSize bricksSize =
        std::max<VkDeviceSize>(concatenated_.bricks.size(), 1);
    const VkDeviceSize materialsSize =
        std::max<VkDeviceSize>(concatenated_.materials.size(), 1);

    CreateHostBuffer(device, nodesSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        concatenated_.nodes.empty() ? nullptr : concatenated_.nodes.data(),
        nodesBuffer_, nodesMemory_, "octree nodes SSBO");

    // Inc1 M2: allocate the bricks SSBO at FULL capacity, but only populate it now if
    // residency was already requested before this Compile (e.g. Rematerialize re-running
    // with residencyRequested_ already true). Default is unpopulated ("mip-only tree") —
    // ExecuteImpl's UploadBrickPool (via BatchedUploader) fills it lazily on request.
    // hasBrick() (SVOTypes.h) reads ChildDescriptor.contourPointer, which lives in the
    // NODE array (nodesBuffer_, populated above unconditionally) — never in bricksBuffer_
    // itself, so an unwritten-but-allocated bricks buffer is already safely distinguishable
    // from a populated one at traversal time; no additional GPU-side flag is needed.
    //
    // Tiered-ESVO Inc2 M5 Task 11: TRANSFER_DST_BIT is REQUIRED here, not just STORAGE_BUFFER_BIT
    // -- UploadBrickPool's post-Compile residency grant (device->Upload -> BatchedUploader's
    // vkCmdCopyBuffer) targets this exact buffer, and a lazy false->true RequestBrickResidency
    // grant landing AFTER the first Compile (the real "residency arrives mid-flight while a ray
    // is crossing" case this milestone's live gate exercises for the first time — M4's own tests
    // only ever toggled residency BEFORE the first Compile/upload cycle) hit
    // VUID-vkCmdCopyBuffer-dstBuffer-00120 on real hardware with validation on: this buffer was
    // created host-visible/mapped-write-only, with no path for a later GPU-side copy into it.
    CreateHostBuffer(device, bricksSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        (residencyRequested_ && !concatenated_.bricks.empty()) ? concatenated_.bricks.data() : nullptr,
        bricksBuffer_, bricksMemory_, "octree bricks SSBO");
    brickPoolUploaded_ = residencyRequested_ && !concatenated_.bricks.empty();
    if (brickPoolUploaded_) {
        wholesaleAvailability_.committedRegime = Vixen::SVO::CellFootprintRegime::Surface;
        wholesaleAvailability_.readyMask =
            static_cast<uint32_t>(Vixen::SVO::WholesalePayload::ChannelPool) |
            static_cast<uint32_t>(Vixen::SVO::WholesalePayload::BrickLookup);
    }
    for (auto& cfg : concatenated_.configs) {
        cfg._tailPad[0] = wholesaleAvailability_.readyMask;
    }

    CreateHostBuffer(device, materialsSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        concatenated_.materials.empty() ? nullptr : concatenated_.materials.data(),
        materialsBuffer_, materialsMemory_, "octree materials SSBO");

    // Inc1 M3 Task 7: stamp brickResident into every octree's config so the shader's
    // leaf-hit existence check can distinguish "allocated but not populated" from
    // "fully uploaded" — hasBrick()/contourPointer alone cannot (M2's descriptor
    // pointer stays valid regardless of residency). Config bytes are re-uploaded
    // below this same Compile/Rematerialize call, so this always reflects the
    // brickPoolUploaded_ value just computed above -- UNLESS a caller already
    // stamped heterogeneous per-octree residency directly on the provided pool
    // (SetRecipePool, before this Compile) -- test_tier_crossing_lod_residency.cpp's
    // documented convention for driving one octree non-resident within an otherwise-
    // resident concatenated pool, since RequestBrickResidency is a whole-node flag
    // and structurally cannot express per-octree divergence. Detected by scanning for
    // any two configs that already disagree; if none disagree, every config is at
    // either its post-copy default or a prior uniform stamp, so the normal uniform
    // stamp applies exactly as before (zero behavior change for every ordinary
    // single-residency scene). brickPoolUploaded_==false is a hard hardware truth
    // (no bricks physically landed in bricksBuffer_ at all) and always wins --
    // heterogeneous-but-unuploaded configs are still forced to non-resident.
    bool residencyHeterogeneous = false;
    for (size_t i = 1; i < concatenated_.configs.size(); ++i) {
        if (Vixen::SVO::brickResidentOf(concatenated_.configs[i]) !=
            Vixen::SVO::brickResidentOf(concatenated_.configs[0])) {
            residencyHeterogeneous = true;
            break;
        }
    }
    if (!brickPoolUploaded_ || !residencyHeterogeneous) {
        for (auto& cfg : concatenated_.configs) {
            Vixen::SVO::setBrickResident(cfg, brickPoolUploaded_);
        }
    }
    // else: bricks are uploaded AND the caller's per-octree stamps already diverge
    // -- preserve them untouched (this is the ONLY branch that changes behavior).

    // Config SSBO (binding 5, std430): one 432-byte OctreeConfig per octree.
    // ponytail: min 1 entry so the buffer is never zero-byte.
    //
    // Tiered-ESVO Inc2 M5 Task 11: also needs TRANSFER_DST_BIT, same reasoning as bricksBuffer_
    // above -- PollBrickUploadCompletion's phase-2 re-upload (device->Upload(...configBuffer_...),
    // stamping brickResident=1 config bytes once the brick copy lands) targets this buffer via
    // the same vkCmdCopyBuffer path and hit the identical VUID-vkCmdCopyBuffer-dstBuffer-00120
    // without it.
    //
    // B1 M4 A/B gate: also needs TRANSFER_SRC_BIT -- test_b1_occlusion_ab.cpp reads this buffer
    // back via vkCmdCopyBuffer (to patch traceBounds and cross-check the CPU cull mirror against
    // the real device config), which requires TRANSFER_SRC_BIT on the copy source
    // (VUID-vkCmdCopyBuffer-srcBuffer-00118). The node only exposes VkBuffer via
    // OCTREE_CONFIG_BUFFER (never the backing VkDeviceMemory), so a caller-side vkMapMemory
    // readback isn't possible -- a copy is the only externally-reachable path. This VUID silently
    // downgrades to a no-op instead of failing on WSL/Dozen's weaker validation, which is why the
    // gap surfaced only on native Windows/AMD (iterCounts read back all-zero from a copy whose
    // destination buffer was never actually written).
    const VkDeviceSize configSize =
        static_cast<VkDeviceSize>(std::max<uint32_t>(concatenated_.count, 1u)) *
        static_cast<VkDeviceSize>(sizeof(Vixen::SVO::OctreeConfig));
    CreateHostBuffer(device, configSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        concatenated_.configs.empty() ? nullptr : concatenated_.configs.data(),
        configBuffer_, configMemory_, "octree config SSBO");

    // Inc3 M2: Generic multi-channel pool buffer (binding 11) + brick-grid lookup (binding 12).
    // Pad to 1 byte when empty — binary/Procedural bodies leave channelPool empty;
    // the shader only reads these when OctreeConfig.formatId == FORMAT_STORED_SDF (1u).
    // S5: when the compact shell is the active render payload, the legacy
    // source pair remains descriptor-valid but no longer needs a wholesale
    // allocation. Keep the full source buffers for flag-off identity and for
    // non-shell paths; the shell pair owns the real shader-readable bytes.
    const bool shellReplacesSourcePair = wholesaleAdmissionEnabled_ &&
        !shellCache_[0].compact.channelPool.empty() &&
        !shellCache_[0].compact.brickGridLookup.empty();
    const VkDeviceSize sdfSize = shellReplacesSourcePair ? 1 :
        std::max<VkDeviceSize>(concatenated_.channelPool.size(), 1);
    const VkDeviceSize brickLookupSize = shellReplacesSourcePair ? 1 :
        std::max<VkDeviceSize>(concatenated_.brickGridLookup.size(), 1);

    // E23-S3: a mip-only wholesale leg keeps the mip payload populated, but the
    // fine pair is deliberately reserved without being copied. This removes the
    // legacy compile-time wholesale path from the admission transfer; the
    // descriptor remains valid because the destination still has its full size.
    const bool suppressFineWholesale = wholesaleAdmissionEnabled_ &&
        wholesaleAvailability_.committedRegime != Vixen::SVO::CellFootprintRegime::Surface;

    CreateHostBuffer(device, sdfSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        suppressFineWholesale || concatenated_.channelPool.empty() ? nullptr : concatenated_.channelPool.data(),
        sdfBuffer_, sdfMemory_, "channel pool SSBO");

    CreateHostBuffer(device, brickLookupSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        suppressFineWholesale || concatenated_.brickGridLookup.empty() ? nullptr : concatenated_.brickGridLookup.data(),
        brickLookupBuffer_, brickLookupMemory_, "brick-grid lookup SSBO");
    Vixen::SVO::RetainWholesalePayload(wholesaleAvailability_, Vixen::SVO::WholesalePayloadMask(),
        concatenated_.channelPool.size(), concatenated_.brickGridLookup.size(),
        Fnv1a64(concatenated_.channelPool), Fnv1a64(concatenated_.brickGridLookup));

    // Sparse-Mip ESVO LOD Inc1 M3: mip pool buffer (binding 13). Pad to 1 byte when empty
    // — a tree that was never mip-baked (ConcatenateSdf's plain, non-mip sibling) leaves
    // mipPool empty; the shader's readMipSample bounds-checks against mipPool.length()
    // and never reads past it.
    const VkDeviceSize mipPoolSize =
        std::max<VkDeviceSize>(concatenated_.mipPool.size(), 1);
    CreateHostBuffer(device, mipPoolSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        concatenated_.mipPool.empty() ? nullptr : concatenated_.mipPool.data(),
        mipPoolBuffer_, mipPoolMemory_, "mip pool SSBO");

    // Tiered-ESVO Inc2 M3: tier-crossing reference table buffer (binding 15). Pad to
    // 1 byte when empty — a scene with no tier-crossing leaves anywhere (the
    // overwhelming common case; M2's farBit==1 construction path is explicit opt-in)
    // leaves tierRefTable empty; the shader's traversal-restart bounds-checks against
    // tierRefTable.length() and never reads past it, exactly like mipPool above.
    // E24-S4: an unavailable table is represented by the one-byte placeholder.  The
    // traversal's bounds check then leaves far references unresolved, preserving the
    // parent mip fallback and any-hit no-occluder behavior; it must never see stale or
    // partially admitted tier records.
    const bool suppressTierRefTable = wholesaleAdmissionEnabled_ &&
        (wholesaleAvailability_.readyMask & static_cast<uint32_t>(Vixen::SVO::WholesalePayload::TierRefTable)) == 0u;
    const VkDeviceSize tierRefTableBytes = suppressTierRefTable ? 0u :
        concatenated_.tierRefTable.size() * sizeof(Vixen::SVO::TierRef);
    const VkDeviceSize tierRefTableSize =
        std::max<VkDeviceSize>(tierRefTableBytes, 1);
    CreateHostBuffer(device, tierRefTableSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        suppressTierRefTable || concatenated_.tierRefTable.empty() ? nullptr : concatenated_.tierRefTable.data(),
        tierRefTableBuffer_, tierRefTableMemory_, "tier-ref table SSBO");

    // Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13: occupancy grid buffer (binding 16).
    // Pad to 1 byte when empty — a scene with no derivable procedural recipe occupancy
    // grids (no procedural recipes, or all non-whitelisted-opcode) leaves occupancyGrid_
    // empty; the shader's getRecipeOccupancyGrid switch only ever emits gridDim>0 cases
    // for recipes that HAD a grid, and treats gridDim==0 as "skip the fast-path," so it
    // never indexes into this buffer when it's the 1-byte placeholder.
    const VkDeviceSize occupancyGridSize =
        std::max<VkDeviceSize>(occupancyGrid_.size() * sizeof(float), 1);
    CreateHostBuffer(device, occupancyGridSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        occupancyGrid_.empty() ? nullptr : occupancyGrid_.data(),
        occupancyGridBuffer_, occupancyGridMemory_, "occupancy grid SSBO");

    // E6-T2: charge every wholesale CreateHostBuffer call separately.  A
    // one-byte placeholder is still a known allocation and remains visible in
    // the ledger; it is not silently folded into the brick upload row.
    RecordWholeBufferUpload(static_cast<uint64_t>(nodesSize));
    RecordWholeBufferUpload(static_cast<uint64_t>(bricksSize));
    RecordWholeBufferUpload(static_cast<uint64_t>(materialsSize));
    RecordWholeBufferUpload(static_cast<uint64_t>(configSize));
    if (!suppressFineWholesale) {
        RecordWholeBufferUpload(static_cast<uint64_t>(sdfSize));
        RecordWholeBufferUpload(static_cast<uint64_t>(brickLookupSize));
    }
    RecordWholeBufferUpload(static_cast<uint64_t>(mipPoolSize));
    RecordWholeBufferUpload(static_cast<uint64_t>(tierRefTableSize));
    RecordWholeBufferUpload(static_cast<uint64_t>(occupancyGridSize));

    NODE_LOG_INFO("[BodyOctreeSceneNode] Created octree buffers (nodes=" +
                  std::to_string(static_cast<uint64_t>(nodesSize)) + "B, bricks=" +
                  std::to_string(static_cast<uint64_t>(bricksSize)) + "B, materials=" +
                  std::to_string(static_cast<uint64_t>(materialsSize)) + "B, config=" +
                  std::to_string(static_cast<uint64_t>(configSize)) + "B, channelPool=" +
                  std::to_string(static_cast<uint64_t>(sdfSize)) + "B, brickLookup=" +
                  std::to_string(static_cast<uint64_t>(brickLookupSize)) + "B, mipPool=" +
                  std::to_string(static_cast<uint64_t>(mipPoolSize)) + "B, tierRefTable=" +
                  std::to_string(static_cast<uint64_t>(tierRefTableSize)) + "B, occupancyGrid=" +
                  std::to_string(static_cast<uint64_t>(occupancyGridSize)) + "B)");

    // Batch 10 (validator-requested cheap add): NODE_LOG_INFO above is disabled
    // by default for these nodes and its absence blocked settling the batch-9
    // question from artifacts. Mirror the mipPool size on the proven std::cout
    // channel (same route as VoxelGridNode's [FarField*] counters) so boot logs
    // record whether the mip pool is non-empty at all.
    std::cout << "[MipPoolSize] n=" << static_cast<uint64_t>(mipPoolSize) << std::endl;

    // W-BRICKMAP Slice 2 boot-data-variation theory test: hash the CPU-side finalized
    // octree/SDF data at the exact point it's uploaded (bytes above are copied verbatim
    // from these same vectors). Unconditional, INFO level — boot-time one-shot, cheap.
    {
        const uint64_t nodesHash  = Fnv1a64(concatenated_.nodes);
        const uint64_t poolHash   = Fnv1a64(concatenated_.channelPool);
        const uint64_t lookupHash = Fnv1a64(concatenated_.brickGridLookup);
        const uint64_t configsHash = Fnv1a64(concatenated_.configs);
        char hexBuf[160];
        std::snprintf(hexBuf, sizeof(hexBuf),
            "[BrickDataHash] nodes=%016llx pool=%016llx lookup=%016llx configs=%016llx",
            static_cast<unsigned long long>(nodesHash),
            static_cast<unsigned long long>(poolHash),
            static_cast<unsigned long long>(lookupHash),
            static_cast<unsigned long long>(configsHash));
        // FIX 5: nodeLogger is disabled by default for this node (never SetEnabled(true) anywhere
        // in BuildRenderGraph, unlike deviceNode/provider/camera) -- NODE_LOG_INFO is therefore a
        // silent no-op for every line in this file. Emit straight to stdout instead, the same
        // channel mainLogger->Info(...) ultimately reaches (Logger::Log's terminalOutput path),
        // which every boot-log capture in the sweep tooling redirects to a file.
        std::cout << hexBuf << std::endl;
        std::cout << "[BrickDataHash] sizes: nodes=" << concatenated_.nodes.size() <<
                      "B pool=" << concatenated_.channelPool.size() <<
                      "B lookup=" << concatenated_.brickGridLookup.size() <<
                      "B configs=" << (concatenated_.configs.size() * sizeof(Vixen::SVO::OctreeConfig)) << "B" << std::endl;
    }

    // Surface-Shell ESVO cache: derive the reachable shell of octree 0 from the
    // just-created full pool into BOTH CPU slots, then bootstrap both GPU slots.
    // Render binds SHELL_DATA/SHELL_LOOKUP (the compact pool) — never the full pool.
    DeriveShellCache();
    CreateShellBuffers(device);
}

void BodyOctreeSceneNode::EnsureRingAllocated(VulkanDevice* device, VkDeviceSize neededCapacity) {
    if (perFrame_.IsInitialized() && neededCapacity <= instanceRingCapacity_) {
        // Ring already exists and is large enough — nothing to do.
        NODE_LOG_INFO("[BodyOctreeSceneNode] Reusing persistent instance ring (capacity=" +
                      std::to_string(static_cast<uint64_t>(instanceRingCapacity_)) + "B)");
        return;
    }

    // Grow path: we must wait for all in-flight frames before destroying and recreating
    // the ring. This is a RARE path (capacity overflow), not the per-frame path.
    if (perFrame_.IsInitialized()) {
        NODE_LOG_INFO("[BodyOctreeSceneNode] Growing instance ring (old=" +
                      std::to_string(static_cast<uint64_t>(instanceRingCapacity_)) +
                      "B → new=" + std::to_string(static_cast<uint64_t>(neededCapacity)) +
                      "B) — vkDeviceWaitIdle");
        vkDeviceWaitIdle(device->device);
        perFrame_.Cleanup();
        instanceRingCapacity_ = 0;
    }

    // Allocate fresh ring of kRingSize storage buffers.
    perFrame_.Initialize(device, kRingSize);
    for (uint32_t i = 0; i < kRingSize; ++i) {
        perFrame_.CreateStorageBuffer(i, neededCapacity);
    }
    instanceRingCapacity_ = neededCapacity;

    NODE_LOG_INFO("[BodyOctreeSceneNode] Allocated instance ring: " +
                  std::to_string(kRingSize) + " x " +
                  std::to_string(static_cast<uint64_t>(neededCapacity)) + "B storage buffers");
}

void BodyOctreeSceneNode::Rematerialize() {
    VulkanDevice* device = GetDevice();
    if (!device) {
        NODE_LOG_ERROR("[BodyOctreeSceneNode] Rematerialize called with no device");
        return;
    }
    NODE_LOG_INFO("[BodyOctreeSceneNode] Rematerialize: rebuilding octree buffers");

    // Rare, explicit edit path — safe to stall (mirrors the ring-grow vkDeviceWaitIdle).
    // Guarantees no in-flight command buffer still references the octree buffers we free.
    vkDeviceWaitIdle(device->device);

    octreesBuilt_ = false;     // force EnsureOctreesBuilt to re-bake + re-concatenate all 3 octrees
    EnsureOctreesBuilt();      // octree 0 uses the new bakeRecipe_; octrees 1/2 unchanged

    DestroyOctreeBuffers();    // ring is NOT touched
    CreateOctreeBuffers(device);
}

// ============================================================================
// Surface-Shell ESVO cache
// ============================================================================

void BodyOctreeSceneNode::SetShellThickness(uint32_t dilation) {
    const uint32_t clamped = dilation < 1u ? 1u : (dilation > 3u ? 3u : dilation);
    if (clamped != shellDilation_) {
        shellDilation_ = clamped;
        NODE_LOG_INFO("[BodyOctreeSceneNode] SetShellThickness: shellDilation=" +
                      std::to_string(shellDilation_));
    }
}

void BodyOctreeSceneNode::DeriveShellCache() {
    // Derive the reachable shell of EVERY octree into BOTH CPU double-buffer slots
    // (bootstrap; byte-identical). Multi-octree safe: DeriveShellPool assembles a
    // compact, render-equivalent ConcatenatedOctrees (compact pool + grid remap +
    // per-octree poolBrickBase). No-op for binary/Procedural (no SDF pool).
    if (concatenated_.count == 0u || concatenated_.channelPool.empty()) {
        shellCache_[0] = Vixen::SVO::ShellPool{};
        shellCache_[1] = Vixen::SVO::ShellPool{};
        return;
    }
    Vixen::SVO::ShellDeriveParams params;
    params.shellDilation = shellDilation_;
    try {
        Vixen::SVO::ShellPool derived =
            Vixen::SVO::DeriveShellPool(concatenated_, params);
        shellCache_[0] = derived;             // slot 0 (copy)
        shellCache_[1] = std::move(derived);  // slot 1 (byte-identical bootstrap)
        ++shellFullDeriveCount_;

        const Vixen::SVO::ShellPool& s = shellCache_[0];
        NODE_LOG_INFO("[BodyOctreeSceneNode] Shell pool derived (dilation=" +
                      std::to_string(shellDilation_) + ", octrees=" +
                      std::to_string(s.compact.count) +
                      "): pool " + std::to_string(s.sourcePoolBytes) + "B -> " +
                      std::to_string(s.shellPoolBytes) + "B (" +
                      (s.sourcePoolBytes ? std::to_string(
                          100ull * s.shellPoolBytes / s.sourcePoolBytes) : std::string("100")) +
                      "%), compact channelPool=" +
                      std::to_string(s.compact.channelPool.size()) + "B lookup=" +
                      std::to_string(s.compact.brickGridLookup.size()) + "B");
    } catch (const std::exception& e) {
        NODE_LOG_WARNING(std::string("[BodyOctreeSceneNode] Shell derive skipped: ") + e.what());
        shellCache_[0] = Vixen::SVO::ShellPool{};
        shellCache_[1] = Vixen::SVO::ShellPool{};
    }
}

void BodyOctreeSceneNode::UploadShellSlot(VulkanDevice* device, uint32_t slot) {
    slot &= 1u;
    const Vixen::SVO::ShellPool& sp = shellCache_[slot];

    // Compact pool (binding-11 replacement) — pad to 1 byte when empty so the
    // descriptor is always valid (binary/Procedural non-regression invariant).
    const VkDeviceSize dataSize =
        std::max<VkDeviceSize>(sp.compact.channelPool.size(), 1);
    const VkDeviceSize lookupSize =
        std::max<VkDeviceSize>(sp.compact.brickGridLookup.size(), 1);

    // (Re)create the slot buffers only if capacity changed (shell size is stable
    // for a static doc; a re-derive at a new dilation may grow it).
    auto ensure = [&](VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize& cap,
                      VkDeviceSize needed, const void* data, size_t dataBytes,
                      const char* ctx) {
        if (buf != VK_NULL_HANDLE && cap >= needed) {
            // Reuse: re-map and overwrite in place (host-coherent).
            if (data && dataBytes > 0) {
                void* mapped = nullptr;
                if (vkMapMemory(device->device, mem, 0, dataBytes, 0, &mapped) == VK_SUCCESS) {
                    std::memcpy(mapped, data, dataBytes);
                    vkUnmapMemory(device->device, mem);
                }
            }
            return;
        }
        // Recreate at the new capacity.
        if (buf != VK_NULL_HANDLE) { vkDestroyBuffer(device->device, buf, nullptr); buf = VK_NULL_HANDLE; }
        if (mem != VK_NULL_HANDLE) { vkFreeMemory(device->device, mem, nullptr);    mem = VK_NULL_HANDLE; }
        CreateHostBuffer(device, needed, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         (data && dataBytes > 0) ? data : nullptr, buf, mem, ctx);
        cap = needed;
        RecordWholeBufferUpload(static_cast<uint64_t>(needed));
    };

    ensure(shellDataBuffer_[slot], shellDataMemory_[slot], shellDataCapacity_[slot],
           dataSize, sp.compact.channelPool.empty() ? nullptr : sp.compact.channelPool.data(),
           sp.compact.channelPool.size(), "shell data SSBO");
    ensure(shellLookupBuffer_[slot], shellLookupMemory_[slot], shellLookupCapacity_[slot],
           lookupSize, sp.compact.brickGridLookup.empty() ? nullptr : sp.compact.brickGridLookup.data(),
           sp.compact.brickGridLookup.size(), "shell lookup SSBO");

    // Raster-proxy artifact: flatten the per-octree proxy AABB lists (template-
    // local space) into one contiguous SSBO for the proxy raster pre-pass. Value
    // edits never move a brick's box, so a revalidate re-upload rewrites identical
    // bytes; the flatten stays here (not just Create) so a future re-derive at a
    // new dilation/membership keeps slot content and capacity honest.
    std::vector<Vixen::SVO::ShellProxyAabb> flatProxies;
    {
        size_t proxyCount = 0;
        for (const auto& r : sp.perOctree) proxyCount += r.proxyAabbs.size();
        flatProxies.reserve(proxyCount);
        for (const auto& r : sp.perOctree)
            flatProxies.insert(flatProxies.end(), r.proxyAabbs.begin(), r.proxyAabbs.end());
    }
    const VkDeviceSize proxySize = std::max<VkDeviceSize>(
        flatProxies.size() * sizeof(Vixen::SVO::ShellProxyAabb), 1);
    proxyAabbCount_[slot] = static_cast<uint32_t>(flatProxies.size());
    ensure(proxyAabbBuffer_[slot], proxyAabbMemory_[slot], proxyAabbCapacity_[slot],
           proxySize, flatProxies.empty() ? nullptr : flatProxies.data(),
           flatProxies.size() * sizeof(Vixen::SVO::ShellProxyAabb), "shell proxy AABB SSBO");
    ++proxyAabbUploadCount_[slot];
}

void BodyOctreeSceneNode::CreateShellBuffers(VulkanDevice* device) {
    // Bootstrap BOTH GPU slots from the (identical) CPU cache so frame 0's render
    // reads a valid shell regardless of which slot &1 selects.
    UploadShellSlot(device, 0);
    UploadShellSlot(device, 1);

    // The render reads binding 5 (OCTREE_CONFIG_BUFFER) for poolBrickBase /
    // brickStrideFloats / bricksPerAxisSdf. The COMPACT pool re-packs bricks so its
    // per-octree poolBrickBase differs from the source; rewrite the config buffer to
    // the compact configs so binding-5 addressing matches the compact pool the
    // render now reads. (For a single-octree pool poolBrickBase is 0 in both, so
    // this is a no-op; it is the multi-octree correctness fix.)
    const Vixen::SVO::ShellPool& sp = shellCache_[0];
    if (!sp.compact.configs.empty() && configBuffer_ != VK_NULL_HANDLE) {
        const VkDeviceSize cfgBytes =
            sp.compact.configs.size() * sizeof(Vixen::SVO::OctreeConfig);
        void* mapped = nullptr;
        if (vkMapMemory(device->device, configMemory_, 0, cfgBytes, 0, &mapped) == VK_SUCCESS) {
            std::memcpy(mapped, sp.compact.configs.data(), static_cast<size_t>(cfgBytes));
            vkUnmapMemory(device->device, configMemory_);
        }
    }
    size_t proxyCount = 0;
    for (const auto& r : sp.perOctree) proxyCount += r.proxyAabbs.size();
    NODE_LOG_INFO("[BodyOctreeSceneNode] Shell GPU buffers created (slot0 data=" +
                  std::to_string(static_cast<uint64_t>(shellDataCapacity_[0])) + "B lookup=" +
                  std::to_string(static_cast<uint64_t>(shellLookupCapacity_[0])) + "B; slot1 data=" +
                  std::to_string(static_cast<uint64_t>(shellDataCapacity_[1])) + "B lookup=" +
                  std::to_string(static_cast<uint64_t>(shellLookupCapacity_[1])) + "B; proxyAabbs=" +
                  std::to_string(proxyCount) + " x32B x2 slots)");
}

void BodyOctreeSceneNode::UploadBrickPool() {
    VulkanDevice* device = GetDevice();
    if (!device) {
        NODE_LOG_ERROR("[BodyOctreeSceneNode] UploadBrickPool called with no device");
        return;
    }

    // De-residency (false): Inc1 §0 scope is per-tree binary "not requested"/"fully
    // uploaded" with no GPU-memory-reclaim requirement this increment (M4c's optional
    // concern, decided by M5's bandwidth measurement) — "stop requesting" is sufficient;
    // there is no brick data to un-write. Only handle the populate (true) direction here.
    if (!residencyRequested_ || concatenated_.bricks.empty()) {
        NODE_LOG_INFO("[BodyOctreeSceneNode] UploadBrickPool: no-op (residencyRequested_=" +
                      std::string(residencyRequested_ ? "true" : "false") + ", bricks=" +
                      std::to_string(concatenated_.bricks.size()) + "B)");
        return;
    }
    if (brickPoolUploaded_) {
        NODE_LOG_INFO("[BodyOctreeSceneNode] UploadBrickPool: already uploaded, skipping");
        return;
    }

    const VkDeviceSize size = static_cast<VkDeviceSize>(concatenated_.bricks.size());
    const auto handle = device->Upload(concatenated_.bricks.data(), size, bricksBuffer_, 0);
    if (handle == ResourceManagement::InvalidUploadHandle) {
        NODE_LOG_ERROR("[BodyOctreeSceneNode] UploadBrickPool: BatchedUploader::Upload failed ("
                      + std::to_string(static_cast<uint64_t>(size)) + "B)");
        return;
    }

    // Inc1 M4c: kick off GPU execution without blocking (was device->WaitAllUploads(), a
    // synchronous vkDeviceWaitIdle-equivalent stall) — M2's assumption that residency
    // toggles are rare no longer holds once M4c re-checks the trigger every frame the
    // camera moves/zooms/rotates. brickPoolUploaded_/brickResident are NOT flipped here;
    // PollBrickUploadCompletion() (called every ExecuteImpl) advances the rest of this
    // state machine once the GPU-side copy is actually visible, non-blocking.
    device->FlushUploads();
    pendingBrickUploadHandle_ = handle;
    ++wholesalePairTransferCount_;

    RecordBrickPoolUpload(static_cast<uint64_t>(size));

    // Inc1 M4 Task 6b: first-ever queue is "boot", every later one (a residency toggle) is
    // "steady-state". brickPoolUploaded_ hasn't flipped true yet at this point (that happens
    // in PollBrickUploadCompletion once the GPU-side copy lands), so bootBytesUploaded_==0
    // is exactly "boot upload never queued".
    if (!bootUploadRecorded_) {
        bootBytesUploaded_ += static_cast<uint64_t>(size);
        bootUploadRecorded_ = true;
    } else {
        steadyStateBytesUploaded_ += static_cast<uint64_t>(size);
    }

    NODE_LOG_INFO("[BodyOctreeSceneNode] UploadBrickPool: queued " +
                  std::to_string(static_cast<uint64_t>(size)) + "B via BatchedUploader (async)");
}

void BodyOctreeSceneNode::PublishWholesaleReuse() {
    VulkanDevice* device = GetDevice();
    if (!device) return;
    lastWholesaleReusableBytes_ = wholesaleAvailability_.reusablePopulatedBytes;
    auto* activeConfigs = Vixen::SVO::StampAndSelectActiveConfigs(concatenated_, shellCache_);
    for (auto& cfg : *activeConfigs) cfg._tailPad[0] = Vixen::SVO::WholesalePayloadMask();
    const VkDeviceSize configSize = static_cast<VkDeviceSize>(activeConfigs->size()) * sizeof(Vixen::SVO::OctreeConfig);
    if (configSize == 0) return;
    const auto handle = device->Upload(activeConfigs->data(), configSize, configBuffer_, 0);
    if (handle == ResourceManagement::InvalidUploadHandle) return;
    device->FlushUploads();
    wholesaleAvailability_.readyMask = Vixen::SVO::WholesalePayloadMask();
    wholesaleAvailability_.pendingMask = 0u;
    wholesaleAvailability_.reusablePopulatedBytes = 0u;
    NODE_LOG_INFO("[WholesaleAvailability] reused retained channelPool+brickLookup; transfer_count=" +
                  std::to_string(wholesalePairTransferCount_));
}

void BodyOctreeSceneNode::RecordWholeBufferUpload(uint64_t bytes) {
    uploadLedger_.push_back(UploadLedgerEntry{
        UINT32_MAX, kWholePayloadLevel, bytes, 1u, true});
    std::cout << "[UploadLedger] body=whole-buffer level=whole-buffer-payload bytes="
              << bytes << " events=1" << std::endl;
    if (!bootUploadRecorded_) {
        bootBytesUploaded_ += bytes;
    } else {
        steadyStateBytesUploaded_ += bytes;
    }
}

void BodyOctreeSceneNode::RecordBrickPoolUpload(uint64_t bytes) {
    // Serialized metadata exposes per-octree brick counts, but not a reliable
    // per-level histogram. Attribute each segment to the explicit whole-payload
    // bucket instead of fabricating level attribution.
    uint64_t accounted = 0;
    for (uint32_t body = 0; body < concatenated_.brickCounts.size(); ++body) {
        const uint64_t remaining = bytes - std::min(bytes, accounted);
        const uint64_t bodyBytes = std::min<uint64_t>(
            remaining,
            static_cast<uint64_t>(concatenated_.brickCounts[body]) *
                static_cast<uint64_t>(Vixen::SVO::SerializedOctree::kBrickStrideBytes));
        if (bodyBytes == 0) continue;
        uploadLedger_.push_back(UploadLedgerEntry{
            body, kWholePayloadLevel, bodyBytes, 1u, true});
        std::cout << "[UploadLedger] body=" << body
                  << " level=whole-brick-payload bytes=" << bodyBytes
                  << " events=1" << std::endl;
        accounted += bodyBytes;
    }
    if (accounted < bytes) {
        uploadLedger_.push_back(UploadLedgerEntry{
            UINT32_MAX, kWholePayloadLevel, bytes - accounted, 1u, true});
        std::cout << "[UploadLedger] body=whole-buffer level=whole-brick-payload bytes="
                  << (bytes - accounted) << " events=1" << std::endl;
    }
}

void BodyOctreeSceneNode::PollBrickUploadCompletion() {
    VulkanDevice* device = GetDevice();
    if (!device) {
        return;
    }

    // Phase 1: brick data in flight. Once visible, stamp brickResident=1 into the CPU-side
    // config mirror and queue ITS upload — must not happen before the bricks land, or the
    // shader could observe brickResident=1 while still reading stale/zeroed brick bytes.
    if (pendingBrickUploadHandle_ != ResourceManagement::InvalidUploadHandle) {
        if (!device->IsUploadComplete(pendingBrickUploadHandle_)) {
            return;  // still in flight — check again next frame
        }
        pendingBrickUploadHandle_ = ResourceManagement::InvalidUploadHandle;
        brickPoolUploaded_ = true;

        // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4b: binding-5 (OCTREE_CONFIG_BUFFER)
        // holds whichever configs the live render actually samples. CreateShellBuffers
        // rewrites it to the shell-COMPACT configs (re-packed per-octree poolBrickBase) at
        // Compile whenever a shell cache was derived — re-uploading the SOURCE configs here
        // unconditionally would clobber that rewrite at exactly the mip->brick transition,
        // corrupting SDF addressing for octree index >=1 in any multi-octree pool.
        // StampAndSelectActiveConfigs (ResidencyDefault.h) stamps brickResident=1 into the
        // SAME view CreateShellBuffers last wrote and returns which vector to re-upload.
        const bool haveShellCache = !shellCache_[0].compact.configs.empty();
        std::vector<Vixen::SVO::OctreeConfig>* activeConfigs =
            Vixen::SVO::StampAndSelectActiveConfigs(concatenated_, shellCache_);
        // mipfix (2026-09-01): with wholesale admission DISABLED nothing ever moves
        // pendingMask off 0, so this stamp told StoredSdf.glsl:138 "no payload ready"
        // and the march sentineled to 1e9 — the recorded-history-long MipFallback red.
        // The classic path uploads payloads wholesale by construction: full mask.
        // (The other disabled-path stamps already do this — see the reset path that
        // writes WholesalePayloadMask() into every config.)
        const uint32_t readinessMask = wholesaleAdmissionEnabled_
            ? wholesaleAvailability_.pendingMask
            : Vixen::SVO::WholesalePayloadMask();
        for (auto& cfg : *activeConfigs) {
            cfg._tailPad[0] = readinessMask;
        }

        const VkDeviceSize configSize =
            static_cast<VkDeviceSize>(activeConfigs->size()) *
            static_cast<VkDeviceSize>(sizeof(Vixen::SVO::OctreeConfig));
        if (configSize > 0) {
            const auto cfgHandle = device->Upload(activeConfigs->data(), configSize, configBuffer_, 0);
            if (cfgHandle == ResourceManagement::InvalidUploadHandle) {
                NODE_LOG_ERROR("[BodyOctreeSceneNode] PollBrickUploadCompletion: config re-upload failed ("
                              + std::to_string(static_cast<uint64_t>(configSize)) + "B)");
            } else {
                device->FlushUploads();
                pendingConfigUploadHandle_ = cfgHandle;
            }
        }
        NODE_LOG_INFO("[BodyOctreeSceneNode] PollBrickUploadCompletion: brick pool visible on GPU ("
                      + std::string(haveShellCache ? "compact" : "source") + " configs re-uploaded)");
        return;  // one phase transition per call, matches the queue-then-poll-next-frame pattern
    }

    // Phase 2: config re-upload in flight (brickResident=1 becoming visible).
    if (pendingConfigUploadHandle_ != ResourceManagement::InvalidUploadHandle) {
        if (!device->IsUploadComplete(pendingConfigUploadHandle_)) {
            return;
        }
        pendingConfigUploadHandle_ = ResourceManagement::InvalidUploadHandle;
        Vixen::SVO::PublishWholesaleReady(wholesaleAvailability_);
        NODE_LOG_INFO("[BodyOctreeSceneNode] PollBrickUploadCompletion: brickResident config visible on GPU");
    }
}

void BodyOctreeSceneNode::DestroyOctreeBuffers() {
    if (!GetDevice()) return;
    VkDevice vkDevice = GetDevice()->device;

    auto destroy = [&](VkBuffer& buf, VkDeviceMemory& mem) {
        if (buf != VK_NULL_HANDLE) { vkDestroyBuffer(vkDevice, buf, nullptr); buf = VK_NULL_HANDLE; }
        if (mem != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, mem, nullptr);    mem = VK_NULL_HANDLE; }
    };

    destroy(nodesBuffer_,         nodesMemory_);
    destroy(bricksBuffer_,        bricksMemory_);
    destroy(materialsBuffer_,     materialsMemory_);
    destroy(configBuffer_,        configMemory_);
    destroy(sdfBuffer_,           sdfMemory_);         // Inc2 M3
    destroy(brickLookupBuffer_,   brickLookupMemory_); // Inc2 M3
    // Surface-Shell ESVO cache — both double-buffer slots.
    for (uint32_t i = 0; i < 2; ++i) {
        destroy(shellDataBuffer_[i],   shellDataMemory_[i]);
        destroy(shellLookupBuffer_[i], shellLookupMemory_[i]);
        destroy(proxyAabbBuffer_[i],   proxyAabbMemory_[i]);
        shellDataCapacity_[i]   = 0;
        shellLookupCapacity_[i] = 0;
        proxyAabbCapacity_[i]   = 0;
        proxyAabbCount_[i]      = 0;
    }
    destroy(mipPoolBuffer_,       mipPoolMemory_);      // Inc1 M3
    destroy(tierRefTableBuffer_,  tierRefTableMemory_); // Tiered-ESVO Inc2 M3
    destroy(occupancyGridBuffer_, occupancyGridMemory_); // Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13
}

// ============================================================================
// W-RTQUERY Slice A: per-brick-AABB TLAS for the ray_query traversal backend
// ============================================================================
// Hand-rolled (see BodyOctreeSceneNode.h's field comment for why this doesn't route
// through CashSystem::AccelerationStructureCacher -- that cacher builds ONE BLAS from
// a single VoxelAABBData blob + ONE single-instance TLAS; this needs one BLAS PER
// OCTREE (each with its own per-brick AABB set, read from concatenated_.brickGridLookup)
// plus one TLAS with one instance PER BODYOCTREESCENENODE INSTANCE, each carrying its
// own local-to-world transform and instanceCustomIndex = instance index -- shaped
// differently enough from the cacher's single-instance contract that reusing it would
// mean bending the cacher's API, not the AS build itself). Mirrors
// AccelerationStructureCacher::BuildBLAS/BuildTLAS's Vulkan call sequence verbatim
// (same struct fills, same synchronous submit-and-wait -- this runs at most once per
// scene-identity change, not per frame, so synchronous is fine here too).
void BodyOctreeSceneNode::EnsureRtQueryTlasBuilt(VulkanDevice* device) {
    // W-COMPOSED: mirror BuildRenderGraph.cpp's resolved scene predicate at the
    // downstream TLAS owner. The orbital structure admission selects composed
    // traversal by default; the explicit 0/1 override remains authoritative.
    const char* composedEnv = std::getenv("VIXEN_COMPOSED_TRAVERSAL");
    bool composedTraversalEnabled = envFlagEnabled("VIXEN_TIER_OBSERVABLE_STRUCTURE");
    if (composedEnv != nullptr) {
        while (*composedEnv != '\0' && std::isspace(static_cast<unsigned char>(*composedEnv))) ++composedEnv;
        if (*composedEnv == '0') composedTraversalEnabled = false;
        else if (*composedEnv == '1') composedTraversalEnabled = true;
    }
    if (!envFlagEnabled("VIXEN_RTQUERY_TRAVERSAL") && !composedTraversalEnabled) {
        return;  // both flags off: never build, never touch the RT function pointers
    }

    const RTXCapabilities& rtxCaps = device->GetRTXCapabilities();
    if (!rtxCaps.supported || !rtxCaps.rayQuery) {
        static bool warnedOnce = false;
        if (!warnedOnce) {
            warnedOnce = true;
            NODE_LOG_WARNING("[BodyOctreeSceneNode] VIXEN_RTQUERY_TRAVERSAL set but "
                              "RTXCapabilities.rayQuery unavailable on this device -- "
                              "staying on ESVO (RTQUERY_TLAS stays VK_NULL_HANDLE)");
        }
        return;
    }

    // Rebuild only when the (instance, octree) identity that produced the current
    // TLAS has actually changed -- cheap re-entrant no-op on every other Execute.
    if (rtQueryTlasBuilt_ &&
        rtQueryTlasBuiltForInstanceCount_ == instanceCount_ &&
        rtQueryTlasBuiltForOctreeCount_   == concatenated_.count) {
        return;
    }

    if (instances_.empty() || concatenated_.count == 0u) {
        return;  // nothing to build yet (pre-SetInstances / pre-EnsureOctreesBuilt)
    }

    NODE_LOG_INFO("[BodyOctreeSceneNode] VIXEN_RTQUERY_TRAVERSAL: (re)building per-brick-AABB "
                   "TLAS (" + std::to_string(concatenated_.count) + " octrees, " +
                   std::to_string(instances_.size()) + " instances)");

    DestroyRtQueryTlas();  // drop any stale BLAS/TLAS from a prior identity first

    VkDevice   vkDevice = device->device;
    const auto vkCreateAS = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(vkDevice, "vkCreateAccelerationStructureKHR"));
    const auto vkGetASBuildSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(vkDevice, "vkGetAccelerationStructureBuildSizesKHR"));
    const auto vkCmdBuildAS = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(vkDevice, "vkCmdBuildAccelerationStructuresKHR"));
    const auto vkGetASDeviceAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(vkDevice, "vkGetAccelerationStructureDeviceAddressKHR"));
    if (!vkCreateAS || !vkGetASBuildSizes || !vkCmdBuildAS || !vkGetASDeviceAddress) {
        NODE_LOG_WARNING("[BodyOctreeSceneNode] VIXEN_RTQUERY_TRAVERSAL: RT function pointers "
                          "failed to resolve despite RTXCapabilities.rayQuery -- staying on ESVO");
        return;
    }

    // One-time command pool + fence for the synchronous build submits below.
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = device->graphicsQueueIndex;
    VkCommandPool buildPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &buildPool) != VK_SUCCESS) {
        throw std::runtime_error("[BodyOctreeSceneNode] EnsureRtQueryTlasBuilt: vkCreateCommandPool failed");
    }

    auto submitAndWait = [&](VkCommandBuffer cmd) {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        std::lock_guard<std::mutex> submitLock(device->SubmitMutex(device->queue));
        if (vkQueueSubmit(device->queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            throw std::runtime_error("[BodyOctreeSceneNode] EnsureRtQueryTlasBuilt: vkQueueSubmit failed");
        }
        vkQueueWaitIdle(device->queue);
    };
    auto beginOneShot = [&]() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = buildPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(vkDevice, &allocInfo, &cmd);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);
        return cmd;
    };

    // --- Per-octree BLAS: one AABB per occupied brick, in octree-LOCAL [0,1]^3 space ---
    // gx/gy/gz below are BRICK-INDEX units (each in [0,bpa)), not voxel units -- so the
    // box convention is min = gx/bpa, max = (gx+1)/bpa, i.e. resolution == bricksPerAxis.
    // (ROUND-19 FIX: this was previously bpa*8 -- a voxel-unit scale applied to brick-unit
    // indices -- which packed all bpa^3 brick boxes into local [0, 1/8]^3, an 8x-shrunk cube
    // in the corner of the correct instance box. bpa*8 remains valid as BRICK_SIZE_SDF/voxel
    // grid resolution elsewhere (e.g. brickLocalToGrid in SceneBindings.glsl /
    // CoordinateTransforms.glsl), which indexes in VOXEL units -- do not conflate the two.)
    rtQueryBlas_.assign(concatenated_.count, RtQueryBlas{});
    const uint32_t* lookup = reinterpret_cast<const uint32_t*>(concatenated_.brickGridLookup.data());
    const size_t    lookupCount = concatenated_.brickGridLookup.size() / sizeof(uint32_t);

    for (uint32_t oi = 0; oi < concatenated_.count; ++oi) {
        const Vixen::SVO::OctreeConfig& cfg = concatenated_.configs[oi];
        const int bpa = cfg.bricksPerAxis;
        if (bpa <= 0) continue;  // e.g. FORMAT_BINARY octrees in a mixed scene: no lookup table
        const float resolution = static_cast<float>(bpa);  // gx/gy/gz are brick-index units in [0,bpa)
        const uint32_t base = cfg.brickLookupBase;

        // FIX 1: primitive index == flat grid index (gz*bpa*bpa + gy*bpa + gx), matching the
        // shader's decode convention -- so this pushes ONE AABB PER CELL (never compacts/skips),
        // with unallocated cells getting a degenerate inverted box that the RT core never
        // intersects (min > max on every axis). Memory cost is bpa^3*24B, trivial.
        std::vector<VkAabbPositionsKHR> aabbs;
        aabbs.reserve(static_cast<size_t>(bpa) * bpa * bpa);
        uint32_t occupiedCount = 0;
        for (int gz = 0; gz < bpa; ++gz) {
            for (int gy = 0; gy < bpa; ++gy) {
                for (int gx = 0; gx < bpa; ++gx) {
                    const uint32_t flatIdx = static_cast<uint32_t>(gz * bpa * bpa + gy * bpa + gx);
                    const size_t idx = static_cast<size_t>(base) + flatIdx;
                    VkAabbPositionsKHR box{};
                    if (idx >= lookupCount || Vixen::SVO::isBrickUnallocated(lookup[idx])) {
                        box.minX = box.minY = box.minZ = 1e30f;
                        box.maxX = box.maxY = box.maxZ = -1e30f;
                    } else {
                        box.minX = static_cast<float>(gx)     / resolution;
                        box.minY = static_cast<float>(gy)     / resolution;
                        box.minZ = static_cast<float>(gz)     / resolution;
                        box.maxX = static_cast<float>(gx + 1) / resolution;
                        box.maxY = static_cast<float>(gy + 1) / resolution;
                        box.maxZ = static_cast<float>(gz + 1) / resolution;
                        ++occupiedCount;
                    }
                    aabbs.push_back(box);
                }
            }
        }
        // ROUND-17 probe: bpa + occupiedBrickCount per octree -- discriminates the
        // remaining round-16 question directly. If octree 3 (the far body) has very
        // few occupied bricks (e.g. single digits) then ~4 candidates/frame for its
        // 71 screen pixels is the GEOMETRICALLY CORRECT candidate count for that
        // BLAS (few boxes exist to hit at all) rather than a traversal/gate bug --
        // see docs/plans/2026-08-04-wavefront-recipe-shading.md round-17 order.
        std::cout << "[RtBlasOccupancy] octree=" << oi << " bpa=" << bpa
                  << " occupiedBricks=" << occupiedCount
                  << " totalCells=" << (static_cast<uint64_t>(bpa) * bpa * bpa) << std::endl;

        if (occupiedCount == 0) continue;  // fully-unallocated octree (degenerate/empty body)

        RtQueryBlas& blas = rtQueryBlas_[oi];
        CreateDeviceAddressBuffer(device,
            static_cast<VkDeviceSize>(aabbs.size() * sizeof(VkAabbPositionsKHR)),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            blas.aabbBuffer, blas.aabbMemory, "RtQuery BLAS AABBs");
        void* mapped = nullptr;
        vkMapMemory(vkDevice, blas.aabbMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
        std::memcpy(mapped, aabbs.data(), aabbs.size() * sizeof(VkAabbPositionsKHR));
        vkUnmapMemory(vkDevice, blas.aabbMemory);

        VkAccelerationStructureGeometryAabbsDataKHR aabbsData{};
        aabbsData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        aabbsData.data.deviceAddress = GetBufferDeviceAddress(device, blas.aabbBuffer);
        aabbsData.stride = sizeof(VkAabbPositionsKHR);

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.aabbs = aabbsData;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        uint32_t primitiveCount = static_cast<uint32_t>(aabbs.size());
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetASBuildSizes(vkDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                           &buildInfo, &primitiveCount, &sizeInfo);

        CreateDeviceAddressBuffer(device, sizeInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, blas.asBuffer, blas.asMemory, "RtQuery BLAS");

        VkBuffer       scratchBuf = VK_NULL_HANDLE;
        VkDeviceMemory scratchMem = VK_NULL_HANDLE;
        CreateDeviceAddressBuffer(device, sizeInfo.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            scratchBuf, scratchMem, "RtQuery BLAS scratch");

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = blas.asBuffer;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        if (vkCreateAS(vkDevice, &createInfo, nullptr, &blas.handle) != VK_SUCCESS) {
            throw std::runtime_error("[BodyOctreeSceneNode] EnsureRtQueryTlasBuilt: vkCreateAccelerationStructureKHR (BLAS) failed");
        }

        buildInfo.dstAccelerationStructure = blas.handle;
        buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(device, scratchBuf);

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = primitiveCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        VkCommandBuffer cmd = beginOneShot();
        vkCmdBuildAS(cmd, 1, &buildInfo, &pRangeInfo);
        vkEndCommandBuffer(cmd);
        submitAndWait(cmd);
        vkFreeCommandBuffers(vkDevice, buildPool, 1, &cmd);

        vkDestroyBuffer(vkDevice, scratchBuf, nullptr);
        vkFreeMemory(vkDevice, scratchMem, nullptr);

        VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addrInfo.accelerationStructure = blas.handle;
        blas.deviceAddress = vkGetASDeviceAddress(vkDevice, &addrInfo);
    }

    // --- TLAS: one instance per BodyOctreeSceneNode instance ---
    // Transform = local-to-world for THIS instance's placement: TraceWorld.glsl computes
    // instOrigin=(rayOrigin-worldPos)/renderScale then localRayOrigin=worldToLocal*instOrigin,
    // i.e. world->local = octreeConfig.worldToLocal * scale(1/renderScale) * translate(-worldPos).
    // The TLAS wants the INVERSE (local->world), which is exactly the octree's own
    // localToWorld already composed with this instance's placement:
    //   local->world = translate(worldPos) * scale(renderScale) * octreeConfig.localToWorld.
    std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
    vkInstances.reserve(instances_.size());
    for (uint32_t ii = 0; ii < static_cast<uint32_t>(instances_.size()); ++ii) {
        const Vixen::SVO::BodyInstanceGpu& inst = instances_[ii];
        if (inst.providerKind != 0u) continue;  // PROVIDER_STORED only -- procedural bodies have no octree/BLAS
        const uint32_t oi = inst.octreeIndex;
        if (oi >= rtQueryBlas_.size() || rtQueryBlas_[oi].handle == VK_NULL_HANDLE) {
            continue;  // no brick occupied this octree (or index out of range) -- nothing to instance
        }
        const glm::mat4 localToWorld =
            glm::translate(glm::mat4(1.0f), glm::vec3(inst.worldPos[0], inst.worldPos[1], inst.worldPos[2])) *
            glm::scale(glm::mat4(1.0f), glm::vec3(inst.renderScale)) *
            concatenated_.configs[oi].localToWorld;

        VkAccelerationStructureInstanceKHR vkInst{};
        // VkTransformMatrixKHR is row-major 3x4; glm::mat4 is column-major -- transpose via
        // direct [row][col] = mat[col][row] indexing (same convention TLASInstanceManager.h's
        // glm::mat3x4 comment documents for this engine's other TLAS instance path).
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                vkInst.transform.matrix[row][col] = localToWorld[col][row];
            }
        }
        // Hoist redesign: instanceCustomIndex is the INSTANCE index (ii, position in
        // instances_/bodyInstances[]), NOT the octree index -- the shader derives the
        // octree as configs[instances[ci].octreeIndex], mirroring TraceWorld.glsl's own
        // instance loop (`uint oi = inst.octreeIndex;`). This lets the hoisted RT search
        // recover the SAME BodyInstance (worldPos/renderScale/octreeIndex) TraceWorld's
        // per-instance loop would have used for this candidate, instead of only the
        // octree it happens to share with potentially multiple instances.
        vkInst.instanceCustomIndex = ii;
        vkInst.mask = 0xFF;
        vkInst.instanceShaderBindingTableRecordOffset = 0;
        vkInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        vkInst.accelerationStructureReference = rtQueryBlas_[oi].deviceAddress;
        vkInstances.push_back(vkInst);

        // ROUND-18 STEP 1: dump this instance's world-space AABB, composed through the
        // EXACT matrix bytes just written into vkInst.transform (row-major 3x4), not a
        // parallel derivation -- reconstruct a glm::mat4 from those bytes and transform
        // the BLAS-local [0,1]^3 cube's 8 corners.
        {
            glm::mat4 m(1.0f);
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 4; ++col)
                    m[col][row] = vkInst.transform.matrix[row][col];
            glm::vec3 wmin(1e30f), wmax(-1e30f);
            for (int c = 0; c < 8; ++c) {
                glm::vec3 local((c & 1) ? 1.0f : 0.0f, (c & 2) ? 1.0f : 0.0f, (c & 4) ? 1.0f : 0.0f);
                glm::vec3 w = glm::vec3(m * glm::vec4(local, 1.0f));
                wmin = glm::min(wmin, w);
                wmax = glm::max(wmax, w);
            }
            std::cout << "[RtTlasInst] i=" << ii << " octree=" << oi
                      << " worldMin=(" << wmin.x << "," << wmin.y << "," << wmin.z << ")"
                      << " worldMax=(" << wmax.x << "," << wmax.y << "," << wmax.z << ")"
                      << std::endl;
        }
    }

    // ROUND-16 candidate-supply probe (proven channel, plan-mandated): report per-octree
    // BLAS presence + final TLAS instance count so a boot's stdout directly answers
    // "does the scene-scoped 4th octree (VIXEN_BRICKMAP_SCENE far body, octreeIndex=3)
    // get a BLAS and a TLAS instance, or does the far body have no acceleration
    // structure at all." See docs/plans/2026-08-04-wavefront-recipe-shading.md,
    // round-16 order (c) for why.
    {
        std::string aabbsPerOctree = "[";
        for (uint32_t oi = 0; oi < concatenated_.count; ++oi) {
            if (oi) aabbsPerOctree += ",";
            aabbsPerOctree += (rtQueryBlas_[oi].handle != VK_NULL_HANDLE) ? "BLAS" : "none";
        }
        aabbsPerOctree += "]";
        std::cout << "[RtTlas] instances=" << vkInstances.size()
                  << " octreeCount=" << concatenated_.count
                  << " aabbsPerOctree=" << aabbsPerOctree << std::endl;
    }

    if (vkInstances.empty()) {
        vkDestroyCommandPool(vkDevice, buildPool, nullptr);
        NODE_LOG_WARNING("[BodyOctreeSceneNode] VIXEN_RTQUERY_TRAVERSAL: no Stored-SDF instances "
                          "with occupied bricks -- TLAS not built, RTQUERY_TLAS stays VK_NULL_HANDLE");
        return;
    }

    CreateDeviceAddressBuffer(device,
        static_cast<VkDeviceSize>(vkInstances.size() * sizeof(VkAccelerationStructureInstanceKHR)),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        rtQueryInstanceBuffer_, rtQueryInstanceMemory_, "RtQuery TLAS instances");
    void* mappedInst = nullptr;
    vkMapMemory(vkDevice, rtQueryInstanceMemory_, 0, VK_WHOLE_SIZE, 0, &mappedInst);
    std::memcpy(mappedInst, vkInstances.data(), vkInstances.size() * sizeof(VkAccelerationStructureInstanceKHR));
    vkUnmapMemory(vkDevice, rtQueryInstanceMemory_);

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.data.deviceAddress = GetBufferDeviceAddress(device, rtQueryInstanceBuffer_);

    VkAccelerationStructureGeometryKHR tlasGeometry{};
    tlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlasGeometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo{};
    tlasBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tlasBuildInfo.geometryCount = 1;
    tlasBuildInfo.pGeometries = &tlasGeometry;

    uint32_t tlasInstanceCount = static_cast<uint32_t>(vkInstances.size());
    VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo{};
    tlasSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetASBuildSizes(vkDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                       &tlasBuildInfo, &tlasInstanceCount, &tlasSizeInfo);

    CreateDeviceAddressBuffer(device, tlasSizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, rtQueryTlasBuffer_, rtQueryTlasMemory_, "RtQuery TLAS");
    CreateDeviceAddressBuffer(device, tlasSizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        rtQueryScratchBuffer_, rtQueryScratchMemory_, "RtQuery TLAS scratch");

    VkAccelerationStructureCreateInfoKHR tlasCreateInfo{};
    tlasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    tlasCreateInfo.buffer = rtQueryTlasBuffer_;
    tlasCreateInfo.size = tlasSizeInfo.accelerationStructureSize;
    tlasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (vkCreateAS(vkDevice, &tlasCreateInfo, nullptr, &rtQueryTlas_) != VK_SUCCESS) {
        vkDestroyCommandPool(vkDevice, buildPool, nullptr);
        throw std::runtime_error("[BodyOctreeSceneNode] EnsureRtQueryTlasBuilt: vkCreateAccelerationStructureKHR (TLAS) failed");
    }

    tlasBuildInfo.dstAccelerationStructure = rtQueryTlas_;
    tlasBuildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(device, rtQueryScratchBuffer_);

    VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo{};
    tlasRangeInfo.primitiveCount = tlasInstanceCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pTlasRangeInfo = &tlasRangeInfo;

    VkCommandBuffer tlasCmd = beginOneShot();
    vkCmdBuildAS(tlasCmd, 1, &tlasBuildInfo, &pTlasRangeInfo);
    vkEndCommandBuffer(tlasCmd);
    submitAndWait(tlasCmd);
    vkFreeCommandBuffers(vkDevice, buildPool, 1, &tlasCmd);
    vkDestroyCommandPool(vkDevice, buildPool, nullptr);

    rtQueryTlasBuilt_ = true;
    rtQueryTlasBuiltForInstanceCount_ = instanceCount_;
    rtQueryTlasBuiltForOctreeCount_   = concatenated_.count;

    NODE_LOG_INFO("[BodyOctreeSceneNode] VIXEN_RTQUERY_TRAVERSAL: TLAS built (" +
                   std::to_string(vkInstances.size()) + " instances over " +
                   std::to_string(rtQueryBlas_.size()) + " BLAS)");
}

void BodyOctreeSceneNode::DestroyRtQueryTlas() {
    if (!GetDevice()) {
        rtQueryBlas_.clear();
        rtQueryTlas_ = VK_NULL_HANDLE;
        rtQueryTlasBuilt_ = false;
        rtQueryTlasBuiltForInstanceCount_ = -1;
        rtQueryTlasBuiltForOctreeCount_ = 0;
        return;
    }
    VkDevice vkDevice = GetDevice()->device;
    const auto vkDestroyAS = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(vkDevice, "vkDestroyAccelerationStructureKHR"));

    for (RtQueryBlas& blas : rtQueryBlas_) {
        if (vkDestroyAS && blas.handle != VK_NULL_HANDLE) vkDestroyAS(vkDevice, blas.handle, nullptr);
        if (blas.asBuffer != VK_NULL_HANDLE)   { vkDestroyBuffer(vkDevice, blas.asBuffer, nullptr);   }
        if (blas.asMemory != VK_NULL_HANDLE)   { vkFreeMemory(vkDevice, blas.asMemory, nullptr);      }
        if (blas.aabbBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(vkDevice, blas.aabbBuffer, nullptr); }
        if (blas.aabbMemory != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, blas.aabbMemory, nullptr);    }
    }
    rtQueryBlas_.clear();

    if (vkDestroyAS && rtQueryTlas_ != VK_NULL_HANDLE) vkDestroyAS(vkDevice, rtQueryTlas_, nullptr);
    rtQueryTlas_ = VK_NULL_HANDLE;
    auto destroy = [&](VkBuffer& buf, VkDeviceMemory& mem) {
        if (buf != VK_NULL_HANDLE) { vkDestroyBuffer(vkDevice, buf, nullptr); buf = VK_NULL_HANDLE; }
        if (mem != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, mem, nullptr);    mem = VK_NULL_HANDLE; }
    };
    destroy(rtQueryTlasBuffer_,    rtQueryTlasMemory_);
    destroy(rtQueryScratchBuffer_, rtQueryScratchMemory_);
    destroy(rtQueryInstanceBuffer_, rtQueryInstanceMemory_);

    rtQueryTlasBuilt_ = false;
    rtQueryTlasBuiltForInstanceCount_ = -1;
    rtQueryTlasBuiltForOctreeCount_ = 0;
}

void BodyOctreeSceneNode::DestroyBuffers() {
    DestroyOctreeBuffers();
    DestroyRtQueryTlas();  // W-RTQUERY Slice A: no-op when never built (flag off / no capability)

    // FR-7: destroy the instance ring via PerFrameResources (mirrors DynamicInstanceBufferNode).
    perFrame_.Cleanup();
    instanceRingCapacity_ = 0;

    NODE_LOG_INFO("[BodyOctreeSceneNode] All buffers destroyed");
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
// Required after the M4 app-TU split dropped VulkanGraphApplication::RegisterNodeTypes (which used to
// hand-register this node); BuildRenderGraph now AddNode<BodyOctreeSceneNodeType>s it, so it must be in
// the RegisterAllNodes manifest like every other node.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::BodyOctreeSceneNodeType);
