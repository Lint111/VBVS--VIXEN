#include "Nodes/VoxelSelectionProviderNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "Selection/SelectionCandidate.h"
#include "InputEvents.h"
#include "VulkanDevice.h"
#include "Core/GpuFault.h"
#include "Core/RenderGraph.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

namespace {
// Local memory-type finder. Anonymous namespace (not an inline helper header) to avoid emitting
// an inline-function COMDAT into this TU that conflicts with the same inline instantiated
// elsewhere — the same precaution the old VoxelSelectionProvider/PickIdTargetNode take.
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

bool CreateHostBuffer(VulkanDevice* device, VkDeviceSize bytes, VkBuffer& buffer, VkDeviceMemory& memory) {
    VkDevice vkDevice = device->device;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size        = bytes;
    bufInfo.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vkDevice, &bufInfo, nullptr, &buffer) != VK_SUCCESS) {
        buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vkDevice, buffer, &req);
    const uint32_t memType = FindHostVisibleMemoryType(
        device->gpuMemoryProperties,
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memType == UINT32_MAX) {
        vkDestroyBuffer(vkDevice, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = memType;
    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(vkDevice, buffer, memory, 0);
    return true;
}

// Window pixels -> texel of the render-extent pick image.
glm::uvec2 ToRenderTexel(const glm::vec2& windowPos, const glm::vec2& windowExtent, const glm::uvec2& renderExtent) {
    const glm::vec2 scaled = windowPos * glm::vec2(renderExtent) / windowExtent;
    return glm::uvec2(glm::clamp(scaled, glm::vec2(0.0f), glm::vec2(renderExtent) - 1.0f));
}
} // namespace

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> VoxelSelectionProviderNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<VoxelSelectionProviderNode>(
        instanceName, const_cast<VoxelSelectionProviderNodeType*>(this));
}

// ============================================================================
// VOXEL SELECTION PROVIDER NODE IMPLEMENTATION
// ============================================================================

VoxelSelectionProviderNode::VoxelSelectionProviderNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<VoxelSelectionProviderNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("[VoxelSelectionProvider] constructor");
}

void VoxelSelectionProviderNode::TypedSetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[VoxelSelectionProvider] setup");
    // Provider layer priority (world layer = 0 by default). Read once at graph-scope setup.
    priority_ = GetParameterValue<int>(VoxelSelectionProviderNodeConfig::PARAM_PRIORITY, 0);
}

void VoxelSelectionProviderNode::TypedCompileImpl(TypedCompileContext& ctx) {
    // Cache the compile-stable Dependency handles (the ID-image ring, device and command pool are
    // valid for the cached scene's lifetime). The device lives in the base NodeInstance (SetDevice
    // now, GetDevice() in Execute/Cleanup) per the device convention — no private device member.
    VulkanDevice* device = ctx.In(VoxelSelectionProviderNodeConfig::VULKAN_DEVICE);

    // If the device changes on recompile, tear down the staging buffer first so we never free it
    // against the wrong device. On a same-device recompile we keep it (it is reused across clicks).
    if (device != GetDevice()) {
        DestroyReadbackSlots();
        DestroyStagingBuffer();
    }
    SetDevice(device);
    commandPool_ = ctx.In(VoxelSelectionProviderNodeConfig::COMMAND_POOL);

    // KI-012: cache once per Compile, not re-queried per click.
    requiresFullImageTransfers_ = GetDevice() && GetDevice()->RequiresFullImageTransfers();

    ResetTracking();
    previousFrame_ = {};

    const bool ready = GetDevice() && commandPool_ != VK_NULL_HANDLE;
    NODE_LOG_INFO(std::string("[VoxelSelectionProvider] compile: priority=") +
                  std::to_string(priority_) + "; " +
                  (ready ? "configured (device+pool acquired)"
                         : "INERT (missing device/pool)"));
}

void VoxelSelectionProviderNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    // A provider emits a candidate EVERY frame so the coordinator's accumulation slot always has a
    // fresh value from this source. Default = miss; only a click-edge hit overwrites it.
    SelectionCandidate candidate{};
    candidate.hit      = false;
    candidate.id       = kInvalidSelectionId;
    candidate.depth    = 0.0f;
    candidate.priority = priority_;
    candidate.worldPos = glm::vec3(0.0f);

    InputStatePtr input = ctx.In(VoxelSelectionProviderNodeConfig::INPUT_STATE);
    if (!input) {
        ctx.Out(VoxelSelectionProviderNodeConfig::CANDIDATE, candidate);
        return;  // no input state this frame
    }

    const uint32_t width  = ctx.In(VoxelSelectionProviderNodeConfig::VIEWPORT_WIDTH);
    const uint32_t height = ctx.In(VoxelSelectionProviderNodeConfig::VIEWPORT_HEIGHT);
    uint32_t windowWidth  = ctx.In(VoxelSelectionProviderNodeConfig::PRESENTATION_WIDTH);
    uint32_t windowHeight = ctx.In(VoxelSelectionProviderNodeConfig::PRESENTATION_HEIGHT);
    if (windowWidth == 0 || windowHeight == 0) {  // not wired: window and render extents coincide
        windowWidth  = width;
        windowHeight = height;
    }
    const glm::vec2 jitter = ctx.In(VoxelSelectionProviderNodeConfig::JITTER);
    const bool jittered = jitter != glm::vec2(0.0f);
    const bool resourcesReady = GetDevice() && commandPool_ != VK_NULL_HANDLE && width != 0 && height != 0;
    const glm::vec2 windowExtent(float(std::max(windowWidth, 1u)), float(std::max(windowHeight, 1u)));
    const glm::uvec2 renderExtent(width, height);

    // The vote is only per-frame when sub-pixel jitter makes one frame's hit unrepresentative.
    const bool tracking = jittered && resourcesReady;
    RetireReadbacks(tracking, glm::vec2(renderExtent));
    if (!tracking) {
        ResetTracking();
    } else {
        if (trackedExtent_ != renderExtent) {
            ResetTracking();
            trackedExtent_ = renderExtent;
        }
        trackedTexel_[kCrosshair] = renderExtent / 2u;
        const glm::uvec2 cursor = ToRenderTexel(input->mousePosition, windowExtent, renderExtent);
        if (cursor != trackedTexel_[kCursor]) history_[kCursor].Reset();
        trackedTexel_[kCursor] = cursor;
        if (previousFrame_.jittered && previousFrame_.pick != VK_NULL_HANDLE) SubmitTrackingReadback(previousFrame_);
    }

    // The pick image the frame's own trace writes is complete once that frame is queued, which is
    // one Execute from now; the snapshot is what the next frame copies.
    const VkImage pickImage = ctx.In(VoxelSelectionProviderNodeConfig::ID_IMAGE);
    const VkImage settledPick = previousFrame_.pick != VK_NULL_HANDLE ? previousFrame_.pick : pickImage;
    FrameSnapshot frame;
    frame.pick = pickImage;
    frame.jitter = jitter;
    frame.jittered = tracking;
    if (tracking) {
        frame.motion = ctx.In(VoxelSelectionProviderNodeConfig::MOTION_TARGET);
        frame.validity = ctx.In(VoxelSelectionProviderNodeConfig::VALIDITY_TARGET);
        if (frame.motion) frame.motionIndex = frame.motion->GetCurrentIndex();
        if (frame.validity) frame.validityIndex = frame.validity->GetCurrentIndex();
    }
    previousFrame_ = frame;

    // Find the left-button press entry (input-rework slice 1 M3: the shared click list replaces
    // the old private lastLeftDown_ edge detector). Several presses in one frame: the LAST one
    // wins — matches the old single-poll semantics (only whether/where a press happened this
    // frame matters here). x/y are the cursor position AT that press (ClickEvent's own
    // fold-order field), so the pick follows the actual click position, not a fixed crosshair.
    bool pressedThisFrame = false;
    float clickX = 0.0f, clickY = 0.0f;
    for (const ClickEvent& click : input->clicksThisFrame) {
        if (click.button == static_cast<int>(EventBus::MouseButton::Left) && click.pressed) {
            pressedThisFrame = true;
            clickX = click.x;
            clickY = click.y;
        }
    }

    if (!pressedThisFrame) {
        ctx.Out(VoxelSelectionProviderNodeConfig::CANDIDATE, candidate);
        return;  // cheap: only read back on a click edge
    }

    if (!resourcesReady || settledPick == VK_NULL_HANDLE) {
        // Resources or viewport not ready — emit a miss (never crash).
        NODE_LOG_INFO("[VoxelSelectionProvider] click ignored — resources/viewport not ready");
        ctx.Out(VoxelSelectionProviderNodeConfig::CANDIDATE, candidate);
        return;
    }

    // The clicked texel in the render-extent pick image: window pixels scale to render texels
    // (render scale < 1), and a click can land at the exact edge or just outside during a resize race.
    const glm::uvec2 clickTexel = ToRenderTexel(glm::vec2(clickX, clickY), windowExtent, renderExtent);
    const uint32_t targetX = clickTexel.x;
    const uint32_t targetY = clickTexel.y;

    PickTargetPixel pixel{kMissSentinel, kMissSentinel};
    std::string how = "single read";
    bool read = false;
    if (tracking && clickTexel == trackedTexel_[kCursor] && history_[kCursor].Count() > 0) {
        // The cursor texel has been voting every frame; the click resolves from that vote.
        const PickVoteResult vote = history_[kCursor].Winner();
        pixel = {vote.address, vote.instance};
        how = "vote samples=" + std::to_string(vote.sampleCount) + " weight=" + std::to_string(vote.weight) +
              (vote.unanimous ? " unanimous" : " split");
        read = true;
    } else {
        // One-shot fenced copy of the last settled frame's pick image: no vote history exists for a
        // texel that was not being tracked (or there is no jitter), so this is the single current hit.
        read = ReadPixelAt(settledPick, width, height, targetX, targetY, pixel);
    }
    if (!read || pixel.address == kMissSentinel) {
        // Readback failed (see prior error log) or empty space under the cursor — report a miss.
        NODE_LOG_INFO("[VoxelSelectionProvider] click — miss (empty space under cursor) pixel=(" +
                      std::to_string(targetX) + "," + std::to_string(targetY) + ") " + how);
        ctx.Out(VoxelSelectionProviderNodeConfig::CANDIDATE, candidate);
        return;
    }

    // Hit. payload carries the packed pickID (brick = id >> 10, voxel = id & 0x3FF). World position
    // from brick/voxel is deferred (needs the brick->world inverse — see design "Out of scope"), so
    // depth = 0 and worldPos = (0,0,0) for now. A sentinel in channel G represents no instance
    // (legacy non-instanced raymarch); expose that as an empty optional, never as an id.
    candidate.hit      = true;
    candidate.id       = SelectionId{ ProviderKind::Voxel, static_cast<uint64_t>(pixel.address) };
    if (pixel.instanceIndex != kMissSentinel) {
        candidate.instanceIndex = pixel.instanceIndex;
    }
    candidate.depth    = 0.0f;
    candidate.worldPos = glm::vec3(0.0f);

    const uint32_t brickIndex     = pixel.address >> kBrickIdxShift;
    const uint32_t voxelLinearIdx = pixel.address & kVoxelIdxMask;
    NODE_LOG_INFO("[VoxelSelectionProvider] click — HIT pickID=" + std::to_string(pixel.address) +
                  " brick=" + std::to_string(brickIndex) +
                  " voxel=" + std::to_string(voxelLinearIdx) +
                  " instance=" + (candidate.instanceIndex ? std::to_string(*candidate.instanceIndex) : "none") +
                  " priority=" + std::to_string(priority_) +
                  " pixel=(" + std::to_string(targetX) + "," + std::to_string(targetY) + ") " + how);

    ctx.Out(VoxelSelectionProviderNodeConfig::CANDIDATE, candidate);
}

