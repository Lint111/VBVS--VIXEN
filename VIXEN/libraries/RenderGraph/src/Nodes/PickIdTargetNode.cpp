#include "Nodes/PickIdTargetNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include <stdexcept>

// Frames-in-flight for the ID-image ring. Matches FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT (= 4),
// mirroring RenderTargetNode's DEFAULT_FRAMES_IN_FLIGHT. One ID image per in-flight frame so a
// click's P2 readback copy never races a later frame's compute write (ring, per the design doc).
static constexpr uint32_t PICK_ID_FRAMES_IN_FLIGHT = 4;

// Local helper — mirrors RenderGraph::NodeHelpers::FindMemoryType from BufferHelpers.h but defined
// here to avoid emitting an inline-function COMDAT into this TU that conflicts when the same inline
// is instantiated in other TUs with different COMDAT selection (see InstanceBufferNode.cpp).
static uint32_t FindSuitableMemoryType(
    const VkPhysicalDeviceMemoryProperties& memProps,
    uint32_t typeFilter,
    VkMemoryPropertyFlags required,
    const char* context)
{
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    throw std::runtime_error(
        std::string("[PickIdTargetNode] No suitable memory type found for ") + context);
}

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ====== PickIdTargetNodeType ======

std::unique_ptr<NodeInstance> PickIdTargetNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<PickIdTargetNode>(n, const_cast<PickIdTargetNodeType*>(this));
}

// ====== PickIdTargetNode ======

PickIdTargetNode::PickIdTargetNode(const std::string& n, NodeType* t)
    : TypedNode<PickIdTargetNodeConfig>(n, t)
{
}

void PickIdTargetNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access).
    NODE_LOG_DEBUG("[PickIdTargetNode] Setup (graph-scope initialization)");
}

void PickIdTargetNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[PickIdTargetNode] Compile START");

    device_ = ctx.In(PickIdTargetNodeConfig::VULKAN_DEVICE_IN);
    if (!device_) {
        throw std::runtime_error("[PickIdTargetNode] VULKAN_DEVICE_IN is null");
    }

    VkCommandPool commandPool = ctx.In(PickIdTargetNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[PickIdTargetNode] COMMAND_POOL is null");
    }

    width_  = ctx.In(PickIdTargetNodeConfig::WIDTH);
    height_ = ctx.In(PickIdTargetNodeConfig::HEIGHT);
    if (width_ == 0 || height_ == 0) {
        throw std::runtime_error("[PickIdTargetNode] WIDTH/HEIGHT must be > 0 (got " +
                                 std::to_string(width_) + "x" + std::to_string(height_) + ")");
    }

    // FR-7: images are persistent across recompile — only create once. (A future
    // followSwapchainExtent mode would recreate here when the extent changes.)
    if (images_.empty()) {
        imageCount_ = PICK_ID_FRAMES_IN_FLIGHT;
        CreateImages(device_, commandPool);
    } else {
        NODE_LOG_INFO("[PickIdTargetNode] Reusing persistent pick-ID images across recompile");
    }

    currentIndex_ = 0;

    // Publish the current frame's view/image so the descriptor gatherer can pick them up.
    ctx.Out(PickIdTargetNodeConfig::ID_IMAGE_VIEW, images_[currentIndex_].view);
    ctx.Out(PickIdTargetNodeConfig::ID_IMAGE,      images_[currentIndex_].image);

    NODE_LOG_INFO("[PickIdTargetNode] Outputs published (" + std::to_string(width_) + "x" +
                  std::to_string(height_) + ", " + std::to_string(imageCount_) + " R32_UINT images)");
}

void PickIdTargetNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Ring: select this frame's image by the engine frame-in-flight index, then re-emit its
    // view/image (mirrors how producer nodes re-publish their current buffer each Execute, so the
    // DescriptorResourceGatherer refreshes binding 9 with the right view).
    const uint32_t frameIndex = ctx.In(PickIdTargetNodeConfig::CURRENT_FRAME_INDEX);
    if (imageCount_ > 0) {
        currentIndex_ = frameIndex % imageCount_;
    }

    ctx.Out(PickIdTargetNodeConfig::ID_IMAGE_VIEW, images_[currentIndex_].view);
    ctx.Out(PickIdTargetNodeConfig::ID_IMAGE,      images_[currentIndex_].image);
}

