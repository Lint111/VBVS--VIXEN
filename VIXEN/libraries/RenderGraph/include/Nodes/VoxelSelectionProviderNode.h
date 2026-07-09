#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/VoxelSelectionProviderNodeConfig.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the voxel-domain selection provider (SEL-P2).
 */
class VoxelSelectionProviderNodeType : public TypedNodeType<VoxelSelectionProviderNodeConfig> {
public:
    VoxelSelectionProviderNodeType(const std::string& typeName = "VoxelSelectionProvider")
        : TypedNodeType<VoxelSelectionProviderNodeConfig>(typeName) {}
    ~VoxelSelectionProviderNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Voxel-domain selection provider — now a graph NODE (SEL-P2).
 *
 * The voxel selection provider is a first-class node (was the C++
 * VoxelSelectionProvider object the SelectionCoordinatorNode owned), per the
 * everything-is-a-node principle. On a left-click down-edge it performs the GPU
 * ID-buffer readback — a single-texel vkCmdCopyImageToBuffer of the pixel under
 * the actual cursor position (the ClickEvent's own x/y, not a fixed viewport-center
 * crosshair — changed so mouse-driven inspection/picking has real fine-grained
 * control over what gets sampled) of PickIdTargetNode's per-pixel pick-ID image
 * (binding 9), fenced and mapped to decode the packed pickID — and emits a
 * SelectionCandidate on its CANDIDATE output. The SelectionCoordinatorNode gathers
 * it (plus any other provider nodes' candidates) via a MultiConnect accumulation
 * slot and resolves.
 *
 * The readback logic was MOVED here from the old C++ provider, not rewritten
 * (EnsureStagingBuffer / ReadPixelAt / DestroyStagingBuffer below were that
 * object's methods, since renamed/parameterized for cursor-position picking).
 * The only structural changes are node-shaped:
 *   - the device is the base NodeInstance::device (SetDevice in CompileImpl from
 *     the VULKAN_DEVICE input; GetDevice() in Execute/Cleanup) — no device member;
 *   - it looks for a left-button press entry in InputState.clicksThisFrame (a
 *     provider only resolves on click; input-rework slice 1 M3 — the shared click
 *     list, not a private per-node edge detector);
 *   - it emits a SelectionCandidate (hit + id + priority + depth + worldPos) every
 *     Execute (hit=false off the click edge / on a miss) instead of returning a
 *     std::optional<Hit> to a coordinator that drove it.
 *
 * It owns a lazily-created host-visible staging buffer for the buffer's lifetime
 * (freed in CleanupImpl — RAII). Off the click edge the per-frame cost is a scan of
 * clicksThisFrame (typically empty) and one slot write.
 */
class VoxelSelectionProviderNode : public TypedNode<VoxelSelectionProviderNodeConfig> {
public:
    VoxelSelectionProviderNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~VoxelSelectionProviderNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Lazily (re)create the host-visible staging buffer, sized for `bytesNeeded`. Reused
    // across clicks as long as the requested size doesn't grow (e.g. a window resize on
    // the KI-012 full-image path); recreated on growth. Returns true when ready.
    // (Was VoxelSelectionProvider::EnsureStagingBuffer, now size-parameterized for KI-012.)
    bool EnsureStagingBuffer(VkDeviceSize bytesNeeded);

    // Record the one-shot readback copy on commandPool_, submit on GetDevice()->queue with
    // a fresh fence, wait it, then map the staged pixel(s) and extract the pixel at
    // (targetX, targetY) into pickIDOut. Returns true on success. Branches on
    // requiresFullImageTransfers_ (KI-012): when the graphics queue family's
    // minImageTransferGranularity is (0,0,0), a sub-region copy (the single target texel at an
    // arbitrary offset) is a spec violation on that queue — some drivers (Dozen) tolerate it
    // anyway, but this isn't guaranteed elsewhere — so that case copies the WHOLE id image to a
    // full-size staging buffer and indexes the target texel on the CPU side instead.
    // (Was VoxelSelectionProvider::ReadCenterPixel — renamed/parameterized to pick at the
    // actual cursor position instead of always the viewport center.)
    bool ReadPixelAt(uint32_t width, uint32_t height, uint32_t targetX, uint32_t targetY,
                     uint32_t& pickIDOut);

    // Free the staging buffer + memory (idempotent). Called from CleanupImpl and on
    // a device change at CompileImpl. (Was VoxelSelectionProvider::DestroyStagingBuffer.)
    void DestroyStagingBuffer();

    // ----- Compile-cached Dependency handles (stable for the cached scene's lifetime) -----
    // The device is NOT cached here: it lives in the base NodeInstance (SetDevice/GetDevice),
    // re-set each CompileImpl. Only the command pool + ID image are cached locally.
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkImage       idImage_     = VK_NULL_HANDLE;

    // KI-012: cached once per Compile from GetDevice()->RequiresFullImageTransfers() — whether
    // this device's graphics queue family requires whole-image transfers (see VulkanDevice.h).
    bool requiresFullImageTransfers_ = false;

    // ----- Host-visible staging buffer for the readback -----
    // Sized for one R32_UINT texel on the common path, or the whole id image
    // (stagingWidth_ * stagingHeight_ * sizeof(uint32_t)) when requiresFullImageTransfers_.
    VkBuffer       stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    VkDeviceSize   stagingCapacity_ = 0;  // bytes actually allocated; EnsureStagingBuffer grows-only
    static constexpr VkDeviceSize kSingleTexelSize = sizeof(uint32_t);  // one R32_UINT texel

    // ----- Provider config -----
    int priority_ = 0;  ///< Layer priority (PARAM_PRIORITY) stamped on every candidate.

    // Pick-ID encoding (must match the shader's imageStore at binding 9):
    //   pickID = hit ? ((brickIndex << 10) | (voxelLinearIdx & 0x3FF)) : kMissSentinel.
    static constexpr uint32_t kMissSentinel  = 0xFFFFFFFFu;
    static constexpr uint32_t kVoxelIdxMask  = 0x3FFu;  // voxelLinearIdx is 0..511 (9 bits)
    static constexpr uint32_t kBrickIdxShift = 10u;
};

} // namespace Vixen::RenderGraph
