#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include <cstdint>
#include <string>
#include <memory>

namespace CashSystem {

/**
 * @brief Render pass resource wrapper
 *
 * Stores VkRenderPass and associated metadata.
 */
struct RenderPassWrapper {
    VkRenderPass renderPass = VK_NULL_HANDLE;

    // Cache identification (for debugging/logging)
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    bool hasDepth = false;
};

/**
 * @brief Render pass creation parameters
 *
 * All parameters that affect render pass creation.
 * Used to generate cache keys and create render passes.
 */
struct RenderPassCreateParams {
    // Color attachment
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkAttachmentLoadOp colorLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp colorStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment (optional)
    bool hasDepth = false;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkAttachmentLoadOp depthLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE;

    // Subpass dependency
    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags srcAccessMask = 0;
    VkAccessFlags dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
};

/**
 * @brief TypedCacher for render pass resources
 *
 * Caches render passes based on attachment formats, load/store ops,
 * and layout transitions. Render passes are expensive to create
 * (driver validation) and highly reusable.
 *
 * Usage:
 * ```cpp
 * auto& cacher = mainCacher.GetOrCreateDeviceRegistry(device).GetCacher<RenderPassCacher>();
 *
 * RenderPassCreateParams params{};
 * params.colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
 * params.colorLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
 * params.colorStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
 * params.hasDepth = true;
 * params.depthFormat = VK_FORMAT_D32_SFLOAT;
 *
 * auto wrapper = cacher.GetOrCreate(params);
 * VkRenderPass renderPass = wrapper->renderPass;
 * ```
 */
class RenderPassCacher : public TypedCacher<RenderPassWrapper, RenderPassCreateParams> {
public:
    RenderPassCacher() = default;
    ~RenderPassCacher() override = default;

    // Override to add cache hit/miss logging
    std::shared_ptr<RenderPassWrapper> GetOrCreate(const RenderPassCreateParams& ci);

protected:
    // TypedCacher implementation
    std::shared_ptr<RenderPassWrapper> Create(const RenderPassCreateParams& ci) override;
    std::uint64_t ComputeKey(const RenderPassCreateParams& ci) const override;

    // Resource cleanup
    void Cleanup() override;

    // Serialization (render passes are not serializable - driver/GPU specific)
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "RenderPassCacher"; }
};

} // namespace CashSystem
