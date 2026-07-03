#include "Nodes/VoxelSelectionProviderNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "Selection/SelectionCandidate.h"
#include "InputEvents.h"
#include "VulkanDevice.h"

#include <glm/glm.hpp>
#include <cstring>
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

void VoxelSelectionProviderNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[VoxelSelectionProvider] setup");
    // Provider layer priority (world layer = 0 by default). Read once at graph-scope setup.
    priority_ = GetParameterValue<int>(VoxelSelectionProviderNodeConfig::PARAM_PRIORITY, 0);
}

void VoxelSelectionProviderNode::CompileImpl(TypedCompileContext& ctx) {
    // Cache the compile-stable Dependency handles (the ID-image ring, device and command pool are
    // valid for the cached scene's lifetime). The device lives in the base NodeInstance (SetDevice
    // now, GetDevice() in Execute/Cleanup) per the device convention — no private device member.
    VulkanDevice* device = ctx.In(VoxelSelectionProviderNodeConfig::VULKAN_DEVICE);

    // If the device changes on recompile, tear down the staging buffer first so we never free it
    // against the wrong device. On a same-device recompile we keep it (it is reused across clicks).
    if (device != GetDevice()) {
        DestroyStagingBuffer();
    }
    SetDevice(device);
    commandPool_ = ctx.In(VoxelSelectionProviderNodeConfig::COMMAND_POOL);
    idImage_     = ctx.In(VoxelSelectionProviderNodeConfig::ID_IMAGE);

    const bool ready = GetDevice() && commandPool_ != VK_NULL_HANDLE && idImage_ != VK_NULL_HANDLE;
    NODE_LOG_INFO(std::string("[VoxelSelectionProvider] compile: priority=") +
                  std::to_string(priority_) + "; " +
                  (ready ? "configured (device+pool+ID image acquired)"
                         : "INERT (missing device/pool/ID image)"));
}

void VoxelSelectionProviderNode::ExecuteImpl(TypedExecuteContext& ctx) {
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

    // Find the left-button press entry (input-rework slice 1 M3: the shared click list replaces
    // the old private lastLeftDown_ edge detector). Several presses in one frame: the LAST one
    // wins — matches the old single-poll semantics (the readback is a single crosshair sample, not
    // per-press, so only whether a press happened this frame matters here).
    bool pressedThisFrame = false;
    for (const ClickEvent& click : input->clicksThisFrame) {
        if (click.button == static_cast<int>(EventBus::MouseButton::Left) && click.pressed) {
            pressedThisFrame = true;
        }
    }

    if (!pressedThisFrame) {
        ctx.Out(VoxelSelectionProviderNodeConfig::CANDIDATE, candidate);
        return;  // cheap: only read back on a click edge
    }

    const uint32_t width  = ctx.In(VoxelSelectionProviderNodeConfig::VIEWPORT_WIDTH);
    const uint32_t height = ctx.In(VoxelSelectionProviderNodeConfig::VIEWPORT_HEIGHT);

    if (!GetDevice() || commandPool_ == VK_NULL_HANDLE || idImage_ == VK_NULL_HANDLE ||
        width == 0 || height == 0) {
        // Resources or viewport not ready — emit a miss (never crash).
        NODE_LOG_INFO("[VoxelSelectionProvider] click ignored — resources/viewport not ready");
        ctx.Out(VoxelSelectionProviderNodeConfig::CANDIDATE, candidate);
        return;
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
    if (!ReadCenterPixel(width, height, pickID) || pickID == kMissSentinel) {
        // Readback failed (see prior error log) or empty space under the crosshair — report a miss.
        NODE_LOG_INFO("[VoxelSelectionProvider] click — miss (empty space under crosshair)");
        ctx.Out(VoxelSelectionProviderNodeConfig::CANDIDATE, candidate);
        return;
    }

    // Hit. payload carries the packed pickID (brick = id >> 10, voxel = id & 0x3FF). World position
    // from brick/voxel is deferred (needs the brick->world inverse — see design "Out of scope"), so
    // depth = 0 and worldPos = (0,0,0) for now.
    candidate.hit      = true;
    candidate.id       = SelectionId{ ProviderKind::Voxel, static_cast<uint64_t>(pickID) };
    candidate.depth    = 0.0f;
    candidate.worldPos = glm::vec3(0.0f);

    const uint32_t brickIndex     = pickID >> kBrickIdxShift;
    const uint32_t voxelLinearIdx = pickID & kVoxelIdxMask;
    NODE_LOG_INFO("[VoxelSelectionProvider] click — HIT pickID=" + std::to_string(pickID) +
                  " brick=" + std::to_string(brickIndex) +
                  " voxel=" + std::to_string(voxelLinearIdx) +
                  " priority=" + std::to_string(priority_));

    ctx.Out(VoxelSelectionProviderNodeConfig::CANDIDATE, candidate);
}

// ----------------------------------------------------------------------------
// GPU readback helpers (MOVED verbatim from the old C++ VoxelSelectionProvider;
// device_ → GetDevice() per the base-NodeInstance device convention).
// ----------------------------------------------------------------------------

bool VoxelSelectionProviderNode::EnsureStagingBuffer() {
    if (stagingBuffer_ != VK_NULL_HANDLE) {
        return true;  // already created — reused across clicks
    }
    VulkanDevice* device = GetDevice();
    if (!device) {
        return false;
    }
    VkDevice vkDevice = device->device;

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
        device->gpuMemoryProperties,
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

bool VoxelSelectionProviderNode::ReadCenterPixel(uint32_t width, uint32_t height, uint32_t& pickIDOut) {
    if (!EnsureStagingBuffer()) {
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

    if (vkQueueSubmit(device->queue, 1, &submit, fence) != VK_SUCCESS) {
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
}

void VoxelSelectionProviderNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[VoxelSelectionProvider] cleanup");
    // Release the host-visible staging buffer we own (RAII). The device lives in the base
    // NodeInstance and is re-set every CompileImpl via SetDevice(), so it is intentionally not
    // reset here; drop the cached Dependency handles (re-acquired next CompileImpl).
    DestroyStagingBuffer();
    commandPool_ = VK_NULL_HANDLE;
    idImage_     = VK_NULL_HANDLE;
    // No click-edge state to reset: it lives in InputState.clicksThisFrame (owned by InputNode), not
    // this node — the duplicate-fire-after-recompile bug dies with that private state.
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::VoxelSelectionProviderNodeType);
