#include "Nodes/PresentNode.h"
#include "Core/NodeRegistration.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "Core/RenderGraph.h"
#include "Core/FailScenario.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace Vixen::RenderGraph {

// ====== PresentNodeType ======

std::unique_ptr<NodeInstance> PresentNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<PresentNode>(
        instanceName,
        const_cast<PresentNodeType*>(this)
    );
}

// ====== PresentNode ======

PresentNode::PresentNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<PresentNodeConfig>(instanceName, nodeType)
{
}

void PresentNode::TypedSetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("PresentNode: Setup (graph-scope initialization)");
}

void PresentNode::TypedCompileImpl(TypedCompileContext& ctx) {
    // Access device input (compile-time dependency)
    VulkanDevice* devicePtr = ctx.In(PresentNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("PresentNode: Invalid device handle");
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    // Get parameters using config constants
    waitForIdle = GetParameterValue<bool>(PresentNodeConfig::WAIT_FOR_IDLE, true);

    // Validate inputs using typed slot access
    VkSwapchainKHR swapchain = ctx.In(PresentNodeConfig::SWAPCHAIN);
    if (swapchain == VK_NULL_HANDLE) {
        throw std::runtime_error("PresentNode: swapchain input not connected or invalid");
    }

    // Note: PRESENT_FUNCTION input is optional - if not provided, we use vkQueuePresentKHR directly
}

void PresentNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    Present(ctx);
}

void PresentNode::TypedCleanupImpl(TypedCleanupContext& ctx) {
    (void)ctx;
}

VkResult PresentNode::Present(Context& ctx) {
    // Get inputs on-demand via typed slots (SlotRole from config)
    VkSwapchainKHR swapchain = ctx.In(PresentNodeConfig::SWAPCHAIN);
    uint32_t imageIndex = ctx.In(PresentNodeConfig::IMAGE_INDEX);
    VkSemaphore renderCompleteSemaphore = ctx.In(PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);
    const std::vector<VkFence>& presentFenceArray = ctx.In(PresentNodeConfig::PRESENT_FENCE_ARRAY);

    // Guard against invalid image index (swapchain out of date)
    if (imageIndex == UINT32_MAX) {
        NODE_LOG_WARNING("PresentNode: Invalid image index - skipping present");
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    
    // Get present function pointer - prefer device-provided function, fallback to input connection
    PFN_vkQueuePresentKHR fpQueuePresent = nullptr;
    if (device != nullptr && device->HasPresentSupport()) {
        fpQueuePresent = device->GetPresentFunction();
    } else {
        // Fallback: try to get from input connection
        fpQueuePresent = ctx.In(PresentNodeConfig::PRESENT_FUNCTION);
    }

    if (fpQueuePresent == nullptr) {
        throw std::runtime_error("PresentNode: No present function available");
    }

    // R715: the present is a record issued by the graph's single submission point (at once on the
    // sequential executor, after the final wave on the lowered one), never under a node-held VK2.
    SubmitRecord present;
    present.present = true;
    present.presentFn = fpQueuePresent;
    present.swapchain = swapchain;
    present.imageIndex = imageIndex;
    present.presentWait = renderCompleteSemaphore;

    // VK_EXT_swapchain_maintenance1: chain the present fence only when the extension is available.
    // The presentFenceArray is only populated when the extension is enabled (checked in FrameSyncNode)
    if (!presentFenceArray.empty()) {
        // CRITICAL: Reset fence immediately before reuse to avoid race condition
        // SwapChainNode waits on the fence, but vkQueuePresentKHR may still own it after signaling
        // Per Vulkan spec: "vkResetFences must not be called on a fence in use by a queue operation"
        // Resetting here (right before present) ensures previous present operation has released ownership
        VkFence fenceToReset = presentFenceArray[imageIndex];
        vkResetFences(device->device, 1, &fenceToReset);
        present.presentFence = presentFenceArray[imageIndex];
    }

    const auto presentStarted = std::chrono::steady_clock::now();
    {
        lastResult = VIXEN_FAULT_FILTER(GetOwningGraph(), Present,
                                        GetOwningGraph()->SubmitNodeWork(this, std::move(present)));
    }

    // Wait for device idle if requested (for compatibility with current behavior). VK_SUBOPTIMAL_KHR
    // means the present itself succeeded (the image was submitted; the driver is only hinting the
    // swapchain should be recreated soon -- see SwapChainNode::AcquireNextImage for the matching
    // acquire-side handling) -- treat it the same as VK_SUCCESS here, or Dozen (which returns
    // VK_SUBOPTIMAL_KHR routinely) permanently loses this idle-wait's synchronization margin.
    const bool presentSucceeded = (lastResult == VK_SUCCESS || lastResult == VK_SUBOPTIMAL_KHR);
    if (waitForIdle && presentSucceeded && device != nullptr) {
        const GpuWaitResult idle = GetOwningGraph()->WaitIssuedWork(device);
        if (!idle.Ok()) {
            GetOwningGraph()->ReportGpuFault("PresentNode::idle wait", idle.result, true);
        }
    } else if (lastResult == VK_ERROR_DEVICE_LOST || lastResult == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
               lastResult == VK_ERROR_OUT_OF_HOST_MEMORY) {
        // OUT_OF_DATE / SUBOPTIMAL are the swapchain nodes' business; a loss or OOM here is not.
        GetOwningGraph()->ReportGpuFault("PresentNode::vkQueuePresentKHR", lastResult);
    }
    if (std::getenv("VIXEN_GPUYIELD_TRACE")) {
        const auto completed = std::chrono::steady_clock::now();
        std::fprintf(stderr, "[GPUYIELD_HOST] operation=present-and-idle elapsed_ms=%.6f start_ns=%lld end_ns=%lld\n",
            std::chrono::duration<double, std::milli>(completed - presentStarted).count(),
            static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(presentStarted.time_since_epoch()).count()),
            static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(completed.time_since_epoch()).count()));
    }

    // Set outputs
    ctx.Out(PresentNodeConfig::PRESENT_RESULT, &lastResult);
    ctx.Out(PresentNodeConfig::VULKAN_DEVICE_OUT, device);

    return lastResult;
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::PresentNodeType);

#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
namespace FS = Vixen::RenderGraph::FailScenario;
VIXEN_FAIL_SCENARIOS_DECLARE(Vixen::RenderGraph::PresentNodeType,
    VIXEN_SCENARIO(PresentOutOfDate,
        FS::VkTransient{ .site = FS::FaultSite::Present, .result = VK_ERROR_OUT_OF_DATE_KHR },
        // Minimal contract: no crash + continued progress (global criteria). NOTE (from planning
        // exploration): PresentNode currently IGNORES the present result — nothing consumes
        // PRESENT_RESULT to trigger recreation. This scenario documents today's tolerated behavior;
        // when present-driven recreation is implemented, tighten this contract to assert it.
        [](FS::ScenarioContext&) {})
);
#endif