bool VoxelSelectionProviderNode::EnsureReadbackSlots() {
    VulkanDevice* device = GetDevice();
    if (!device) return false;
    const VkDeviceSize bytes = requiresFullImageTransfers_
        ? kTrackedTexelBytes * trackedExtent_.x * trackedExtent_.y
        : kTrackedTexelBytes * kTrackedTexels;
    for (ReadbackSlot& slot : slots_) {
        if (slot.buffer != VK_NULL_HANDLE && slot.capacity == bytes) continue;
        if (slot.inFlight) return false;  // still reading the old size: retry once it retires
        if (slot.buffer != VK_NULL_HANDLE) {
            vkUnmapMemory(device->device, slot.memory);
            vkDestroyBuffer(device->device, slot.buffer, nullptr);
            vkFreeMemory(device->device, slot.memory, nullptr);
            slot.buffer = VK_NULL_HANDLE;
            slot.memory = VK_NULL_HANDLE;
            slot.mapped = nullptr;
        }
        if (!CreateHostBuffer(device, bytes, slot.buffer, slot.memory) ||
            vkMapMemory(device->device, slot.memory, 0, VK_WHOLE_SIZE, 0, &slot.mapped) != VK_SUCCESS) {
            return false;
        }
        slot.capacity = bytes;
        if (slot.fence != VK_NULL_HANDLE) continue;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(device->device, &fenceInfo, nullptr, &slot.fence) != VK_SUCCESS) return false;
    }
    return true;
}

