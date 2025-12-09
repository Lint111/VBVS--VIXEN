#include "pch.h"
#include "RenderPassCacher.h"
#include "MainCacher.h"
#include "VulkanDevice.h"
#include "VixenHash.h"
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

namespace CashSystem {

void RenderPassCacher::Cleanup() {
    LOG_INFO("Cleaning up " + std::to_string(m_entries.size()) + " cached render passes");

    // Destroy all cached Vulkan resources
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource) {
                if (entry.resource->renderPass != VK_NULL_HANDLE) {
                    LOG_DEBUG("Destroying VkRenderPass: " + std::to_string(reinterpret_cast<uint64_t>(entry.resource->renderPass)));
                    vkDestroyRenderPass(GetDevice()->device, entry.resource->renderPass, nullptr);
                    entry.resource->renderPass = VK_NULL_HANDLE;
                }
            }
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    LOG_INFO("Cleanup complete");
}

std::shared_ptr<RenderPassWrapper> RenderPassCacher::GetOrCreate(const RenderPassCreateParams& ci) {
    auto key = ComputeKey(ci);
    std::string renderPassName = std::string("color:") + std::to_string(static_cast<int>(ci.colorFormat)) +
                                 (ci.hasDepth ? ("+depth:" + std::to_string(static_cast<int>(ci.depthFormat))) : "");

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            LOG_DEBUG("CACHE HIT for render pass " + renderPassName + " (key=" + std::to_string(key) + ")");
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            LOG_DEBUG("CACHE PENDING for render pass " + renderPassName + " (key=" + std::to_string(key) + "), waiting...");
            return pit->second.get();
        }
    }

    LOG_DEBUG("CACHE MISS for render pass " + renderPassName + " (key=" + std::to_string(key) + "), creating new resource...");

    // Call parent implementation which will invoke Create()
    return TypedCacher<RenderPassWrapper, RenderPassCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<RenderPassWrapper> RenderPassCacher::Create(const RenderPassCreateParams& ci) {
    LOG_DEBUG("Creating new render pass: color=" + std::to_string(ci.colorFormat) + (ci.hasDepth ? (", depth=" + std::to_string(ci.depthFormat)) : ""));

    auto wrapper = std::make_shared<RenderPassWrapper>();
    wrapper->colorFormat = ci.colorFormat;
    wrapper->depthFormat = ci.depthFormat;
    wrapper->hasDepth = ci.hasDepth;

    // Setup attachments
    VkAttachmentDescription attachments[2];
    uint32_t attachmentCount = ci.hasDepth ? 2 : 1;

    // Color attachment
    attachments[0] = {};
    attachments[0].format = ci.colorFormat;
    attachments[0].samples = ci.samples;
    attachments[0].loadOp = ci.colorLoadOp;
    attachments[0].storeOp = ci.colorStoreOp;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = ci.initialLayout;
    attachments[0].finalLayout = ci.finalLayout;
    attachments[0].flags = 0;

    // Depth attachment (if enabled)
    if (ci.hasDepth) {
        attachments[1] = {};
        attachments[1].format = ci.depthFormat;
        attachments[1].samples = ci.samples;
        attachments[1].loadOp = ci.depthLoadOp;
        attachments[1].storeOp = ci.depthStoreOp;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[1].flags = 0;
    }

    // Attachment references
    VkAttachmentReference colorReference = {};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference = {};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Subpass description
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.flags = 0;
    subpass.inputAttachmentCount = 0;
    subpass.pInputAttachments = nullptr;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pResolveAttachments = nullptr;
    subpass.pDepthStencilAttachment = ci.hasDepth ? &depthReference : nullptr;
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments = nullptr;

    // Subpass dependency
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = ci.srcStageMask;
    dependency.dstStageMask = ci.dstStageMask;
    dependency.srcAccessMask = ci.srcAccessMask;
    dependency.dstAccessMask = ci.dstAccessMask;
    dependency.dependencyFlags = 0;

    // Create render pass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.pNext = nullptr;
    renderPassInfo.attachmentCount = attachmentCount;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(GetDevice()->device, &renderPassInfo, nullptr, &wrapper->renderPass);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("RenderPassCacher: Failed to create render pass (VkResult: " +
                                 std::to_string(result) + ")");
    }

    LOG_DEBUG("VkRenderPass created: " + std::to_string(reinterpret_cast<uint64_t>(wrapper->renderPass)));

    return wrapper;
}

