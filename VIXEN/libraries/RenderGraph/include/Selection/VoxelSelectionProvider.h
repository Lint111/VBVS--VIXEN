#pragma once

#include "Selection/ISelectionProvider.h"
#include "Selection/SelectContext.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <optional>

namespace Vixen::RenderGraph {

/**
 * @brief Voxel domain provider — GPU ID-buffer readback → pickID (SEL-P2).
 *
 * The FIRST ISelectionProvider under the engine-wide selection system. It owns
 * EXACTLY the GPU readback that the shipped PickingNode used to perform inline:
 * a single-texel vkCmdCopyImageToBuffer of the crosshair pixel of the per-pixel
 * pick-ID image (binding 9, written by the voxel compute ray-march), fenced and
 * mapped to decode the packed pickID. This class is the new home of that logic —
 * it was MOVED here verbatim, not re-implemented (see Selection-System-Design,
 * "Migration": PickingNode's readback → VoxelSelectionProvider, unchanged).
 *
 * It is NOT a graph node. It is a plain C++ object the SelectionCoordinatorNode
 * owns and drives: the coordinator calls configure() once at compile (handing it
 * the device / command pool / current ID image cached from the graph) and then
 * resolve() on each click edge. The provider holds the Vulkan handles it needs
 * plus a lazily-created host-visible staging buffer it owns for the buffer's
 * lifetime (freed in the dtor — RAII, mirroring the node's old CleanupImpl).
 *
 * Dependency-light: depends only on Vulkan + the engine-native Selection headers
 * (no app/UI deps), consistent with the design's "engine-native, dependency-light"
 * decision.
 */
class VoxelSelectionProvider final : public ISelectionProvider {
public:
    VoxelSelectionProvider() = default;
    ~VoxelSelectionProvider() override;

    // Non-copyable / non-movable: it owns raw Vulkan handles (staging buffer +
    // memory). Copying would double-free; moving is unnecessary (the coordinator
    // holds it via unique_ptr).
    VoxelSelectionProvider(const VoxelSelectionProvider&) = delete;
    VoxelSelectionProvider& operator=(const VoxelSelectionProvider&) = delete;
    VoxelSelectionProvider(VoxelSelectionProvider&&) = delete;
    VoxelSelectionProvider& operator=(VoxelSelectionProvider&&) = delete;

    /**
     * @brief Bind the Vulkan resources the readback needs (called by the
     *        coordinator at compile, with handles it cached from the graph).
     *
     * @param device  Owning device for the one-shot copy submit + staging buffer.
     * @param pool    Command pool for the one-shot readback command buffer.
     * @param idImage The current-frame pick-ID image (VK_FORMAT_R32_UINT, in
     *                VK_IMAGE_LAYOUT_GENERAL) to copy the center texel from.
     *
     * Re-callable on recompile: it just re-caches the handles. The staging buffer
     * is preserved across reconfigure if the device is unchanged; if the device
     * differs the old buffer is torn down first (so we never leak across devices).
     */
    void configure(Vixen::Vulkan::Resources::VulkanDevice* device,
                   VkCommandPool pool,
                   VkImage idImage);

    /**
     * @brief Read back the crosshair texel of the ID image and decode the pick.
     *
     * Uses ctx.viewportWidth/Height for the center offset (the engine runs the
     * cursor locked to screen center; see SelectContext). On a miss (pickID ==
     * 0xFFFFFFFF) or if resources/viewport are not ready it returns std::nullopt.
     * On a hit it returns Hit{ {ProviderKind::Voxel, pickID}, depth=0, worldPos=0 }
     * — world position from brick/voxel is deferred per the design.
     */
    std::optional<Hit> resolve(const SelectContext& ctx) override;

    /// Low priority — the world layer. UI providers register higher so UI occludes.
    int priority() const override { return kPriority; }

    /// This provider owns the Voxel domain.
    ProviderKind kind() const override { return ProviderKind::Voxel; }

private:
    // Lazily create the host-visible staging buffer for the single readback texel.
    // Created once (first resolve) and reused — picks are infrequent. Returns true
    // when the staging buffer is ready. (Was PickingNode::EnsureStagingBuffer.)
    bool EnsureStagingBuffer();

    // Record the one-shot center-texel copy on commandPool_, submit on
    // device_->queue with a fresh fence, wait it, then map the staged uint32 into
    // pickIDOut. Returns true on success. (Was PickingNode::ReadCenterPixel.)
    bool ReadCenterPixel(uint32_t width, uint32_t height, uint32_t& pickIDOut);

    // Free the staging buffer + memory (idempotent). Called on device change and
    // from the dtor. (Was PickingNode::DestroyStagingBuffer.)
    void DestroyStagingBuffer();

    // ----- Bound Vulkan resources (from configure(); stable for the cached scene) -----
    Vixen::Vulkan::Resources::VulkanDevice* device_      = nullptr;
    VkCommandPool                           commandPool_ = VK_NULL_HANDLE;
    VkImage                                 idImage_     = VK_NULL_HANDLE;

    // ----- Host-visible staging buffer for the single readback texel (R32_UINT) -----
    VkBuffer       stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    static constexpr VkDeviceSize kStagingSize = sizeof(uint32_t);  // one R32_UINT texel

    // Pick-ID encoding (must match the shader's imageStore at binding 9):
    //   pickID = hit ? ((brickIndex << 10) | (voxelLinearIdx & 0x3FF)) : kMissSentinel.
    static constexpr uint32_t kMissSentinel  = 0xFFFFFFFFu;
    static constexpr uint32_t kVoxelIdxMask  = 0x3FFu;  // voxelLinearIdx is 0..511 (9 bits)
    static constexpr uint32_t kBrickIdxShift = 10u;

    // World layer priority (low). UI/mesh providers use higher values to occlude.
    static constexpr int kPriority = 0;
};

} // namespace Vixen::RenderGraph
