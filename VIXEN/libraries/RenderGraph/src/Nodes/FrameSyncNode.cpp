#include "Nodes/FrameSyncNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/FailScenario.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "Message.h"
#include <mutex>
#include <stdexcept>

namespace Vixen::RenderGraph {

// ====== FrameSyncNodeType ======

std::unique_ptr<NodeInstance> FrameSyncNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<FrameSyncNode>(
        instanceName,
        const_cast<NodeType*>(static_cast<const NodeType*>(this))
    );
}

// ====== FrameSyncNode ======

FrameSyncNode::FrameSyncNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<FrameSyncNodeConfig>(instanceName, nodeType)
{
}

void FrameSyncNode::TypedSetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("FrameSyncNode: Setup (graph-scope initialization)");

    if (GetMessageBus() && !frameEndSubscribed_) {
        const EventBus::EventSubscriptionID subscription = SubscribeToMessage(
            EventBus::FrameEndEvent::TYPE,
            [this](const EventBus::BaseEventMessage&) {
                HandleFrameEnd();
                return true;
            });
        frameEndSubscribed_ = subscription != 0;
    }
}

void FrameSyncNode::TypedCompileImpl(TypedCompileContext& ctx) {
    // Access device input (compile-time dependency)
    VulkanDevice* devicePtr = ctx.In(FrameSyncNodeConfig::VULKAN_DEVICE);

    if (devicePtr == nullptr) {
        std::string errorMsg = "FrameSyncNode: VulkanDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);
    // Phase 0.4 / FR-3: FrameSyncNode owns only per-FLIGHT sync (CPU-GPU fences +
    // imageAvailable semaphores). Per-IMAGE renderComplete semaphores and present
    // fences are owned by SwapChainNode (sized to the exact swapchain image count).
    constexpr uint32_t flightCount = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

    NODE_LOG_INFO("Creating per-flight synchronization primitives: MAX_FRAMES_IN_FLIGHT="
                  + std::to_string(flightCount));

    // Create per-flight fences (CPU-GPU sync)
    frameSyncData.resize(flightCount);
    frameCompletionValues_.resize(flightCount, 0);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled so first frame doesn't wait

    for (uint32_t i = 0; i < flightCount; i++) {
        if (vkCreateFence(device->device, &fenceInfo, nullptr, &frameSyncData[i].inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create in-flight fence for frame " + std::to_string(i));
        }
        NODE_LOG_INFO("Flight " + std::to_string(i) + ": fence=0x"
                      + std::to_string(reinterpret_cast<uint64_t>(frameSyncData[i].inFlightFence)));
    }

    // Phase 0.6: CORRECT per Vulkan validation guide
    // https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
    //
    // CRITICAL: renderComplete MUST be per-IMAGE, not per-FLIGHT!
    //
    // Reason: vkQueuePresentKHR holds the renderComplete semaphore until the presentation
    // engine finishes displaying the image. This can take longer than GPU rendering.
    // Fences only track GPU work completion, NOT presentation completion.
    //
    // - imageAvailable: per-FLIGHT (tracks frame pacing)
    // - renderComplete: per-IMAGE (tracks presentation engine usage per swapchain image)

    imageAvailableSemaphores.resize(flightCount);  // Per-FLIGHT for acquisition

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // Create per-FLIGHT acquisition semaphores
    for (uint32_t i = 0; i < flightCount; i++) {
        if (vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create imageAvailable semaphore for flight " + std::to_string(i));
        }
    }

    isCreated = true;
    currentFrameIndex = 0;

    // P5a M1: create the per-loop timeline semaphore ONCE (persistent across recompile so the
    // monotonic counter is never reset by resize/recompile). Guard on VK_NULL_HANDLE so a second
    // CompileImpl (e.g. swapchain resize) is a no-op for this object.
    if (timelineSemaphore_ == VK_NULL_HANDLE) {
        VkSemaphoreTypeCreateInfo typeInfo{};
        typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeInfo.initialValue = 0;

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semInfo.pNext = &typeInfo;

        if (vkCreateSemaphore(device->device, &semInfo, nullptr, &timelineSemaphore_) != VK_SUCCESS) {
            throw std::runtime_error("FrameSyncNode: failed to create per-loop timeline semaphore");
        }
        std::fill(frameCompletionValues_.begin(), frameCompletionValues_.end(), 0);
        NODE_LOG_INFO("Created per-loop timeline semaphore (auto-sync P5a M1)");
    }

    // Set initial outputs (flight 0)
    ctx.Out(FrameSyncNodeConfig::CURRENT_FRAME_INDEX, currentFrameIndex);
    ctx.Out(FrameSyncNodeConfig::IN_FLIGHT_FENCE, frameSyncData[currentFrameIndex].inFlightFence);

    // Output the per-FLIGHT imageAvailable array
    ctx.Out(FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, imageAvailableSemaphores);

    // P5a M1: publish safe compile-time defaults for the new timeline slots.
    // frameBase_ is NOT reset here — it must survive recompile to keep the counter monotonic.
    ctx.Out(FrameSyncNodeConfig::TIMELINE_SEMAPHORE, timelineSemaphore_);
    ctx.Out(FrameSyncNodeConfig::TIMELINE_FRAME_BASE, frameBase_);

    NODE_LOG_INFO("Per-flight synchronization primitives created successfully");
    NODE_LOG_INFO("Created " + std::to_string(imageAvailableSemaphores.size()) + " imageAvailable semaphores (per-flight)");
}

void FrameSyncNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    if (frameCompletionSubmitFailure_ != VK_SUCCESS) {
        throw std::runtime_error("FrameSyncNode: frame completion timeline submit previously failed: " +
                                 std::to_string(frameCompletionSubmitFailure_));
    }

    // Advance frame index (ring buffer for CPU-GPU sync)
    currentFrameIndex = (currentFrameIndex + 1) % FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

    // Advance the monotonic timeline base. The graph groups use offsets below this frame's
    // completion marker; FrameEndEvent signals the reserved value after all queue submissions.
    const uint64_t stride = GetOwningGraph()->GetFrameSyncSchedule().timelineValuesPerFrame;
    frameBase_ = NextFrameBase(frameBase_, stride);

    // Wait for every queue submit from the previous use of this flight before reusing any
    // frame-indexed command buffer or descriptor set. The binary fence may belong to an
    // intermediate submit (for example, UI before later compute stages), so it is not a
    // sufficient guard for the whole graph frame.
    const uint64_t completionValue = frameCompletionValues_[currentFrameIndex];
    if (completionValue != 0) {
        VkSemaphoreWaitInfo timelineWait{};
        timelineWait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        timelineWait.semaphoreCount = 1;
        timelineWait.pSemaphores = &timelineSemaphore_;
        timelineWait.pValues = &completionValue;
        const VkResult timelineWaitResult = vkWaitSemaphores(
            device->device, &timelineWait, UINT64_MAX);
        if (timelineWaitResult == VK_ERROR_DEVICE_LOST) {
            NODE_LOG_ERROR("Device lost while waiting on frame completion timeline (flight " +
                           std::to_string(currentFrameIndex) + ")");
            GetOwningGraph()->NotifyDeviceLost("FrameSyncNode::vkWaitSemaphores");
            return;
        }
        if (timelineWaitResult != VK_SUCCESS) {
            throw std::runtime_error("FrameSyncNode: failed to wait for frame completion timeline: " +
                                     std::to_string(timelineWaitResult));
        }
    }

    // Keep the binary fence wait for existing consumers that use it as a mid-frame
    // synchronization point (for example, optional debug readback).
    VkFence currentFence = frameSyncData[currentFrameIndex].inFlightFence;
    VkResult waitResult = VIXEN_FAULT_FILTER(GetOwningGraph(), FenceWait,
                              vkWaitForFences(device->device, 1, &currentFence, VK_TRUE, UINT64_MAX));

    // AR#1 Phase 3 (Increment 1): this fence wait runs every frame and is the universal, earliest
    // backstop for device loss — when the GPU device is lost, the submitted work never completes and
    // the wait returns VK_ERROR_DEVICE_LOST immediately. Latch it on the graph so RenderFrame() reports
    // a distinct status (and, in Increment 2, triggers a rebuild on a fresh device). Bail out before
    // publishing this frame's now-invalid fence/semaphore handles downstream.
    if (waitResult == VK_ERROR_DEVICE_LOST) {
        NODE_LOG_ERROR("Device lost while waiting on in-flight fence (flight "
                       + std::to_string(currentFrameIndex) + ")");
        GetOwningGraph()->NotifyDeviceLost("FrameSyncNode::vkWaitForFences");
        return;
    }

    // Note: Fence will be reset by GeometryRenderNode before submission

    // Update outputs with current frame's fence
    ctx.Out(FrameSyncNodeConfig::CURRENT_FRAME_INDEX, currentFrameIndex);
    ctx.Out(FrameSyncNodeConfig::IN_FLIGHT_FENCE, currentFence);

    // Re-output the per-FLIGHT imageAvailable array for Execute-phase connections
    ctx.Out(FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, imageAvailableSemaphores);

    // Publish timeline outputs for scheduled resource hazards and frame completion tracking.
    ctx.Out(FrameSyncNodeConfig::TIMELINE_SEMAPHORE, timelineSemaphore_);
    ctx.Out(FrameSyncNodeConfig::TIMELINE_FRAME_BASE, frameBase_);
}

void FrameSyncNode::HandleFrameEnd() {
    if (!frameEndSubscribed_ || timelineSemaphore_ == VK_NULL_HANDLE ||
        GetDevice() == nullptr || currentFrameIndex >= frameCompletionValues_.size()) {
        return;
    }

    const uint64_t timelineStride = GetOwningGraph()->GetFrameSyncSchedule().timelineValuesPerFrame;
    // Each scheduled group uses an offset in [0, stride). Signal the next value as a marker
    // after all node submissions from this frame. Reserve one value even for an empty graph.
    const uint64_t completionValue = FrameCompletionValue(frameBase_, timelineStride);

    VkSemaphoreSubmitInfo signal{};
    signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = timelineSemaphore_;
    signal.value = completionValue;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;

    VulkanDevice* const vulkanDevice = GetDevice();
    VkResult result;
    {
        std::lock_guard submitLock(vulkanDevice->SubmitMutex(vulkanDevice->queue));
        result = vulkanDevice->fpQueueSubmit2(vulkanDevice->queue, 1, &submit, VK_NULL_HANDLE);
    }
    if (result != VK_SUCCESS) {
        frameCompletionSubmitFailure_ = result;
        NODE_LOG_ERROR("FrameSyncNode: failed to submit frame completion timeline marker: " +
                       std::to_string(result));
        if (result == VK_ERROR_DEVICE_LOST) {
            GetOwningGraph()->NotifyDeviceLost("FrameSyncNode::FrameEnd vkQueueSubmit2");
        }
        return;
    }

    frameCompletionValues_[currentFrameIndex] = completionValue;
    // Preserve monotonicity if the graph schedule changes during recompile.
    frameBase_ = completionValue;
}

void FrameSyncNode::TypedCleanupImpl(TypedCleanupContext& ctx) {
    if (isCreated && device != nullptr && device->device != VK_NULL_HANDLE) {
        NODE_LOG_INFO("Destroying frame synchronization primitives");

        // Destroy per-flight fences
        for (auto& sync : frameSyncData) {
            if (sync.inFlightFence != VK_NULL_HANDLE) {
                vkDestroyFence(device->device, sync.inFlightFence, nullptr);
                sync.inFlightFence = VK_NULL_HANDLE;
            }
        }

        // Destroy per-flight imageAvailable semaphores
        for (auto& semaphore : imageAvailableSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device->device, semaphore, nullptr);
                semaphore = VK_NULL_HANDLE;
            }
        }

        frameSyncData.clear();
        imageAvailableSemaphores.clear();
        currentFrameIndex = 0;
        isCreated = false;

        NODE_LOG_INFO("Frame synchronization primitives destroyed");
    }

    // P5a M1: the per-loop timeline semaphore is PERSISTENT across recompile (resize/recompile must
    // NOT destroy it — that would reset the monotonic counter and collide with frames still in
    // flight). Torn down on FinalTeardown AND DeviceLost (a device-scoped object cannot outlive its
    // device; frameBase_ is CPU-side, so timeline monotonicity survives the recreate). KI-004 class.
    if (ctx.reason != CleanupReason::Recompile &&
        timelineSemaphore_ != VK_NULL_HANDLE &&
        device != nullptr && device->device != VK_NULL_HANDLE) {
        vkDestroySemaphore(device->device, timelineSemaphore_, nullptr);
        timelineSemaphore_ = VK_NULL_HANDLE;
        std::fill(frameCompletionValues_.begin(), frameCompletionValues_.end(), 0);
        frameCompletionValues_.clear();
        frameCompletionSubmitFailure_ = VK_SUCCESS;
        NODE_LOG_INFO("Destroyed per-loop timeline semaphore (final teardown)");
    }
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::FrameSyncNodeType);

