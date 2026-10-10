// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/AccumulationHistoryNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the temporal-accumulation history target (persistent storage image).
 */
class AccumulationHistoryNodeType : public TypedNodeType<AccumulationHistoryNodeConfig> {
public:
    AccumulationHistoryNodeType(const std::string& typeName = "AccumulationHistory")
        : TypedNodeType<AccumulationHistoryNodeConfig>(typeName) {}
    virtual ~AccumulationHistoryNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Allocates two temporal-accumulation history images, sized to the render target's extent,
 * format-matched to the scene-linear HDR intermediate (VK_FORMAT_R16G16B16A16_SFLOAT).
 *
 * The node owns a pair of persistent images and publishes one as previous history and the other
 * as the current output. Their roles flip once per graph execution, so a dispatch never reads and
 * writes the same image. This is a two-image history pair, not a frame-in-flight ring.
 *
 * Usage = STORAGE only (no TRANSFER_SRC/DST needed -- no clear-on-resize copy; see below for why
 * uninitialized content on (re)creation is safe).
 *
 * Layout: the compute shader will use the image as a STORAGE image, requiring
 * VK_IMAGE_LAYOUT_GENERAL. The image is created UNDEFINED and transitioned UNDEFINED->GENERAL
 * exactly once, at Compile, via a one-shot command buffer submitted on the device queue --
 * identical mechanics to PickIdTargetNode::TransitionAllToGeneral, just for one image instead of a
 * ring. Storage images remain in GENERAL across dispatches, so no per-frame barrier is required.
 *
 * On frame 1 of a run (or the frame right after a reset-on-motion reset, alpha>=1.0), the shader
 * skips history reads and writes pure outColor. The accumulation config also resets the frame
 * counter after graph recompile, so freshly recreated images are populated before later frames
 * read from them. Reprojection reads only the immutable previous image and writes only the
 * separate current image.
 *
 * Lifecycle: persists across graph recompile (same extent); released only on FinalTeardown. A
 * genuine resize recreates both images at the new extent with fresh uninitialized content;
 * AccumulationConfigNode::CompileImpl (which runs on every recompile, including a resize) forces
 * its frame counter to restart on the next Execute specifically to cover this case -- a resize
 * changes CameraData::aspect, not cameraPos/cameraDir, so the counter's own motion-epsilon check
 * alone would not have caught it.
 */
class AccumulationHistoryNode : public TypedNode<AccumulationHistoryNodeConfig> {
public:
    using Base = TypedNode<AccumulationHistoryNodeConfig>;

    AccumulationHistoryNode(const std::string& instanceName, NodeType* nodeType);
    ~AccumulationHistoryNode() override = default;

protected:
    void TypedSetupImpl(TypedSetupContext&    ctx) override;
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;
    void TypedCleanupImpl(TypedCleanupContext& ctx) override;

private:
    void CreateImage(Vixen::Vulkan::Resources::VulkanDevice* device, VkCommandPool commandPool,
                    uint32_t imageIndex);
    void TransitionToGeneral(VkCommandPool commandPool, VkImage image);
    void DestroyImages();

    VkImage        images_[2]  = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory memories_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView    views_[2]   = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    uint32_t nextWriteImageIndex_ = 1;

    uint32_t width_  = 0;
    uint32_t height_ = 0;
    // Extent the image was actually created at, to detect a genuine resize (mirrors
    // PickIdTargetNode's ringWidth_/ringHeight_ vs width_/height_ comparison).
    uint32_t createdWidth_  = 0;
    uint32_t createdHeight_ = 0;

    static constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
};

} // namespace Vixen::RenderGraph