void PickIdTargetNode::CleanupImpl(TypedCleanupContext& ctx) {
    // FR-7: persist across recompile; release only on final application teardown.
    if (ctx.reason != CleanupReason::FinalTeardown) {
        NODE_LOG_INFO("[PickIdTargetNode] Cleanup (recompile) - keeping persistent pick-ID images");
        return;
    }

    NODE_LOG_INFO("[PickIdTargetNode] Cleanup (final teardown) - destroying pick-ID images");
    DestroyImages();
}

void PickIdTargetNode::CreateImages(VulkanDevice* device, VkCommandPool commandPool) {
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    images_.resize(imageCount_);

    for (auto& img : images_) {
        // --- Create R32_UINT storage image ---
        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = kFormat;  // VK_FORMAT_R32_UINT
        imgInfo.extent        = {width_, height_, 1u};
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // STORAGE for the compute write at binding 9; TRANSFER_SRC for the P2 click-readback copy.
        imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(vkDevice, &imgInfo, nullptr, &img.image) != VK_SUCCESS) {
            throw std::runtime_error("[PickIdTargetNode] vkCreateImage failed");
        }

        // --- Allocate device-local memory via real memory-type selection ---
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(vkDevice, img.image, &req);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = req.size;
        allocInfo.memoryTypeIndex = FindSuitableMemoryType(
            memProps,
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "PickIdTargetNode R32_UINT image"
        );

        if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &img.memory) != VK_SUCCESS) {
            vkDestroyImage(vkDevice, img.image, nullptr);
            img.image = VK_NULL_HANDLE;
            throw std::runtime_error("[PickIdTargetNode] vkAllocateMemory failed");
        }

        vkBindImageMemory(vkDevice, img.image, img.memory, 0);

        // --- Create image view ---
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image            = img.image;
        viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format           = kFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        if (vkCreateImageView(vkDevice, &viewInfo, nullptr, &img.view) != VK_SUCCESS) {
            throw std::runtime_error("[PickIdTargetNode] vkCreateImageView failed");
        }
    }

    // One-time UNDEFINED -> GENERAL transition for every ring image. Storage images stay GENERAL
    // across dispatches, so this single transition makes the descriptor (always GENERAL) correct
    // for every frame — no per-frame barrier required.
    TransitionAllToGeneral(commandPool);

    NODE_LOG_INFO("[PickIdTargetNode] Created " + std::to_string(imageCount_) +
                  " R32_UINT storage images at " + std::to_string(width_) + "x" +
                  std::to_string(height_) + " (transitioned UNDEFINED->GENERAL)");
}

void PickIdTargetNode::TransitionAllToGeneral(VkCommandPool commandPool) {
    VkDevice vkDevice = device_->device;

    // One-shot command buffer for the layout transitions.
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDevice, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("[PickIdTargetNode] vkAllocateCommandBuffers (transition) failed");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
        throw std::runtime_error("[PickIdTargetNode] vkBeginCommandBuffer (transition) failed");
    }

    std::vector<VkImageMemoryBarrier> barriers;
    barriers.reserve(images_.size());
    for (const auto& img : images_) {
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = img.image;
        barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barriers.push_back(barrier);
    }

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(barriers.size()), barriers.data()
    );

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
        throw std::runtime_error("[PickIdTargetNode] vkEndCommandBuffer (transition) failed");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    // Submit on the device queue and wait — this runs once at Compile, before any dispatch.
    if (vkQueueSubmit(device_->queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
        throw std::runtime_error("[PickIdTargetNode] vkQueueSubmit (transition) failed");
    }
    vkQueueWaitIdle(device_->queue);

    vkFreeCommandBuffers(vkDevice, commandPool, 1, &cmd);
}

void PickIdTargetNode::DestroyImages() {
    if (!device_) return;
    VkDevice vkDevice = device_->device;

    for (auto& img : images_) {
        if (img.view   != VK_NULL_HANDLE) { vkDestroyImageView(vkDevice, img.view,   nullptr); img.view   = VK_NULL_HANDLE; }
        if (img.image  != VK_NULL_HANDLE) { vkDestroyImage    (vkDevice, img.image,  nullptr); img.image  = VK_NULL_HANDLE; }
        if (img.memory != VK_NULL_HANDLE) { vkFreeMemory      (vkDevice, img.memory, nullptr); img.memory = VK_NULL_HANDLE; }
    }
    images_.clear();

    NODE_LOG_INFO("[PickIdTargetNode] Pick-ID images destroyed");
}

} // namespace Vixen::RenderGraph
