#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/PickIdTargetNodeConfig.h"
#include <vector>
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the GPU picking ID target (R32_UINT storage image ring)
 * Type ID: 126
 */
class PickIdTargetNodeType : public TypedNodeType<PickIdTargetNodeConfig> {
public:
    PickIdTargetNodeType(const std::string& typeName = "PickIdTarget")
        : TypedNodeType<PickIdTargetNodeConfig>(typeName) {}
    virtual ~PickIdTargetNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Allocates the per-pixel pick-ID target (AR#35 GPU picking, P1): an R32_UINT 2D STORAGE
 * image ring (one image+memory+view per in-flight frame), sized to the swapchain extent. The voxel
 * compute ray-march writes the packed (brickIndex<<10)|voxelLinearIdx hit identity into it at
 * descriptor binding 9, beside the color store at binding 0.
 *
 * Usage = STORAGE | TRANSFER_SRC (TRANSFER_SRC reserved for the P2 click-readback copy).
 *
 * Layout: the compute shader uses the image as a STORAGE image, which requires
 * VK_IMAGE_LAYOUT_GENERAL. Each ring image is created UNDEFINED and transitioned UNDEFINED->GENERAL
 * exactly once, at Compile, via a one-shot command buffer submitted on the device queue. Storage
 * images remain in GENERAL across dispatches (a storage write does not change layout), so no
 * per-frame barrier is required and the descriptor — always written with imageLayout = GENERAL by
 * DescriptorSetNode::HandleStorageImage — is correct every frame. This mirrors how the swapchain
 * storage image is kept in GENERAL for the dispatch, without coupling to ComputeDispatchNode.
 *
 * ExecuteImpl advances the ring index and re-emits the current frame's view/image (like a ring
 * buffer producer) so the DescriptorResourceGatherer picks up the right view at binding 9.
 *
 * FR-7 lifecycle: images persist across graph recompile; released only on FinalTeardown.
 */
class PickIdTargetNode : public TypedNode<PickIdTargetNodeConfig> {
public:
    using Base = TypedNode<PickIdTargetNodeConfig>;

    PickIdTargetNode(const std::string& instanceName, NodeType* nodeType);
    ~PickIdTargetNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    struct IdImage {
        VkImage        image  = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView    view   = VK_NULL_HANDLE;
    };

    void CreateImages(Vixen::Vulkan::Resources::VulkanDevice* device, VkCommandPool commandPool);
    void TransitionAllToGeneral(VkCommandPool commandPool);
    void DestroyImages();

    Vixen::Vulkan::Resources::VulkanDevice* device_      = nullptr;  // cached for cleanup
    std::vector<IdImage>                    images_;
    uint32_t                                width_       = 0;
    uint32_t                                height_      = 0;
    uint32_t                                imageCount_  = 0;
    uint32_t                                currentIndex_ = 0;
    static constexpr VkFormat               kFormat      = VK_FORMAT_R32_UINT;
};

} // namespace Vixen::RenderGraph
