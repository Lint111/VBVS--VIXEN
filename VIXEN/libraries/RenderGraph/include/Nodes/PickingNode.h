#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/PickingNodeConfig.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for GPU ID-buffer click-picking (AR#35) — node type ID 125.
 */
class PickingNodeType : public TypedNodeType<PickingNodeConfig> {
public:
    PickingNodeType(const std::string& typeName = "Picking")
        : TypedNodeType<PickingNodeConfig>(typeName) {}
    ~PickingNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief GPU ID-buffer click-picking node (AR#35, P2).
 *
 * On a left-mouse-button PRESS edge it reads back the CENTER pixel (crosshair — the
 * cursor is locked to screen center) of the per-pixel pick-ID image the voxel compute
 * ray-march wrote at binding 9 (PickIdTargetNode). The readback is a single-texel
 * vkCmdCopyImageToBuffer on a one-shot command buffer submitted on device->queue and
 * fenced with a fresh VkFence (waited, not vkQueueWaitIdle, so the render loop's own
 * submits are undisturbed); the staged uint32 pickID is then mapped, decoded
 * (brick = id >> 10, voxel = id & 0x3FF; 0xFFFFFFFF == miss) and reported (log +
 * PickResultEvent). The copy happens ONLY on the click edge, so per-frame cost is a
 * couple of pointer reads and an edge comparison.
 *
 * The CPU ray-march path (ComputePickRay + GaiaVoxelWorld) was removed: the ECS world
 * is null on cached-scene hits, so picking must use the GPU octree the shader already
 * traverses (see GPU-IDBuffer-Picking-Design-2026-06.md).
 */
class PickingNode : public TypedNode<PickingNodeConfig> {
public:
    PickingNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~PickingNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Lazily creates the host-visible staging buffer that holds the single readback texel.
    // Created once (first click) and reused — a click is infrequent, so one shared buffer
    // is ample. Returns true if the staging buffer is ready.
    bool EnsureStagingBuffer();

    // Reads back the center texel of the pick-ID image into pickIDOut. Records a one-shot
    // copy on commandPool_, submits on device_->queue with a fresh fence, waits it, then
    // maps the staging memory. Returns true on success (pickIDOut valid).
    bool ReadCenterPixel(uint32_t width, uint32_t height, uint32_t& pickIDOut);

    void DestroyStagingBuffer();

    // ----- Compile-cached Dependency handles (stable for the cached scene's lifetime) -----
    Vixen::Vulkan::Resources::VulkanDevice* device_      = nullptr;
    VkCommandPool                           commandPool_ = VK_NULL_HANDLE;
    VkImage                                 idImage_     = VK_NULL_HANDLE;

    // ----- Host-visible staging buffer for the single readback texel (R32_UINT) -----
    VkBuffer       stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    static constexpr VkDeviceSize kStagingSize = sizeof(uint32_t);  // one R32_UINT texel

    // Edge detection for the left mouse button (fire on the down-edge only).
    bool lastLeftDown_ = false;

    // Status mirrored to the LAST_PICK_HIT output (1 = last pick hit a voxel).
    uint32_t lastPickHit_ = 0;

    // Pick-ID encoding (must match the shader's imageStore at binding 9):
    //   pickID = hit ? ((brickIndex << 10) | (voxelLinearIdx & 0x3FF)) : kMissSentinel.
    static constexpr uint32_t kMissSentinel  = 0xFFFFFFFFu;
    static constexpr uint32_t kVoxelIdxMask  = 0x3FFu;  // voxelLinearIdx is 0..511 (9 bits)
    static constexpr uint32_t kBrickIdxShift = 10u;
};

} // namespace Vixen::RenderGraph
