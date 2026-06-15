#include "Selection/VoxelSelectionProvider.h"
#include "VulkanDevice.h"

#include <glm/glm.hpp>
#include <cstring>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

namespace {
// Local memory-type finder. Anonymous namespace (not an inline helper header) to avoid emitting
// an inline-function COMDAT into this TU that conflicts with the same inline instantiated
// elsewhere — the same precaution the old PickingNode/PickIdTargetNode/InstanceBufferNode take.
uint32_t FindHostVisibleMemoryType(
    const VkPhysicalDeviceMemoryProperties& memProps,
    uint32_t typeFilter,
    VkMemoryPropertyFlags required)
{
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    return UINT32_MAX;
}
} // namespace

VoxelSelectionProvider::~VoxelSelectionProvider() {
    // RAII: release the host-visible staging buffer we own (was PickingNode::CleanupImpl).
    DestroyStagingBuffer();
}

void VoxelSelectionProvider::configure(VulkanDevice* device,
                                       VkCommandPool pool,
                                       VkImage idImage) {
    // If the device changes, tear down the staging buffer first so we never free it against the
    // wrong device. On a same-device recompile we keep it (it is reused across clicks).
    if (device != device_) {
        DestroyStagingBuffer();
    }
    device_      = device;
    commandPool_ = pool;
    idImage_     = idImage;
}

std::optional<Hit> VoxelSelectionProvider::resolve(const SelectContext& ctx) {
    const uint32_t width  = ctx.viewportWidth;
    const uint32_t height = ctx.viewportHeight;

    if (!device_ || commandPool_ == VK_NULL_HANDLE || idImage_ == VK_NULL_HANDLE ||
        width == 0 || height == 0) {
        // Resources or viewport not ready — treat as a miss (no hit to report).
        return std::nullopt;
    }

    // Read back the center texel of the current frame's pick-ID image.
    //
    // Which frame's image: PickIdTargetNode re-emits images_[frameIndex % count] as ID_IMAGE each
    // Execute and binds that same slot at binding 9, so the configured idImage_ is exactly the image
    // this frame's dispatch wrote. We record our copy on a one-shot command buffer and fence IT (not
    // vkQueueWaitIdle), submitting on device->queue: the queue is FIFO, so the copy is ordered after
    // every dispatch already submitted to the queue and our fence guarantees that work has completed
    // before we map. The 4-deep ID-image ring means the slot we read is never being written
    // concurrently (it is not reused until MAX_FRAMES_IN_FLIGHT frames later), so even in the worst
    // ordering (this frame's dispatch not yet submitted) we read a complete, recent pickID — never a
    // torn write. If stale reads are ever observed under fast motion, the principled fallback is the
    // previous-completed-frame slot — not needed so far.
    uint32_t pickID = kMissSentinel;
    if (!ReadCenterPixel(width, height, pickID)) {
        // Readback failed (see prior error log) — report no hit.
        return std::nullopt;
    }

    if (pickID == kMissSentinel) {
        return std::nullopt;  // empty space under the crosshair
    }

    // Hit. payload carries the packed pickID (brick = id >> 10, voxel = id & 0x3FF). World position
    // from brick/voxel is deferred (needs the brick->world inverse — see design "Out of scope"), so
    // depth = 0 and worldPos = (0,0,0) for now.
    return Hit{ SelectionId{ ProviderKind::Voxel, static_cast<uint64_t>(pickID) },
                /*depth=*/0.0f,
                /*worldPos=*/glm::vec3(0.0f) };
}

bool VoxelSelectionProvider::EnsureStagingBuffer() {
    if (stagingBuffer_ != VK_NULL_HANDLE) {
        return true;  // already created — reused across clicks
    }
    if (!device_) {
        return false;
    }
    VkDevice vkDevice = device_->device;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size        = kStagingSize;
    bufInfo.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkDevice, &bufInfo, nullptr, &stagingBuffer_) != VK_SUCCESS) {
        stagingBuffer_ = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vkDevice, stagingBuffer_, &req);

    const uint32_t memType = FindHostVisibleMemoryType(
        device_->gpuMemoryProperties,
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memType == UINT32_MAX) {
        vkDestroyBuffer(vkDevice, stagingBuffer_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = memType;

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &stagingMemory_) != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, stagingBuffer_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        stagingMemory_ = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(vkDevice, stagingBuffer_, stagingMemory_, 0);
    return true;
}

bool VoxelSelectionProvider::ReadCenterPixel(uint32_t width, uint32_t height, uint32_t& pickIDOut) {
    if (!EnsureStagingBuffer()) {
        return false;
    }
    VkDevice vkDevice = device_->device;

    // --- One-shot command buffer from the (graph-owned) command pool ---
    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool        = commandPool_;
    cbAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDevice, &cbAlloc, &cmd) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool_, 1, &cmd);
        return false;
    }

    // Copy exactly the center texel. The ID image is already in VK_IMAGE_LAYOUT_GENERAL (storage
    // image, transitioned once by PickIdTargetNode and kept there), which is valid for TRANSFER_SRC.
    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                  = 0;  // tightly packed (single texel)
    region.bufferImageHeight                = 0;
    region.imageSubresource.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel        = 0;
    region.imageSubresource.baseArrayLayer  = 0;
    region.imageSubresource.layerCount      = 1;
    region.imageOffset                      = { static_cast<int32_t>(width / 2),
                                                static_cast<int32_t>(height / 2), 0 };
    region.imageExtent                      = { 1, 1, 1 };

    vkCmdCopyImageToBuffer(cmd, idImage_, VK_IMAGE_LAYOUT_GENERAL, stagingBuffer_, 1, &region);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool_, 1, &cmd);
        return false;
    }

    // --- Submit on device->queue with a fresh fence, then wait it (NOT vkQueueWaitIdle, which would
    // stall the whole queue including the render loop's submits). A click is infrequent, so a short
    // CPU stall here is fine. ---
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(vkDevice, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool_, 1, &cmd);
        return false;
    }

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;

    if (vkQueueSubmit(device_->queue, 1, &submit, fence) != VK_SUCCESS) {
        vkDestroyFence(vkDevice, fence, nullptr);
        vkFreeCommandBuffers(vkDevice, commandPool_, 1, &cmd);
        return false;
    }

    vkWaitForFences(vkDevice, 1, &fence, VK_TRUE, UINT64_MAX);

    // --- Map the staging memory and read the single uint32 pickID ---
    void* mapped = nullptr;
    if (vkMapMemory(vkDevice, stagingMemory_, 0, kStagingSize, 0, &mapped) != VK_SUCCESS) {
        vkDestroyFence(vkDevice, fence, nullptr);
        vkFreeCommandBuffers(vkDevice, commandPool_, 1, &cmd);
        return false;
    }
    std::memcpy(&pickIDOut, mapped, sizeof(uint32_t));
    vkUnmapMemory(vkDevice, stagingMemory_);

    // Free the one-shot cmd buffer + fence (staging buffer is kept for reuse).
    vkDestroyFence(vkDevice, fence, nullptr);
    vkFreeCommandBuffers(vkDevice, commandPool_, 1, &cmd);
    return true;
}

void VoxelSelectionProvider::DestroyStagingBuffer() {
    if (!device_) {
        return;
    }
    VkDevice vkDevice = device_->device;
    if (stagingBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkDevice, stagingBuffer_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
    }
    if (stagingMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, stagingMemory_, nullptr);
        stagingMemory_ = VK_NULL_HANDLE;
    }
}

} // namespace Vixen::RenderGraph