#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
namespace FS = Vixen::RenderGraph::FailScenario;
VIXEN_FAIL_SCENARIOS_DECLARE(Vixen::RenderGraph::FrameSyncNodeType,
    // Hard regression gate for KI-004 (resolved — two fixes): (1) NotifyDeviceLost() arms the
    // central frame abort so no downstream node executes on the condemned frame; (2) UIRenderNode
    // tears down its persistent per-image command buffers + RmlUi GPU objects on DeviceLost
    // (persistence is only valid across Recompile, where the device survives).
    VIXEN_SCENARIO(DeviceLostRecovery,
        FS::VkTransient{ .site = FS::FaultSite::FenceWait, .result = VK_ERROR_DEVICE_LOST },
        // The one-shot forced VK_ERROR_DEVICE_LOST drives the REAL detection path
        // (this node's fence wait → NotifyDeviceLost → frame aborts → RenderFrame returns
        // DEVICE_LOST → app Render() → RecoverFromDeviceLoss teardown-reverse/rebuild-forward).
        // On the healthy device the rebuild succeeds — the global criteria then prove
        // 30 frames of continuous post-recovery rendering, which is exactly the manual
        // VIXEN_SIMULATE_DEVICE_LOSS gate, automated.
        [](FS::ScenarioContext& c) {
            if (c.Graph()->IsDeviceLost())
                c.Fail("device-lost latch still set — recovery did not complete");
        })
);
#endif
