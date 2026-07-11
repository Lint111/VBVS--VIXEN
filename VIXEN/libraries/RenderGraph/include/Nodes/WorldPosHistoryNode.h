// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/WorldPosHistoryNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the worldPos/depth companion history target (persistent storage image).
 */
class WorldPosHistoryNodeType : public TypedNodeType<WorldPosHistoryNodeConfig> {
public:
    WorldPosHistoryNodeType(const std::string& typeName = "WorldPosHistory")
        : TypedNodeType<WorldPosHistoryNodeConfig>(typeName) {}
    virtual ~WorldPosHistoryNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Allocates the worldPos/depth companion history image (Sampled Lighting Inc3 M2 --
 * KI-023 prerequisite): a SINGLE persistent 2D STORAGE image, sized to the render target's
 * extent, rgba32f (worldPos.xyz in .xyz, hitT/depth in .w).
 *
 * Mirrors AccumulationHistoryNode exactly (see its own file header for the full "one
 * persistent resource, not a ring" rationale) -- the only structural difference is format
 * (rgba32f vs rgba8, since world-space positions need float precision, not [0,1] color range)
 * and usage (worldPos/depth geometry, not shaded color).
 *
 * Written each frame by DirectLighting.comp alongside historyImage's own write, at the same
 * pixelCoords; read back at the reprojected texel to validate reprojection GEOMETRICALLY
 * (length(histWorldPos - bestWorldPos) > epsilon) rather than by color-consistency -- the fix
 * for KI-023 (a converged history legitimately differs from a noisy current sample once Inc3's
 * ReSTIR lands, so a color-based reject would fight the noise). This same buffer is available
 * for Inc3's OWN reservoir-reprojection validity test (M4/M5) -- one buffer, two consumers.
 *
 * Usage = STORAGE only. Layout: VK_IMAGE_LAYOUT_GENERAL, one-time UNDEFINED->GENERAL transition
 * at Compile via a one-shot command buffer, identical mechanics to AccumulationHistoryNode.
 *
 * Lifecycle: persists across graph recompile (same extent); released only on FinalTeardown. A
 * genuine resize recreates the image at the new extent with fresh uninitialized content -- safe
 * because the shader's existing alpha>=1.0 / bounds-reject guards already skip reading historyImage
 * (and now this buffer) whenever there's no valid prior frame to reproject from.
 */
class WorldPosHistoryNode : public TypedNode<WorldPosHistoryNodeConfig> {
public:
    using Base = TypedNode<WorldPosHistoryNodeConfig>;

    WorldPosHistoryNode(const std::string& instanceName, NodeType* nodeType);
    ~WorldPosHistoryNode() override = default;

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
    uint32_t createdWidth_  = 0;
    uint32_t createdHeight_ = 0;

    static constexpr VkFormat kFormat = VK_FORMAT_R32G32B32A32_SFLOAT;  // worldPos.xyz + hitT.w
};

} // namespace Vixen::RenderGraph
