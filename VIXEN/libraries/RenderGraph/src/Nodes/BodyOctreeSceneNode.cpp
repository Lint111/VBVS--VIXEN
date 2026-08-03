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
#include <cstdlib>   // std::getenv
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

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
    if (std::getenv("VIXEN_STORED_SDF_DEMO")) {
        NODE_LOG_INFO("[BodyOctreeSceneNode] VIXEN_STORED_SDF_DEMO: baking 3 Stored-SDF octrees");

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
        constexpr SdfKind kSdfKinds[kKindCount] = {
            { Vixen::SVO::RECIPE_SPHERE,           0.0f, 0.0f   },  // kind 0: smooth
            { Vixen::SVO::RECIPE_DISPLACED_SPHERE, 2.7f, 0.375f },  // kind 1: displaced
            { Vixen::SVO::RECIPE_SPHERE,           0.0f, 0.0f   },  // kind 2: smooth
        };

        std::vector<Vixen::SVO::SdfBodyOctree> sdfOctrees;
        sdfOctrees.reserve(kKindCount);

        for (uint32_t k = 0; k < kKindCount; ++k) {
            Vixen::SVO::SdfBakeResult baked;
            // ponytail: recipe injection for octree 0 only; analytic path unchanged for k>0
            if (k == 0 && !bakeRecipe_.empty()) {
                NODE_LOG_INFO("[BodyOctreeSceneNode] octree 0: baking via recipe ("
                              + std::to_string(bakeRecipe_.size()) + " instructions)");
                baked = Vixen::SVO::BakeRecipeInstructionsToSdfWorld(
                    bakeRecipe_.data(), static_cast<uint32_t>(bakeRecipe_.size()),
                    center, kSdfN, kSdfBand, kSdfBrickDepth);
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
        concatenated_ = Vixen::SVO::ConcatenateSdfWithMips(sdfPtrs);
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

    CreateHostBuffer(device, materialsSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        concatenated_.materials.empty() ? nullptr : concatenated_.materials.data(),
        materialsBuffer_, materialsMemory_, "octree materials SSBO");

    // Inc1 M3 Task 7: stamp brickResident into every octree's config so the shader's
    // leaf-hit existence check can distinguish "allocated but not populated" from
    // "fully uploaded" — hasBrick()/contourPointer alone cannot (M2's descriptor
    // pointer stays valid regardless of residency). Config bytes are re-uploaded
    // below this same Compile/Rematerialize call, so this always reflects the
    // brickPoolUploaded_ value just computed above.
    for (auto& cfg : concatenated_.configs) {
        Vixen::SVO::setBrickResident(cfg, brickPoolUploaded_);
    }

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
    const VkDeviceSize sdfSize =
        std::max<VkDeviceSize>(concatenated_.channelPool.size(), 1);
    const VkDeviceSize brickLookupSize =
        std::max<VkDeviceSize>(concatenated_.brickGridLookup.size(), 1);

    CreateHostBuffer(device, sdfSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        concatenated_.channelPool.empty() ? nullptr : concatenated_.channelPool.data(),
        sdfBuffer_, sdfMemory_, "channel pool SSBO");

    CreateHostBuffer(device, brickLookupSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        concatenated_.brickGridLookup.empty() ? nullptr : concatenated_.brickGridLookup.data(),
        brickLookupBuffer_, brickLookupMemory_, "brick-grid lookup SSBO");

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
    const VkDeviceSize tierRefTableSize =
        std::max<VkDeviceSize>(concatenated_.tierRefTable.size() * sizeof(Vixen::SVO::TierRef), 1);
    CreateHostBuffer(device, tierRefTableSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        concatenated_.tierRefTable.empty() ? nullptr : concatenated_.tierRefTable.data(),
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
    ensure(proxyAabbBuffer_[slot], proxyAabbMemory_[slot], proxyAabbCapacity_[slot],
           proxySize, flatProxies.empty() ? nullptr : flatProxies.data(),
           flatProxies.size() * sizeof(Vixen::SVO::ShellProxyAabb), "shell proxy AABB SSBO");
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

    // Inc1 M4 Task 6b: first-ever queue is "boot", every later one (a residency toggle) is
    // "steady-state". brickPoolUploaded_ hasn't flipped true yet at this point (that happens
    // in PollBrickUploadCompletion once the GPU-side copy lands), so bootBytesUploaded_==0
    // is exactly "boot upload never queued".
    if (bootBytesUploaded_ == 0) {
        bootBytesUploaded_ = static_cast<uint64_t>(size);
    } else {
        steadyStateBytesUploaded_ += static_cast<uint64_t>(size);
    }

    NODE_LOG_INFO("[BodyOctreeSceneNode] UploadBrickPool: queued " +
                  std::to_string(static_cast<uint64_t>(size)) + "B via BatchedUploader (async)");
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
    }
    destroy(mipPoolBuffer_,       mipPoolMemory_);      // Inc1 M3
    destroy(tierRefTableBuffer_,  tierRefTableMemory_); // Tiered-ESVO Inc2 M3
    destroy(occupancyGridBuffer_, occupancyGridMemory_); // Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13
}

void BodyOctreeSceneNode::DestroyBuffers() {
    DestroyOctreeBuffers();

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
