#include "Nodes/BodyOctreeSceneNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"

#include <algorithm>
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
    instances_      = std::move(instances);
    instancesDirty_ = true;
    // Request a recompile so the instance SSBO is re-uploaded on the next compile.
    // (The cached octrees are untouched — only the instance buffer is rebuilt.)
    MarkNeedsRecompile();
    NODE_LOG_INFO("[BodyOctreeSceneNode] SetInstances: " +
                  std::to_string(instances_.size()) + " instances (recompile requested)");
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

    // 3b) Instance SSBO — (re)build when dirty (grows allocation if needed).
    CreateOrUpdateInstanceBuffer(devicePtr);

    // 4) Publish outputs.
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER,     nodesBuffer_);
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_BRICKS_BUFFER,    bricksBuffer_);
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_MATERIALS_BUFFER, materialsBuffer_);
    ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,    configBuffer_);
    ctx.Out(BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,         instanceBuffer_);
    ctx.Out(BodyOctreeSceneNodeConfig::INSTANCE_COUNT,          instanceCount_);

    NODE_LOG_INFO("[BodyOctreeSceneNode] Outputs published (octrees=" +
                  std::to_string(concatenated_.count) + ", instances=" +
                  std::to_string(instanceCount_) + ")");
}

void BodyOctreeSceneNode::ExecuteImpl(TypedExecuteContext& /*ctx*/) {
    // Static, host-coherent buffers — filled at compile time. Nothing per-frame.
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

    // Build one owning shell octree per kind. ShellOctree is move-only (unique_ptr
    // members) and OWNS its world/registry/octree, so the cached vector keeps them
    // alive for the node's lifetime — required because Serialize() reads the world.
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

    // Config UBO: 3 x 256-byte OctreeConfig (std140), uploaded contiguously. Always
    // upload the full kMaxOctrees array so the slot covers every selectable index.
    const VkDeviceSize configSize =
        static_cast<VkDeviceSize>(sizeof(Vixen::SVO::OctreeConfig)) *
        Vixen::SVO::ConcatenatedOctrees::kMaxOctrees;
    CreateHostBuffer(device, configSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        concatenated_.configs.data(),
        configBuffer_, configMemory_, "octree config UBO");

    NODE_LOG_INFO("[BodyOctreeSceneNode] Created octree buffers (nodes=" +
                  std::to_string(static_cast<uint64_t>(nodesSize)) + "B, bricks=" +
                  std::to_string(static_cast<uint64_t>(bricksSize)) + "B, materials=" +
                  std::to_string(static_cast<uint64_t>(materialsSize)) + "B, config=" +
                  std::to_string(static_cast<uint64_t>(configSize)) + "B)");
}

void BodyOctreeSceneNode::CreateOrUpdateInstanceBuffer(VulkanDevice* device) {
    // Already current and allocated — nothing to do.
    if (!instancesDirty_ && instanceBuffer_ != VK_NULL_HANDLE) {
        return;
    }

    // Pack the instances. If empty, still produce a valid 1-element placeholder so
    // the output slot is always a valid VkBuffer (the count reports the real value).
    std::vector<Vixen::SVO::BodyInstanceGpu> toPack = instances_;
    instanceCount_ = static_cast<uint32_t>(instances_.size());
    if (toPack.empty()) {
        toPack.push_back(Vixen::SVO::BodyInstanceGpu{});  // zeroed placeholder record
    }
    const std::vector<uint8_t> packed = Vixen::SVO::PackInstances(toPack);
    const VkDeviceSize neededSize = std::max<VkDeviceSize>(packed.size(), 1);

    VkDevice vkDevice = device->device;

    // Reuse the existing allocation if it is large enough (host-coherent re-upload).
    // This keeps the buffer persistent across recompile in the common case; we only
    // grow (destroy + recreate) when the instance list outgrows the allocation.
    if (instanceBuffer_ != VK_NULL_HANDLE && neededSize <= instanceCapacity_) {
        void* mapped = nullptr;
        if (vkMapMemory(vkDevice, instanceMemory_, 0, neededSize, 0, &mapped) != VK_SUCCESS) {
            throw std::runtime_error("[BodyOctreeSceneNode] vkMapMemory failed for instance SSBO re-upload");
        }
        std::memcpy(mapped, packed.data(), static_cast<size_t>(packed.size()));
        vkUnmapMemory(vkDevice, instanceMemory_);
        instancesDirty_ = false;
        NODE_LOG_INFO("[BodyOctreeSceneNode] Re-uploaded instance SSBO in place (" +
                      std::to_string(instanceCount_) + " instances)");
        return;
    }

    // (Re)create at the needed size. Destroying here is safe: this runs in CompileImpl,
    // never in the recompile cleanup path. Grow-only — small allocations are rare.
    if (instanceBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkDevice, instanceBuffer_, nullptr);
        instanceBuffer_ = VK_NULL_HANDLE;
    }
    if (instanceMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, instanceMemory_, nullptr);
        instanceMemory_ = VK_NULL_HANDLE;
    }

    CreateHostBuffer(device, neededSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        packed.data(),
        instanceBuffer_, instanceMemory_, "body instance SSBO");
    instanceCapacity_ = neededSize;
    instancesDirty_   = false;

    NODE_LOG_INFO("[BodyOctreeSceneNode] Created instance SSBO (" +
                  std::to_string(instanceCount_) + " instances, " +
                  std::to_string(static_cast<uint64_t>(neededSize)) + "B)");
}

void BodyOctreeSceneNode::DestroyBuffers() {
    if (!GetDevice()) return;
    VkDevice vkDevice = GetDevice()->device;

    auto destroy = [&](VkBuffer& buf, VkDeviceMemory& mem) {
        if (buf != VK_NULL_HANDLE) { vkDestroyBuffer(vkDevice, buf, nullptr); buf = VK_NULL_HANDLE; }
        if (mem != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, mem, nullptr);    mem = VK_NULL_HANDLE; }
    };

    destroy(nodesBuffer_,     nodesMemory_);
    destroy(bricksBuffer_,    bricksMemory_);
    destroy(materialsBuffer_, materialsMemory_);
    destroy(configBuffer_,    configMemory_);
    destroy(instanceBuffer_,  instanceMemory_);
    instanceCapacity_ = 0;

    NODE_LOG_INFO("[BodyOctreeSceneNode] All buffers destroyed");
}

} // namespace Vixen::RenderGraph
