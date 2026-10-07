// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

// Persistent procedural octahedral background cache. The image is generated on the CPU from a
// stable seed, uploaded when its node parameters or optional star-list input change, then read by
// SpatialReuseShade only for ray misses. Disabled images are cleared to transparent black.

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/SkySphereNodeConfig.h"
#include "IRenderTarget.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/** @brief Node type for the persistent octahedral sky-sphere image. */
class SkySphereNodeType : public TypedNodeType<SkySphereNodeConfig> {
public:
    SkySphereNodeType(const std::string& typeName = "SkySphere")
        : TypedNodeType<SkySphereNodeConfig>(typeName) {}
    virtual ~SkySphereNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Owns a persistent octahedral image used as an optional procedural background.
 *
 * The image persists across graph recompile and refreshes only when a live parameter changes
 * or a connected star-list source provides new values at its configured cadence.
 */
class SkySphereNode : public TypedNode<SkySphereNodeConfig> {
public:
    using Base = TypedNode<SkySphereNodeConfig>;

    SkySphereNode(const std::string& instanceName, NodeType* nodeType);
    ~SkySphereNode() override = default;

protected:
    void TypedSetupImpl(TypedSetupContext& ctx) override;
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;
    void TypedCleanupImpl(TypedCleanupContext& ctx) override;

private:
    void CreateImage(Vixen::Vulkan::Resources::VulkanDevice* device, VkCommandPool commandPool);
    void TransitionToGeneral(VkCommandPool commandPool);
    void RefreshImage(bool enabled, uint32_t seed, float brightness,
                      const std::vector<SkySphereStar>& extraStars);
    void UploadPixels(const std::vector<uint16_t>* rgba16, bool initialized);
    void DestroyImage();

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    uint32_t refreshCadenceFrames_ = 0;
    uint64_t executeFrame_ = 0;

    bool imageInitialized_ = false;
    bool cachedEnabled_ = false;
    uint32_t cachedSeed_ = 0;
    float cachedBrightness_ = -1.0f;
    bool starListInitialized_ = false;
    uint64_t cachedStarListHash_ = 0;
    std::vector<SkySphereStar> cachedExtraStars_;

    Vixen::Vulkan::Resources::RenderTargetData target_;
    Vixen::Vulkan::Resources::VulkanDevice* device_ = nullptr;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
};

} // namespace Vixen::RenderGraph