void VoxelSelectionProviderNode::DestroyReadbackSlots() {
    VulkanDevice* device = GetDevice();
    for (ReadbackSlot& slot : slots_) {
        if (device) {
            if (slot.inFlight) {
                // A submission that never finishes stays with device teardown; freeing its resources now would race it.
                if (!BoundedWaitFence(device->device, slot.fence).Ok()) { slot = {}; continue; }
                vkFreeCommandBuffers(device->device, commandPool_, 1, &slot.cmd);
            }
            if (slot.fence != VK_NULL_HANDLE) vkDestroyFence(device->device, slot.fence, nullptr);
            if (slot.mapped) vkUnmapMemory(device->device, slot.memory);
            if (slot.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device->device, slot.buffer, nullptr);
            if (slot.memory != VK_NULL_HANDLE) vkFreeMemory(device->device, slot.memory, nullptr);
        }
        slot = {};
    }
    nextSlot_ = 0;
    retireSlot_ = 0;
}

void VoxelSelectionProviderNode::ResetTracking() {
    for (PickVoteHistory& history : history_) history.Reset();
    trackedTexel_ = {};
    trackedExtent_ = glm::uvec2(0u);
}

void VoxelSelectionProviderNode::RetireReadbacks(bool observe, const glm::vec2& extent) {
    VulkanDevice* device = GetDevice();
    if (!device) return;
    for (uint32_t n = 0; n < kReadbackSlots; ++n) {
        ReadbackSlot& slot = slots_[retireSlot_];
        if (!slot.inFlight) return;
        if (vkGetFenceStatus(device->device, slot.fence) != VK_SUCCESS) return;  // not finished (or lost): later Executes retry

        if (observe) {
            for (uint32_t t = 0; t < kTrackedTexels; ++t) {
                if (slot.texel[t] != trackedTexel_[t]) continue;  // the tracked pixel moved since this was queued
                float values[5];
                const uint8_t* base = static_cast<const uint8_t*>(slot.mapped);
                if (requiresFullImageTransfers_) {
                    const size_t texels = size_t(slot.extent.x) * slot.extent.y;
                    const size_t index = size_t(slot.texel[t].y) * slot.extent.x + slot.texel[t].x;
                    std::memcpy(values, base + index * 8u, 8u);
                    std::memcpy(values + 2, base + texels * 8u + index * 8u, 8u);
                    std::memcpy(values + 4, base + texels * 16u + index * 4u, 4u);
                } else {
                    std::memcpy(values, base + t * kTrackedTexelBytes, sizeof(values));
                }
                uint32_t ids[2];
                std::memcpy(ids, values, sizeof(ids));
                const bool hit = ids[0] != kPickMissSentinel;
                const PickMotion motion = slot.motionRead
                    ? ClassifyPickMotion(hit, glm::vec2(values[2], values[3]), values[4], extent)
                    : PickMotion::Unknown;
                history_[t].Observe({ids[0], ids[1], slot.jitter}, motion, true);
            }
        }
        vkResetFences(device->device, 1, &slot.fence);
        vkFreeCommandBuffers(device->device, commandPool_, 1, &slot.cmd);
        slot.cmd = VK_NULL_HANDLE;
        slot.inFlight = false;
        retireSlot_ = (retireSlot_ + 1u) % kReadbackSlots;
    }
}

