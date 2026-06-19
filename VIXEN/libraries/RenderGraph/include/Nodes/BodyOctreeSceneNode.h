#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
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
 *   3. creates 5 GPU buffers (nodes / bricks / materials SSBOs, the 3x256B config UBO,
 *      and the packed instance SSBO), host-visible/host-coherent, filled by memcpy,
 *   4. publishes the four octree slots (same names/types as VoxelGridNode) plus the
 *      new INSTANCE_BUFFER + INSTANCE_COUNT.
 *
 * Instance seam: the host (Task 8) calls SetInstances(...) to push the current body
 * list; that stores the vector, marks a dirty flag, and requests a recompile so the
 * instance SSBO is re-uploaded on the next compile (octrees are NOT rebuilt).
 *
 * Lifecycle (CRITICAL — see the WSL/Dozen recompile-frees-in-flight VM-panic trap):
 * the GPU buffers persist across recompile and are destroyed ONLY on FinalTeardown.
 * Mirrors InstanceBufferNode's FR-7 guard.
 *
 * NOTE: this node is intentionally NOT registered / wired into the render graph here;
 * Task 8 wires it (replacing VoxelGridNode's octree output) and bumps the submodule.
 */
class BodyOctreeSceneNode : public TypedNode<BodyOctreeSceneNodeConfig> {
public:
    using Base = TypedNode<BodyOctreeSceneNodeConfig>;

    BodyOctreeSceneNode(const std::string& instanceName, NodeType* nodeType);
    ~BodyOctreeSceneNode() override = default;

    /**
     * @brief Push the current per-body instance list (host -> node seam, Task 8).
     *
     * Stores the vector, marks the instance buffer dirty, and requests a recompile.
     * Thread-simple by design: the buffers are (re)built on the next CompileImpl.
     * The cached octrees are NOT affected (only the instance SSBO is re-uploaded).
     */
    void SetInstances(std::vector<Vixen::SVO::BodyInstanceGpu> instances);

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // --- Octree build / serialization (octrees + concatenated bytes cached) ---
    void EnsureOctreesBuilt();                                   // build + concatenate once
    void CreateOctreeBuffers(Vixen::Vulkan::Resources::VulkanDevice* device);  // 4 octree buffers
    void CreateOrUpdateInstanceBuffer(Vixen::Vulkan::Resources::VulkanDevice* device);  // instance SSBO
    void DestroyBuffers();

    // Build constants (one shell per kind; depth/material chosen here).
    static constexpr int      kShellDepth   = 6;  // 2^6 = 64 cells/axis
    static constexpr uint32_t kKindCount    = 3;

    // Per-kind owning shell octrees (built once, kept alive for the node's lifetime).
    std::vector<Vixen::SVO::ShellOctree>   shellOctrees_;
    Vixen::SVO::ConcatenatedOctrees        concatenated_;
    bool                                   octreesBuilt_ = false;

    // Current instance list + dirty flag (set by SetInstances).
    std::vector<Vixen::SVO::BodyInstanceGpu> instances_;
    uint32_t                                 instanceCount_     = 0;
    bool                                     instancesDirty_    = true;

    // --- GPU resources (persistent across recompile; freed only at FinalTeardown) ---
    VkBuffer       nodesBuffer_      = VK_NULL_HANDLE;
    VkDeviceMemory nodesMemory_      = VK_NULL_HANDLE;
    VkBuffer       bricksBuffer_     = VK_NULL_HANDLE;
    VkDeviceMemory bricksMemory_     = VK_NULL_HANDLE;
    VkBuffer       materialsBuffer_  = VK_NULL_HANDLE;
    VkDeviceMemory materialsMemory_  = VK_NULL_HANDLE;
    VkBuffer       configBuffer_     = VK_NULL_HANDLE;
    VkDeviceMemory configMemory_     = VK_NULL_HANDLE;

    VkBuffer       instanceBuffer_   = VK_NULL_HANDLE;
    VkDeviceMemory instanceMemory_   = VK_NULL_HANDLE;
    VkDeviceSize   instanceCapacity_ = 0;  // bytes currently allocated for the instance SSBO
};

} // namespace Vixen::RenderGraph
