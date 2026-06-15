#include "Nodes/PickingNode.h"
#include "Core/NodeLogging.h"
#include "InputEvents.h"
#include "VulkanDevice.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <cstring>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

namespace {
// Local memory-type finder. Defined in an anonymous namespace (not an inline helper header) to
// avoid emitting an inline-function COMDAT into this TU that conflicts with the same inline
// instantiated elsewhere — same precaution PickIdTargetNode/InstanceBufferNode take.
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

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> PickingNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<PickingNode>(instanceName, const_cast<PickingNodeType*>(this));
}

// ============================================================================
// PICKING NODE IMPLEMENTATION
// ============================================================================

PickingNode::PickingNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<PickingNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("[PickingNode] constructor");
}

void PickingNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[PickingNode] setup");
    lastLeftDown_ = false;
    lastPickHit_ = 0;
}

void PickingNode::CompileImpl(TypedCompileContext& ctx) {
    // Cache the compile-stable Dependency handles (the ID-image ring, device and command pool are
    // valid for the cached scene's lifetime). The single-texel readback copy is recorded on a click
    // edge in ExecuteImpl using these; the staging buffer is created lazily on first use.
    device_      = ctx.In(PickingNodeConfig::VULKAN_DEVICE);
    commandPool_ = ctx.In(PickingNodeConfig::COMMAND_POOL);
    idImage_     = ctx.In(PickingNodeConfig::ID_IMAGE);

    const bool ready = device_ && commandPool_ != VK_NULL_HANDLE && idImage_ != VK_NULL_HANDLE;
    NODE_LOG_INFO(std::string("[PickingNode] compile: GPU readback ") +
                  (ready ? "ready (device+pool+ID image acquired)" : "INERT (missing device/pool/ID image)"));
}

void PickingNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Pull per-frame inputs (mirror CameraNode's pull pattern). Guard nulls.
    InputStatePtr input = ctx.In(PickingNodeConfig::INPUT_STATE);
    const uint32_t width  = ctx.In(PickingNodeConfig::VIEWPORT_WIDTH);
    const uint32_t height = ctx.In(PickingNodeConfig::VIEWPORT_HEIGHT);

    // Always publish the current status output, even on early-out frames.
    ctx.Out(PickingNodeConfig::LAST_PICK_HIT, lastPickHit_);

    if (!input) {
        return;  // no input state this frame
    }

    // Edge-detect the left-button press: fire only on the down-edge.
    const bool leftDown = input->mouseButtons[0];
    const bool pressedThisFrame = leftDown && !lastLeftDown_;
    lastLeftDown_ = leftDown;

    if (!pressedThisFrame) {
        return;  // cheap: only read back the ID buffer on a click edge
    }

    // From here on we are handling a genuine click. CROSSHAIR pick: the app runs with the cursor
    // locked to screen center (GLFW cursor-disabled, FPS-style look), so input->mousePosition is a
    // virtual/accumulating value, NOT a usable screen pixel — read the CENTER texel of the ID image
    // instead (aim the camera at a voxel and click). A future RTS-style mode would release the
    // cursor and use mousePosition; that belongs in the ISelectable/SelectContext redesign.
    const glm::vec2 screen(static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f);

    if (!device_ || commandPool_ == VK_NULL_HANDLE || idImage_ == VK_NULL_HANDLE ||
        width == 0 || height == 0) {
        NODE_LOG_INFO("[PickingNode] click ignored — GPU readback resources or viewport not ready");
        return;
    }

    // Read back the center texel of the current frame's pick-ID image.
    //
    // Which frame's image: PickIdTargetNode re-emits images_[frameIndex % count] as ID_IMAGE each
    // Execute and binds that same slot at binding 9, so ID_IMAGE is exactly the image this frame's
    // dispatch wrote. We record our copy on a one-shot command buffer and fence IT (not
    // vkQueueWaitIdle), submitting on device->queue: the queue is FIFO, so the copy is ordered after
    // every dispatch already submitted to the queue and our fence guarantees that work has completed
    // before we map. The 4-deep ID-image ring means the slot we read is never being written
    // concurrently (it is not reused until MAX_FRAMES_IN_FLIGHT frames later), so even in the worst
    // ordering (this frame's dispatch not yet submitted) we read a complete, recent pickID — never a
    // torn write. If stale reads are ever observed under fast motion, the principled fallback is the
    // previous-completed-frame slot (CURRENT_FRAME_INDEX is wired for exactly that) — not needed so far.
    uint32_t pickID = kMissSentinel;
    if (!ReadCenterPixel(width, height, pickID)) {
        NODE_LOG_INFO("[PickingNode] click — ID readback failed (see prior error); no pick reported");
        return;
    }

    auto* bus = GetMessageBus();
    const bool hit = (pickID != kMissSentinel);

    if (hit) {
        const uint32_t brickIndex     = pickID >> kBrickIdxShift;
        const uint32_t voxelLinearIdx = pickID & kVoxelIdxMask;

        lastPickHit_ = 1;
        ctx.Out(PickingNodeConfig::LAST_PICK_HIT, lastPickHit_);

        NODE_LOG_INFO("[PickingNode] HIT pickID=" + std::to_string(pickID) +
                      " brick=" + std::to_string(brickIndex) +
                      " voxel=" + std::to_string(voxelLinearIdx) +
                      " at center=(" + std::to_string(screen.x) + ", " +
                      std::to_string(screen.y) + ")");

        if (bus) {
            // entityId carries the packed pickID for now; world position from brick/voxel is deferred
            // (needs the brick->world inverse — see design doc "Deferred"). mortonCode left 0.
            bus->Publish(std::make_unique<EventBus::PickResultEvent>(
                instanceId,
                static_cast<uint64_t>(pickID),
                /*didHit=*/true,
                /*worldPosition=*/glm::vec3(0.0f),
                /*mortonCode=*/0ull,
                screen,
                static_cast<int>(EventBus::MouseButton::Left)));
        }
    } else {
        lastPickHit_ = 0;
        ctx.Out(PickingNodeConfig::LAST_PICK_HIT, lastPickHit_);

        NODE_LOG_INFO("[PickingNode] miss (pickID=0xFFFFFFFF) at center=(" + std::to_string(screen.x) +
                      ", " + std::to_string(screen.y) + ")");

        if (bus) {
            bus->Publish(std::make_unique<EventBus::PickResultEvent>(
                instanceId,
                /*entity=*/0ull,
                /*didHit=*/false,
                glm::vec3(0.0f),
                /*morton=*/0ull,
                screen,
                static_cast<int>(EventBus::MouseButton::Left)));
        }
    }
}

