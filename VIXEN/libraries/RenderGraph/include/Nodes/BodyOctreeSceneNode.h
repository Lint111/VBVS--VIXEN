#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"

#include "ShellOctree.h"      // Vixen::SVO::ShellOctree, BuildShellOctree
#include "ShellOctreeGpu.h"   // Vixen::SVO::{Concatenate, ConcatenatedOctrees, BodyInstanceGpu, PackInstances}

#include <cstdint>
#include <memory>
#include <vector>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for BodyOctreeSceneNode (SP2 Task 5b).
 */
class BodyOctreeSceneNodeType : public TypedNodeType<BodyOctreeSceneNodeConfig> {
public:
    BodyOctreeSceneNodeType(const std::string& typeName = "BodyOctreeScene")
        : TypedNodeType<BodyOctreeSceneNodeConfig>(typeName) {}
    virtual ~BodyOctreeSceneNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Uploads the (<=3) per-kind sparse SHELL octrees + a per-body instance SSBO
 *        for the SP2 LOD body renderer (instanced multi-octree ray march).
 *
 * On Compile the node:
 *   1. builds the <=3 per-kind ShellOctrees once (cached as members; they OWN their
 *      world + registry + octree, so they stay alive for the node's lifetime),
 *   2. serializes + concatenates them via Vixen::SVO::Concatenate (cached bytes),
 *   3. creates 4 GPU octree buffers (nodes / bricks / materials SSBOs + config UBO),
 *      host-visible/host-coherent, filled at create time,
 *   4. allocates a RING of kRingSize (= MAX_FRAMES_IN_FLIGHT) instance SSBOs (once,
 *      persistent across recompile; grown with vkDeviceWaitIdle on capacity overflow),
 *   5. publishes the four octree slots plus INSTANCE_BUFFER (ring[0] as a compile-time
 *      placeholder) + INSTANCE_COUNT.
 *
 * Instance seam: the host calls SetInstances(...) which ONLY stashes the vector and
 * sets a dirty flag — it NO LONGER triggers recompile. ExecuteImpl uploads the current
 * instances into ring[frameIndex % kRingSize] every frame and re-emits that buffer on
 * INSTANCE_BUFFER so the descriptor binds the freshly-written, not-in-flight slot.
 *
 * Lifecycle (CRITICAL — see the WSL/Dozen recompile-frees-in-flight VM-panic trap):
 *   - Octree buffers: created once, persist across recompile, destroyed at FinalTeardown.
 *   - Instance ring: allocated once, persists across recompile; grown (behind
 *     vkDeviceWaitIdle) only on capacity overflow; destroyed at FinalTeardown.
 *   - No GPU resource is ever freed on the per-tick / per-recompile path.
 *
 * Mirrors DynamicInstanceBufferNode's FR-7 ring pattern exactly.
 */
class BodyOctreeSceneNode : public TypedNode<BodyOctreeSceneNodeConfig> {
public:
    using Base = TypedNode<BodyOctreeSceneNodeConfig>;

    BodyOctreeSceneNode(const std::string& instanceName, NodeType* nodeType);
    ~BodyOctreeSceneNode() override = default;

    /**
     * @brief Push the current per-body instance list (host -> node seam).
     *
     * Stashes the vector and marks a dirty flag. Does NOT trigger recompile.
     * ExecuteImpl uploads the current data into the current frame's ring buffer
     * on every frame — the per-tick recompile cascade is gone entirely.
     */
    void SetInstances(std::vector<Vixen::SVO::BodyInstanceGpu> instances);

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // --- Octree build / serialization (octrees + concatenated bytes cached) ---
    void EnsureOctreesBuilt();                                        // build + concatenate once
    void CreateOctreeBuffers(Vixen::Vulkan::Resources::VulkanDevice* device);  // 4 octree buffers
    void EnsureRingAllocated(Vixen::Vulkan::Resources::VulkanDevice* device,
                             VkDeviceSize neededCapacity);            // allocate/grow instance ring
    void DestroyBuffers();

    // Build constants (one shell per kind; depth/material chosen here).
    static constexpr int      kShellDepth = 6;   // 2^6 = 64 cells/axis
    static constexpr uint32_t kKindCount  = 3;

    // Ring size = frames-in-flight (same as DynamicInstanceBufferNode).
    // Defined in .cpp from FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT.
    static const uint32_t kRingSize;

    // Per-kind owning shell octrees (built once, kept alive for the node's lifetime).
    std::vector<Vixen::SVO::ShellOctree>   shellOctrees_;
    Vixen::SVO::ConcatenatedOctrees        concatenated_;
    bool                                   octreesBuilt_ = false;

    // Current instance list (set by SetInstances; uploaded in ExecuteImpl).
    std::vector<Vixen::SVO::BodyInstanceGpu> instances_;
    uint32_t                                 instanceCount_ = 0;

    // --- GPU resources (persistent across recompile; freed only at FinalTeardown) ---
    VkBuffer       nodesBuffer_     = VK_NULL_HANDLE;
    VkDeviceMemory nodesMemory_     = VK_NULL_HANDLE;
    VkBuffer       bricksBuffer_    = VK_NULL_HANDLE;
    VkDeviceMemory bricksMemory_    = VK_NULL_HANDLE;
    VkBuffer       materialsBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory materialsMemory_ = VK_NULL_HANDLE;
    VkBuffer       configBuffer_    = VK_NULL_HANDLE;
    VkDeviceMemory configMemory_    = VK_NULL_HANDLE;

    // Instance SSBO ring (one buffer per frame-in-flight — never freed on the tick path).
    PerFrameResources perFrame_;
    VkDeviceSize      instanceRingCapacity_ = 0;  // bytes per ring slot (grow-only)
};

} // namespace Vixen::RenderGraph
