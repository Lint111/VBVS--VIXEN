#include "Nodes/BodyOctreeSceneNode.h"
#include "Core/NodeRegistration.h"  // M3: VIXEN_REGISTER_NODE self-registration
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "VulkanDevice.h"

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
    // Do NOT call MarkNeedsRecompile — the per-tick recompile cascade was the race root cause.
    instances_     = std::move(instances);
    instanceCount_ = static_cast<int32_t>(instances_.size());
    NODE_LOG_INFO("[BodyOctreeSceneNode] SetInstances: " +
                  std::to_string(instanceCount_) + " instances staged for next Execute");
}

void BodyOctreeSceneNode::SetBakeRecipe(std::vector<Vixen::SVO::Recipe::SdfInstruction> prog) {
    bakeRecipe_  = std::move(prog);
    recipeDirty_ = true;   // P2.3: if already compiled, ExecuteImpl re-materializes on the next frame;
                           //       if pre-Compile, CompileImpl bakes fresh and clears this.
    NODE_LOG_INFO("[BodyOctreeSceneNode] SetBakeRecipe: " +
                  std::to_string(bakeRecipe_.size()) + " instructions — octree 0 will use recipe bake");
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

    // Per-frame ring index from FrameSyncNode (clamp via modulo for safety).
    const uint32_t frameIndex = ctx.In(BodyOctreeSceneNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;

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
    // Re-emit the count (it may change each frame if SetInstances was called).
    ctx.Out(BodyOctreeSceneNodeConfig::INSTANCE_COUNT, instanceCount_);

    // Re-emit the octree slots with the freshly-created handles after a re-materialize,
    // so GetOutput()->GetHandle() (and any per-frame descriptor re-bind) sees the new buffers.
    if (octreeRepublished) {
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER,       nodesBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_BRICKS_BUFFER,      bricksBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_MATERIALS_BUFFER,   materialsBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,      configBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_SDF_BUFFER,         sdfBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_BRICKLOOKUP_BUFFER, brickLookupBuffer_);
    }
}

void BodyOctreeSceneNode::CleanupImpl(TypedCleanupContext& ctx) {
    // CRITICAL: recompile must NOT free in-flight GPU objects (WSL/Dozen VM-panic trap).
    // Persist across recompile; release only on final application teardown.
    if (ctx.reason != CleanupReason::FinalTeardown) {
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

        concatenated_ = Vixen::SVO::ConcatenateSdf(sdfPtrs);
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

    CreateHostBuffer(device, bricksSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        concatenated_.bricks.empty() ? nullptr : concatenated_.bricks.data(),
        bricksBuffer_, bricksMemory_, "octree bricks SSBO");

    CreateHostBuffer(device, materialsSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        concatenated_.materials.empty() ? nullptr : concatenated_.materials.data(),
        materialsBuffer_, materialsMemory_, "octree materials SSBO");

    // Config UBO: 3 x 432-byte OctreeConfig (std140, sizeof=432), uploaded contiguously. Always
    // upload the full kMaxOctrees array so the slot covers every selectable index.
    const VkDeviceSize configSize =
        static_cast<VkDeviceSize>(sizeof(Vixen::SVO::OctreeConfig)) *
        Vixen::SVO::ConcatenatedOctrees::kMaxOctrees;
    CreateHostBuffer(device, configSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        concatenated_.configs.data(),
        configBuffer_, configMemory_, "octree config UBO");

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

    NODE_LOG_INFO("[BodyOctreeSceneNode] Created octree buffers (nodes=" +
                  std::to_string(static_cast<uint64_t>(nodesSize)) + "B, bricks=" +
                  std::to_string(static_cast<uint64_t>(bricksSize)) + "B, materials=" +
                  std::to_string(static_cast<uint64_t>(materialsSize)) + "B, config=" +
                  std::to_string(static_cast<uint64_t>(configSize)) + "B, channelPool=" +
                  std::to_string(static_cast<uint64_t>(sdfSize)) + "B, brickLookup=" +
                  std::to_string(static_cast<uint64_t>(brickLookupSize)) + "B)");
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
    NODE_LOG_INFO("[BodyOctreeSceneNode] Rematerialize: re-baking octree 0 from edited recipe");

    // Rare, explicit edit path — safe to stall (mirrors the ring-grow vkDeviceWaitIdle).
    // Guarantees no in-flight command buffer still references the octree buffers we free.
    vkDeviceWaitIdle(device->device);

    octreesBuilt_ = false;     // force EnsureOctreesBuilt to re-bake + re-concatenate all 3 octrees
    EnsureOctreesBuilt();      // octree 0 uses the new bakeRecipe_; octrees 1/2 unchanged

    DestroyOctreeBuffers();    // ring is NOT touched
    CreateOctreeBuffers(device);
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
