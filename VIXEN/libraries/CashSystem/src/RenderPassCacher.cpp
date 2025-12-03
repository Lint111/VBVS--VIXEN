#include "pch.h"
#include "RenderPassCacher.h"
#include "MainCacher.h"
#include "VulkanDevice.h"
#include "VixenHash.h"
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

namespace CashSystem {

void RenderPassCacher::Cleanup() {
    std::cout << "[RenderPassCacher::Cleanup] Cleaning up " << m_entries.size() << " cached render passes" << std::endl;

    // Destroy all cached Vulkan resources
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource) {
                if (entry.resource->renderPass != VK_NULL_HANDLE) {
                    std::cout << "[RenderPassCacher::Cleanup] Destroying VkRenderPass: "
                              << reinterpret_cast<uint64_t>(entry.resource->renderPass) << std::endl;
                    vkDestroyRenderPass(GetDevice()->device, entry.resource->renderPass, nullptr);
                    entry.resource->renderPass = VK_NULL_HANDLE;
                }
            }
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    std::cout << "[RenderPassCacher::Cleanup] Cleanup complete" << std::endl;
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
            std::cout << "[RenderPassCacher::GetOrCreate] CACHE HIT for render pass " << renderPassName
                      << " (key=" << key << ", VkRenderPass="
                      << reinterpret_cast<uint64_t>(it->second.resource->renderPass) << ")" << std::endl;
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            std::cout << "[RenderPassCacher::GetOrCreate] CACHE PENDING for render pass " << renderPassName
                      << " (key=" << key << "), waiting..." << std::endl;
            return pit->second.get();
        }
    }

    std::cout << "[RenderPassCacher::GetOrCreate] CACHE MISS for render pass " << renderPassName
              << " (key=" << key << "), creating new resource..." << std::endl;

    // Call parent implementation which will invoke Create()
    return TypedCacher<RenderPassWrapper, RenderPassCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<RenderPassWrapper> RenderPassCacher::Create(const RenderPassCreateParams& ci) {
    std::cout << "[RenderPassCacher::Create] CACHE MISS - Creating new render pass: "
              << "color=" << ci.colorFormat
              << (ci.hasDepth ? (", depth=" + std::to_string(ci.depthFormat)) : "") << std::endl;

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

    std::cout << "[RenderPassCacher::Create] VkRenderPass created: "
              << reinterpret_cast<uint64_t>(wrapper->renderPass) << std::endl;

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
    std::cout << "[RenderPassCacher::SerializeToFile] Serializing " << m_entries.size() << " render pass configs to " << path << std::endl;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cout << "[RenderPassCacher::SerializeToFile] Failed to open file for writing" << std::endl;
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

    std::cout << "[RenderPassCacher::SerializeToFile] Serialization complete" << std::endl;
    return true;
}

bool RenderPassCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    std::cout << "[RenderPassCacher::DeserializeFromFile] Deserializing render pass configs from " << path << std::endl;

    if (!std::filesystem::exists(path)) {
        std::cout << "[RenderPassCacher::DeserializeFromFile] Cache file does not exist" << std::endl;
        return false;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cout << "[RenderPassCacher::DeserializeFromFile] Failed to open file for reading" << std::endl;
        return false;
    }

    // Read entry count
    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

    std::cout << "[RenderPassCacher::DeserializeFromFile] Loading " << count << " render pass metadata entries" << std::endl;

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
        std::cout << "[RenderPassCacher::DeserializeFromFile] Loaded metadata for key " << key
                  << " (color=" << colorFormat << ", depth=" << depthFormat << ")" << std::endl;
    }

    std::cout << "[RenderPassCacher::DeserializeFromFile] Deserialization complete (handles will be created on-demand)" << std::endl;
    return true;
}

} // namespace CashSystem