void VoxelSelectionProviderNode::SubmitTrackingReadback(const FrameSnapshot& frame) {
    VulkanDevice* device = GetDevice();
    ReadbackSlot& slot = slots_[nextSlot_];
    if (slot.inFlight) return;  // the GPU is more than a ring behind: drop this sample
    if (!EnsureReadbackSlots()) return;

    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool        = commandPool_;
    cbAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device->device, &cbAlloc, &cmd) != VK_SUCCESS) return;

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        vkFreeCommandBuffers(device->device, commandPool_, 1, &cmd);
        return;
    }

    // Motion and validity are only read while their recorded layout is GENERAL (the storage-write layout).
    const bool motionReadable =
        frame.motion && frame.validity &&
        frame.motion->GetImageLayout(frame.motionIndex) == VK_IMAGE_LAYOUT_GENERAL &&
        frame.validity->GetImageLayout(frame.validityIndex) == VK_IMAGE_LAYOUT_GENERAL;
    const VkImage motionImage = motionReadable ? frame.motion->GetImage(frame.motionIndex) : VK_NULL_HANDLE;
    const VkImage validityImage = motionReadable ? frame.validity->GetImage(frame.validityIndex) : VK_NULL_HANDLE;

    std::array<VkImageMemoryBarrier, 3> toCopy{};
    uint32_t barrierCount = 0;
    for (VkImage image : {frame.pick, motionImage, validityImage}) {
        if (image == VK_NULL_HANDLE) continue;
        VkImageMemoryBarrier& barrier = toCopy[barrierCount++];
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, barrierCount, toCopy.data());

    if (requiresFullImageTransfers_) {
        const VkDeviceSize texels = VkDeviceSize(trackedExtent_.x) * trackedExtent_.y;
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {trackedExtent_.x, trackedExtent_.y, 1};
        vkCmdCopyImageToBuffer(cmd, frame.pick, VK_IMAGE_LAYOUT_GENERAL, slot.buffer, 1, &region);
        if (motionReadable) {
            region.bufferOffset = texels * 8u;
            vkCmdCopyImageToBuffer(cmd, motionImage, VK_IMAGE_LAYOUT_GENERAL, slot.buffer, 1, &region);
            region.bufferOffset = texels * 16u;
            vkCmdCopyImageToBuffer(cmd, validityImage, VK_IMAGE_LAYOUT_GENERAL, slot.buffer, 1, &region);
        }
    } else {
        for (uint32_t t = 0; t < kTrackedTexels; ++t) {
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageOffset = {static_cast<int32_t>(trackedTexel_[t].x), static_cast<int32_t>(trackedTexel_[t].y), 0};
            region.imageExtent = {1, 1, 1};
            region.bufferOffset = t * kTrackedTexelBytes;
            vkCmdCopyImageToBuffer(cmd, frame.pick, VK_IMAGE_LAYOUT_GENERAL, slot.buffer, 1, &region);
            if (motionReadable) {
                region.bufferOffset = t * kTrackedTexelBytes + 8u;
                vkCmdCopyImageToBuffer(cmd, motionImage, VK_IMAGE_LAYOUT_GENERAL, slot.buffer, 1, &region);
                region.bufferOffset = t * kTrackedTexelBytes + 16u;
                vkCmdCopyImageToBuffer(cmd, validityImage, VK_IMAGE_LAYOUT_GENERAL, slot.buffer, 1, &region);
            }
        }
    }

    // Execution dependency for later submissions: the next trace may overwrite the images only after these reads.
    VkMemoryBarrier readsDone{};
    readsDone.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 1, &readsDone, 0, nullptr, 0, nullptr);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(device->device, commandPool_, 1, &cmd);
        return;
    }
    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    {
        std::lock_guard submitLock(device->SubmitMutex(device->queue));
        if (vkQueueSubmit(device->queue, 1, &submit, slot.fence) != VK_SUCCESS) {
            vkFreeCommandBuffers(device->device, commandPool_, 1, &cmd);
            return;
        }
    }
    slot.cmd = cmd;
    slot.inFlight = true;
    slot.motionRead = motionReadable;
    slot.extent = trackedExtent_;
    slot.jitter = frame.jitter;
    slot.texel = trackedTexel_;
    nextSlot_ = (nextSlot_ + 1u) % kReadbackSlots;
}

