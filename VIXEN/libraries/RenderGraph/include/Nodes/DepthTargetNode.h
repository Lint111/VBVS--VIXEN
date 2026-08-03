// Copyright (C) 2026 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/DepthTargetNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the B1 occlusion depth ping-pong pair.
 */
class DepthTargetNodeType : public TypedNodeType<DepthTargetNodeConfig> {
public:
    DepthTargetNodeType(const std::string& typeName = "DepthTarget")
        : TypedNodeType<DepthTargetNodeConfig>(typeName) {}
    virtual ~DepthTargetNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Raster-proxy B1 M4: TWO persistent R32_SFLOAT storage images ping-ponged
 * by frame parity — the march writes euclidean ray distance into [frame&1]
 * (binding 36, VIXEN_B1_OCCLUSION_CULL-gated) while the HiZ reduce reads last
 * frame's [(frame+1)&1].
 *
 * Distinct VkImage per slot ⇒ NO march↔reduce sync edge (the shell
 * double-buffer precedent, see DepthTargetNodeConfig.h). Both slots cleared to
 * the 1e30 miss sentinel once at creation (frame-0 reduce sees sky ⇒ cull skips
 * nothing). Lifecycle mirrors WorldPosHistoryNode: persists across recompile at
 * the same extent, recreated on resize, released on final teardown.
 */
class DepthTargetNode : public TypedNode<DepthTargetNodeConfig> {
public:
    using Base = TypedNode<DepthTargetNodeConfig>;

    DepthTargetNode(const std::string& instanceName, NodeType* nodeType);
    ~DepthTargetNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void CreateImages(Vixen::Vulkan::Resources::VulkanDevice* device, VkCommandPool commandPool);
    void DestroyImages();

    VkImage        images_[2]  = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory memories_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView    views_[2]   = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    uint32_t width_  = 0;
    uint32_t height_ = 0;
    uint32_t createdWidth_  = 0;
    uint32_t createdHeight_ = 0;

    static constexpr VkFormat kFormat = VK_FORMAT_R32_SFLOAT;  // euclidean ray distance
    static constexpr float kMissSentinel = 1.0e30f;            // HiZDownsampleMirror's kDepthMissSentinel
};

} // namespace Vixen::RenderGraph