bool PickingNode::EnsureStagingBuffer() {
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
        NODE_LOG_ERROR("[PickingNode] vkCreateBuffer (staging) failed");
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
        NODE_LOG_ERROR("[PickingNode] no HOST_VISIBLE|HOST_COHERENT memory type for staging buffer");
        vkDestroyBuffer(vkDevice, stagingBuffer_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = memType;

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &stagingMemory_) != VK_SUCCESS) {
        NODE_LOG_ERROR("[PickingNode] vkAllocateMemory (staging) failed");
        vkDestroyBuffer(vkDevice, stagingBuffer_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        stagingMemory_ = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(vkDevice, stagingBuffer_, stagingMemory_, 0);
    NODE_LOG_INFO("[PickingNode] staging buffer created (" + std::to_string(kStagingSize) + " bytes, host-visible)");
    return true;
}

bool PickingNode::ReadCenterPixel(uint32_t width, uint32_t height, uint32_t& pickIDOut) {
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
        NODE_LOG_ERROR("[PickingNode] vkAllocateCommandBuffers (readback) failed");
        return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        NODE_LOG_ERROR("[PickingNode] vkBeginCommandBuffer (readback) failed");
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
        NODE_LOG_ERROR("[PickingNode] vkEndCommandBuffer (readback) failed");
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
        NODE_LOG_ERROR("[PickingNode] vkCreateFence (readback) failed");
        vkFreeCommandBuffers(vkDevice, commandPool_, 1, &cmd);
        return false;
    }

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;

    if (vkQueueSubmit(device_->queue, 1, &submit, fence) != VK_SUCCESS) {
        NODE_LOG_ERROR("[PickingNode] vkQueueSubmit (readback) failed");
        vkDestroyFence(vkDevice, fence, nullptr);
        vkFreeCommandBuffers(vkDevice, commandPool_, 1, &cmd);
        return false;
    }

    vkWaitForFences(vkDevice, 1, &fence, VK_TRUE, UINT64_MAX);

    // --- Map the staging memory and read the single uint32 pickID ---
    void* mapped = nullptr;
    if (vkMapMemory(vkDevice, stagingMemory_, 0, kStagingSize, 0, &mapped) != VK_SUCCESS) {
        NODE_LOG_ERROR("[PickingNode] vkMapMemory (staging) failed");
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

void PickingNode::DestroyStagingBuffer() {
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

void PickingNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[PickingNode] cleanup");
    // Release the host-visible staging buffer (owned by this node). Reset edge/state so a recompile
    // starts clean; drop cached Dependency handles (re-acquired on the next CompileImpl).
    DestroyStagingBuffer();
    device_      = nullptr;
    commandPool_ = VK_NULL_HANDLE;
    idImage_     = VK_NULL_HANDLE;
    lastLeftDown_ = false;
    lastPickHit_ = 0;
}

} // namespace Vixen::RenderGraph