std::uint64_t RenderPassCacher::ComputeKey(const RenderPassCreateParams& ci) const {
    // Combine all parameters into a unique key
    std::ostringstream keyStream;
    keyStream << static_cast<int>(ci.colorFormat) << "|"
              << static_cast<int>(ci.samples) << "|"
              << static_cast<int>(ci.colorLoadOp) << "|"
              << static_cast<int>(ci.colorStoreOp) << "|"
              << static_cast<int>(ci.initialLayout) << "|"
              << static_cast<int>(ci.finalLayout) << "|"
              << ci.hasDepth << "|";

    if (ci.hasDepth) {
        keyStream << static_cast<int>(ci.depthFormat) << "|"
                  << static_cast<int>(ci.depthLoadOp) << "|"
                  << static_cast<int>(ci.depthStoreOp) << "|";
    }

    keyStream << static_cast<int>(ci.srcStageMask) << "|"
              << static_cast<int>(ci.dstStageMask) << "|"
              << static_cast<int>(ci.srcAccessMask) << "|"
              << static_cast<int>(ci.dstAccessMask);

    // Use standard hash function
    const std::string keyString = keyStream.str();
    return std::hash<std::string>{}(keyString);
}

bool RenderPassCacher::SerializeToFile(const std::filesystem::path& path) const {
    LOG_INFO("SerializeToFile: Serializing " + std::to_string(m_entries.size()) + " render pass configs to " + path.string());

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        LOG_ERROR("SerializeToFile: Failed to open file for writing");
        return false;
    }

    // Write entry count
    std::shared_lock rlock(m_lock);
    uint32_t count = static_cast<uint32_t>(m_entries.size());
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // Write each entry: key + create params (not VkRenderPass handle)
    for (const auto& [key, entry] : m_entries) {
        ofs.write(reinterpret_cast<const char*>(&key), sizeof(key));

        // Serialize metadata for recreation
        const auto& w = entry.resource;
        ofs.write(reinterpret_cast<const char*>(&w->colorFormat), sizeof(w->colorFormat));
        ofs.write(reinterpret_cast<const char*>(&w->depthFormat), sizeof(w->depthFormat));
        ofs.write(reinterpret_cast<const char*>(&w->hasDepth), sizeof(w->hasDepth));
    }

    LOG_INFO("SerializeToFile: Serialization complete");
    return true;
}

bool RenderPassCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    LOG_INFO("DeserializeFromFile: Deserializing render pass configs from " + path.string());

    if (!std::filesystem::exists(path)) {
        LOG_INFO("DeserializeFromFile: Cache file does not exist");
        return false;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        LOG_ERROR("DeserializeFromFile: Failed to open file for reading");
        return false;
    }

    // Read entry count
    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

    LOG_INFO("DeserializeFromFile: Loading " + std::to_string(count) + " render pass metadata entries");

    // Note: We only deserialize metadata. VkRenderPass handles will be recreated on-demand
    // via GetOrCreate() when the parameters match. This approach ensures driver compatibility.

    for (uint32_t i = 0; i < count; ++i) {
        std::uint64_t key;
        ifs.read(reinterpret_cast<char*>(&key), sizeof(key));

        VkFormat colorFormat, depthFormat;
        bool hasDepth;
        ifs.read(reinterpret_cast<char*>(&colorFormat), sizeof(colorFormat));
        ifs.read(reinterpret_cast<char*>(&depthFormat), sizeof(depthFormat));
        ifs.read(reinterpret_cast<char*>(&hasDepth), sizeof(hasDepth));

        // We don't pre-create VkRenderPass handles from cache file
        // They will be created on first GetOrCreate() call with matching params
        LOG_DEBUG("Loaded metadata for key " + std::to_string(key) + " (color=" + std::to_string(colorFormat) + ", depth=" + std::to_string(depthFormat) + ")");
    }

    LOG_INFO("DeserializeFromFile: Deserialization complete (handles will be created on-demand)");
    return true;
}

} // namespace CashSystem
