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
 * @brief Allocates the temporal-accumulation history image (Sampled Lighting Inc2 M1): a SINGLE
 * persistent 2D STORAGE image, sized to the render target's extent, format-matched to outputImage
 * (VK_FORMAT_R8G8B8A8_UNORM -- RenderTargetNode's own default; confirmed from
 * RenderTargetNode.cpp, NOT rgba16f).
 *
 * Why a dedicated node, not the RenderTargetNode ring or a PickIdTargetNode-style per-frame-in-
 * flight ring: history must survive ACROSS frames by definition (this frame's accumulate step
 * reads what LAST frame wrote) -- KI-009 documents that RenderTargetNode's own ring rotates every
 * frame, so wiring history onto it would read a stale/wrong ring slot depending on frame parity.
 * A ring sized to frames-in-flight has the identical problem one level removed: it doesn't buy
 * anything a single persistent image doesn't already give, and adds needless index-tracking. So
 * this node allocates exactly ONE image+memory+view that lives for the graph's lifetime (until a
 * genuine resize), the same "one persistent resource" shape StorageBufferNode's own HitRecord SSBO
 * uses (also read-and-written by the SAME dispatch, also extent-tracking, also not a ring).
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
 * M1 scope was allocate + transition + wire to a shader binding (20) that declared but did NOT
 * yet read/write it -- pure plumbing, zero visual delta. M2 (BodyInstanceRayMarch.comp's
 * accumulate seam) is the first milestone that actually samples/writes it: on frame 1 of a run
 * (or the frame right after a reset-on-motion reset, alpha>=1.0) the shader skips the
 * historyImage read entirely and writes pure outColor, so this image's genuinely-uninitialized
 * first content (or stale content after a resize recreate) is never actually read -- see the
 * accumulate seam's alpha>=1.0 guard in the shader.
 *
 * Lifecycle: persists across graph recompile (same extent); released only on FinalTeardown. A
 * genuine resize recreates the image at the new extent with fresh uninitialized content;
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
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void CreateImage(Vixen::Vulkan::Resources::VulkanDevice* device, VkCommandPool commandPool);
    void TransitionToGeneral(VkCommandPool commandPool);
    void DestroyImage();

    VkImage        image_  = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView    view_   = VK_NULL_HANDLE;

    uint32_t width_  = 0;
    uint32_t height_ = 0;
    // Extent the image was actually created at, to detect a genuine resize (mirrors
    // PickIdTargetNode's ringWidth_/ringHeight_ vs width_/height_ comparison).
    uint32_t createdWidth_  = 0;
    uint32_t createdHeight_ = 0;

    static constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
};

} // namespace Vixen::RenderGraph
