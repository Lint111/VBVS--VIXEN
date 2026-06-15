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
 * ID-buffer readback — a single-texel vkCmdCopyImageToBuffer of the crosshair
 * pixel of PickIdTargetNode's per-pixel pick-ID image (binding 9), fenced and
 * mapped to decode the packed pickID — and emits a SelectionCandidate on its
 * CANDIDATE output. The SelectionCoordinatorNode gathers it (plus any other
 * provider nodes' candidates) via a MultiConnect accumulation slot and resolves.
 *
 * The readback logic was MOVED here from the old C++ provider, not rewritten
 * (EnsureStagingBuffer / ReadCenterPixel / DestroyStagingBuffer below were that
 * object's methods verbatim). The only structural changes are node-shaped:
 *   - the device is the base NodeInstance::device (SetDevice in CompileImpl from
 *     the VULKAN_DEVICE input; GetDevice() in Execute/Cleanup) — no device member;
 *   - it edge-detects the left button itself (a provider only resolves on click);
 *   - it emits a SelectionCandidate (hit + id + priority + depth + worldPos) every
 *     Execute (hit=false off the click edge / on a miss) instead of returning a
 *     std::optional<Hit> to a coordinator that drove it.
 *
 * It owns a lazily-created host-visible staging buffer for the buffer's lifetime
 * (freed in CleanupImpl — RAII). Off the click edge the per-frame cost is an edge
 * comparison and one slot write.
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
    // Lazily create the host-visible staging buffer for the single readback texel.
    // Created once (first readback) and reused — picks are infrequent. Returns true
    // when the staging buffer is ready. (Was VoxelSelectionProvider::EnsureStagingBuffer.)
    bool EnsureStagingBuffer();

    // Record the one-shot center-texel copy on commandPool_, submit on
    // GetDevice()->queue with a fresh fence, wait it, then map the staged uint32 into
    // pickIDOut. Returns true on success. (Was VoxelSelectionProvider::ReadCenterPixel.)
    bool ReadCenterPixel(uint32_t width, uint32_t height, uint32_t& pickIDOut);

    // Free the staging buffer + memory (idempotent). Called from CleanupImpl and on
    // a device change at CompileImpl. (Was VoxelSelectionProvider::DestroyStagingBuffer.)
    void DestroyStagingBuffer();

    // ----- Compile-cached Dependency handles (stable for the cached scene's lifetime) -----
    // The device is NOT cached here: it lives in the base NodeInstance (SetDevice/GetDevice),
    // re-set each CompileImpl. Only the command pool + ID image are cached locally.
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkImage       idImage_     = VK_NULL_HANDLE;

    // ----- Host-visible staging buffer for the single readback texel (R32_UINT) -----
    VkBuffer       stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    static constexpr VkDeviceSize kStagingSize = sizeof(uint32_t);  // one R32_UINT texel

    // ----- Provider config -----
    int priority_ = 0;  ///< Layer priority (PARAM_PRIORITY) stamped on every candidate.

    // Edge detection for the left mouse button (resolve on the down-edge only).
    bool lastLeftDown_ = false;

    // Pick-ID encoding (must match the shader's imageStore at binding 9):
    //   pickID = hit ? ((brickIndex << 10) | (voxelLinearIdx & 0x3FF)) : kMissSentinel.
    static constexpr uint32_t kMissSentinel  = 0xFFFFFFFFu;
    static constexpr uint32_t kVoxelIdxMask  = 0x3FFu;  // voxelLinearIdx is 0..511 (9 bits)
    static constexpr uint32_t kBrickIdxShift = 10u;
};

} // namespace Vixen::RenderGraph