// ----------------------------------------------------------------------------
// GPU readback helpers (MOVED verbatim from the old C++ VoxelSelectionProvider;
// device_ → GetDevice() per the base-NodeInstance device convention).
// ----------------------------------------------------------------------------

bool VoxelSelectionProviderNode::EnsureStagingBuffer(VkDeviceSize bytesNeeded) {
    if (stagingBuffer_ != VK_NULL_HANDLE && stagingCapacity_ >= bytesNeeded) {
        return true;  // already created and large enough — reused across clicks
    }
    VulkanDevice* device = GetDevice();
    if (!device) {
        return false;
    }

    // Grown (or first-ever) allocation: drop the old one first (idempotent if none exists).
    DestroyStagingBuffer();
    if (!CreateHostBuffer(device, bytesNeeded, stagingBuffer_, stagingMemory_)) {
        return false;
    }
    stagingCapacity_ = bytesNeeded;
    return true;
}

bool VoxelSelectionProviderNode::ReadPixelAt(VkImage image, uint32_t width, uint32_t height,
                                             uint32_t targetX, uint32_t targetY,
                                             PickTargetPixel& pixelOut) {
    // KI-012: a queue family with minImageTransferGranularity=(0,0,0) only accepts whole-image
    // copies at offset (0,0,0) -- the single-texel sub-region copy below is a spec violation
    // there. Fall back to copying the whole id image and indexing the center texel on the CPU.
    const VkDeviceSize bytesNeeded = requiresFullImageTransfers_
        ? static_cast<VkDeviceSize>(width) * height * sizeof(PickTargetPixel)
        : kSingleTexelSize;

    if (!EnsureStagingBuffer(bytesNeeded)) {
        return false;
    }
    VulkanDevice* device = GetDevice();
    VkDevice vkDevice = device->device;

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

    // The ID image is already in VK_IMAGE_LAYOUT_GENERAL (storage image, transitioned once by
    // PickIdTargetNode and kept there), which is valid for TRANSFER_SRC.
    //
    // KI-012: a sub-region copy (offset != (0,0,0) and/or extent != the full image) is a spec
    // violation on a queue family whose minImageTransferGranularity is (0,0,0) — that queue only
    // accepts whole-image copies at offset (0,0,0). requiresFullImageTransfers_ (cached in
    // CompileImpl from VulkanDevice::RequiresFullImageTransfers()) picks the correct region here;
    // the center-texel extraction below runs on the CPU in that case instead of via imageOffset.
    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                  = 0;  // tightly packed
    region.bufferImageHeight                = 0;
    region.imageSubresource.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel        = 0;
    region.imageSubresource.baseArrayLayer  = 0;
    region.imageSubresource.layerCount      = 1;
    if (requiresFullImageTransfers_) {
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { width, height, 1 };
    } else {
        region.imageOffset = { static_cast<int32_t>(targetX),
                               static_cast<int32_t>(targetY), 0 };
        region.imageExtent = { 1, 1, 1 };
    }

    // The dispatch writes this storage image; make those writes visible to the transfer readback.
    // Queue submission order alone orders execution but does not provide the image memory dependency.
    VkImageMemoryBarrier imageToCopy{};
    imageToCopy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageToCopy.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageToCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    imageToCopy.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageToCopy.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageToCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageToCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageToCopy.image = image;
    imageToCopy.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &imageToCopy);

    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_GENERAL, stagingBuffer_, 1, &region);

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

    {
        // Externally synchronized per Vulkan spec (audit V-M11); NOT held across the
        // vkWaitForFences below, per the comment above about not stalling the whole queue.
        std::lock_guard submitLock(device->SubmitMutex(device->queue));
        if (vkQueueSubmit(device->queue, 1, &submit, fence) != VK_SUCCESS) {
            vkDestroyFence(vkDevice, fence, nullptr);
            vkFreeCommandBuffers(vkDevice, commandPool_, 1, &cmd);
            return false;
        }
    }

    if (const GpuWaitResult waited = BoundedWaitFence(vkDevice, fence); !waited.Ok()) {
        // The submission may still be in flight: leave the fence and command buffer to device teardown.
        if (auto* graph = GetOwningGraph()) graph->ReportGpuFault("VoxelSelectionProviderNode::pickFence", waited.result, true);
        return false;
    }

    // --- Map the staging memory and read the pickID. On the full-image path (KI-012), the
    // staging buffer holds the WHOLE image and the target texel must be indexed on the CPU. ---
    void* mapped = nullptr;
    if (vkMapMemory(vkDevice, stagingMemory_, 0, bytesNeeded, 0, &mapped) != VK_SUCCESS) {
        vkDestroyFence(vkDevice, fence, nullptr);
        vkFreeCommandBuffers(vkDevice, commandPool_, 1, &cmd);
        return false;
    }
    if (requiresFullImageTransfers_) {
        const PickTargetPixel* pixels = static_cast<const PickTargetPixel*>(mapped);
        const size_t targetIdx = static_cast<size_t>(targetY) * width + targetX;
        pixelOut = pixels[targetIdx];
    } else {
        std::memcpy(&pixelOut, mapped, sizeof(PickTargetPixel));
    }
    vkUnmapMemory(vkDevice, stagingMemory_);

    // Free the one-shot cmd buffer + fence (staging buffer is kept for reuse).
    vkDestroyFence(vkDevice, fence, nullptr);
    vkFreeCommandBuffers(vkDevice, commandPool_, 1, &cmd);
    return true;
}

void VoxelSelectionProviderNode::DestroyStagingBuffer() {
    VulkanDevice* device = GetDevice();
    if (!device) {
        return;
    }
    VkDevice vkDevice = device->device;
    if (stagingBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkDevice, stagingBuffer_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
    }
    if (stagingMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, stagingMemory_, nullptr);
        stagingMemory_ = VK_NULL_HANDLE;
    }
    stagingCapacity_ = 0;
}

void VoxelSelectionProviderNode::TypedCleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[VoxelSelectionProvider] cleanup");
    // Release the host-visible staging buffer we own (RAII). The device lives in the base
    // NodeInstance and is re-set every CompileImpl via SetDevice(), so it is intentionally not
    // reset here; drop the cached Dependency handles (re-acquired next CompileImpl).
    DestroyReadbackSlots();
    DestroyStagingBuffer();
    ResetTracking();
    previousFrame_ = {};
    commandPool_ = VK_NULL_HANDLE;
    // No click-edge state to reset: it lives in InputState.clicksThisFrame (owned by InputNode), not
    // this node — the duplicate-fire-after-recompile bug dies with that private state.
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::VoxelSelectionProviderNodeType);
