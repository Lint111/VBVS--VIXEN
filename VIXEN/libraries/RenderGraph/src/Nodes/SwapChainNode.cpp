#include "Nodes/SwapChainNode.h"
#include "Core/NodeRegistration.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"  // Phase 0.4: For CURRENT_FRAME_INDEX input
#include "Core/RenderGraph.h"
#include "Core/FailScenario.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "EventTypes/RenderGraphEvents.h"
#include "Message.h"

namespace Vixen::RenderGraph {

// ====== SwapChainNodeType ======

std::unique_ptr<NodeInstance> SwapChainNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<SwapChainNode>(
        instanceName,
        const_cast<NodeType*>(static_cast<const NodeType*>(this))
    );
}

// ====== SwapChainNode ======

SwapChainNode::SwapChainNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<SwapChainNodeConfig>(instanceName, nodeType)
{
}

void SwapChainNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("SwapChainNode: Setup (graph-scope initialization)");

    // Subscribe to window resize events ONCE. SetupImpl re-runs on every recompile; without this
    // guard the subscriptions accumulate and each one fires an extra MarkNeedsRecompile, so a single
    // resize snowballs into many recompiles.
    if (GetMessageBus() && !resizeSubscribed_) {
        resizeSubscribed_ = true;
        SubscribeToMessage(
            EventTypes::WindowResizedMessage::TYPE,
            [this](const EventBus::BaseEventMessage& msg) -> bool {
                // Mark this node for recompilation
                NODE_LOG_INFO("[SwapChainNode] Received WindowResizedMessage - marking self for recompilation");
                MarkNeedsRecompile();
                return true; // Message handled
            }
        );
    }

    if (!swapChainWrapper) {
        // Create a new VulkanSwapChain wrapper
        swapChainWrapper = new VulkanSwapChain();
		swapChainWrapper->Initialize();
        NODE_LOG_INFO("SwapChainNode::Setup - Created swapchain wrapper");
    }

    currentFrame = 0;
}

void SwapChainNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[SwapChainNode::Compile] ===== RECOMPILATION TRIGGERED =====");
    NODE_LOG_INFO("[SwapChainNode::Compile] START");
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
    ++compileCount_;
#endif

    // Access device input (compile-time dependency)
    SetDevice(ctx.In(SwapChainNodeConfig::VULKAN_DEVICE_IN));

    if (GetDevice() == nullptr) {
        std::string errorMsg = "SwapChainNode: VulkanDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Publish render pause starting event
    if (GetMessageBus()) {
        GetMessageBus()->Publish(
            std::make_unique<EventTypes::RenderPauseEvent>(
                instanceId,
                EventTypes::RenderPauseEvent::Reason::SwapChainRecreation,
                EventTypes::RenderPauseEvent::Action::PAUSE_START
            )
        );
    }

    // Get input resources from connected nodes
    NODE_LOG_DEBUG("[SwapChainNode::Compile] Reading inputs...");
    GLFWwindow* window = ctx.In(SwapChainNodeConfig::WINDOW);
    width = ctx.In(SwapChainNodeConfig::WIDTH);
    height = ctx.In(SwapChainNodeConfig::HEIGHT);
    VkInstance instance = ctx.In(SwapChainNodeConfig::INSTANCE);

    NODE_LOG_DEBUG("[SwapChainNode::Compile] WIDTH = " + std::to_string(width) + ", HEIGHT = " + std::to_string(height));

    // Validate all inputs
    ValidateCompileInputs(window, instance);

    // Load extensions and create surface
    LoadExtensionsAndCreateSurface(instance, window);

    // Get graphics queue and setup formats/capabilities
    auto graphicsQueueIndex = GetDevice()->GetGraphicsQueueHandle();
    if (!graphicsQueueIndex.has_value()) {
        std::string errorMsg = "SwapChainNode: No queue family supports both graphics and presentation";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    SetupFormatsAndCapabilities(graphicsQueueIndex.value());

    // Create swapchain and image views
    CreateSwapchainAndViews();

    // FR-3: (re)create per-IMAGE sync resources sized to the EXACT swapchain image count.
    // Follows the swapchain's own destroy(Cleanup)+create(Compile) lifecycle.
    CreatePerImageSyncResources();

    // Publish outputs
    PublishCompileOutputs(ctx);

    // Publish render pause ending event
    if (GetMessageBus()) {
        GetMessageBus()->Publish(
            std::make_unique<EventTypes::RenderPauseEvent>(
                instanceId,
                EventTypes::RenderPauseEvent::Reason::SwapChainRecreation,
                EventTypes::RenderPauseEvent::Action::PAUSE_END
            )
        );
    }
}

void SwapChainNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Phase 0.5: imageAvailable (per-FLIGHT) still comes from FrameSyncNode.
    const std::vector<VkSemaphore>& imageAvailableSemaphores = ctx.In(SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);

    // FR-3: renderComplete (per-IMAGE) is owned here (member), sized to the actual image count.
    if (imageAvailableSemaphores.empty() || renderCompleteSemaphores.empty()) {
        throw std::runtime_error("SwapChainNode: synchronization arrays are empty");
    }

    // Phase 0.7: per-IMAGE present fences (owned here; empty if VK_EXT_swapchain_maintenance1 unavailable)
    const std::vector<VkFence>& presentFencesArray = presentFences;

    // Phase 0.6: CORRECT two-tier semaphore indexing (Vulkan guide pattern)
    // https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
    //
    // - imageAvailable: Indexed by FRAME INDEX (per-flight) - used for acquisition
    // - renderComplete: Indexed by IMAGE INDEX (per-image) - used by GeometryRender/Present
    //
    // This indexing is done in GeometryRenderNode after we know both frame and image indices.

    uint32_t currentFrameIndex = ctx.In(SwapChainNodeConfig::CURRENT_FRAME_INDEX);

    // Acquisition semaphore indexed by flight
    VkSemaphore acquireSemaphore = imageAvailableSemaphores[currentFrameIndex];

    // Phase 0.7: Wait for previous present to finish with the image we're about to acquire
    // Note: We don't know which image index we'll get yet, so we'll wait after acquisition
    // This is safe because presentFences are per-image and we wait before reusing that image

    // Acquire the next available image using the per-FLIGHT semaphore
    currentImageIndex = AcquireNextImage(acquireSemaphore);

    // Phase 0.7: Now that we know which image we got, wait for presentation to finish with it
    // This ensures the presentation engine has released the image before we start rendering to it
    // NOTE: We wait here but don't reset - PresentNode resets the fence right before reuse
    // to avoid race condition where vkQueuePresentKHR still owns the fence after signaling
    if (currentImageIndex != UINT32_MAX && !presentFencesArray.empty()) {
        VkFence presentFence = presentFencesArray[currentImageIndex];
        if (presentFence != VK_NULL_HANDLE && GetDevice() != nullptr) {
            vkWaitForFences(GetDevice()->device, 1, &presentFence, VK_TRUE, UINT64_MAX);
            // Fence reset moved to PresentNode to avoid ownership violation
        }
    }

    // Per-image in-flight fence wait (canonical imagesInFlight pattern). The command buffers,
    // descriptor sets, and timestamp query pools that downstream nodes reuse are keyed by IMAGE
    // index, but FrameSyncNode only waits on the per-FLIGHT fence (currentFrameIndex). With
    // MAX_FRAMES_IN_FLIGHT (4) != swapchain image count (typically 3) the flight ring and image
    // ring desync, so the per-flight wait does NOT prove the previous submission that touched THIS
    // image's resources has finished — the root cause of the re-record/re-submit/descriptor-update
    // -while-pending validation errors (VUID-vkBeginCommandBuffer-00049, -vkQueueSubmit2-03875,
    // -vkUpdateDescriptorSets-None-03047, -vkGetQueryPoolResults-None-09401). Wait on whichever
    // flight fence last guarded this image before its resources are reused, then stamp it with the
    // current frame's fence. This is a CPU-side ordering wait only (no vkDeviceWaitIdle): with
    // flights > images it serialises just enough that a given image is never in two submissions at
    // once, while still allowing the other images to remain in flight.
    if (currentImageIndex != UINT32_MAX && GetDevice() != nullptr &&
        currentImageIndex < imagesInFlight.size()) {
        VkFence imageFence = imagesInFlight[currentImageIndex];
        if (imageFence != VK_NULL_HANDLE) {
            vkWaitForFences(GetDevice()->device, 1, &imageFence, VK_TRUE, UINT64_MAX);
        }
        // Record the fence that will guard this image's work THIS frame, so a future frame that
        // reuses this image index waits on it. The consumer nodes reset + submit with this same
        // per-flight fence (ComputeDispatchNode/GeometryRenderNode/UIRenderNode), so by the time
        // this image comes back around the fence has been signalled by that submission.
        imagesInFlight[currentImageIndex] = ctx.In(SwapChainNodeConfig::IN_FLIGHT_FENCE);
    }

    // If swapchain is out of date, skip this frame.
    if (currentImageIndex == UINT32_MAX) {
        NODE_LOG_INFO("SwapChainNode: Skipping frame due to out-of-date swapchain");
        // Propagate the invalid index so the downstream render + present nodes skip this frame too.
        // Without this the IMAGE_INDEX output keeps its previous (valid) value, so the render node
        // records + submits work that waits on the per-flight acquire semaphore which acquire never
        // signalled (acquire returned OUT_OF_DATE) — that submit blocks forever, the queue never goes
        // idle, and the whole render loop deadlocks on the next fence/idle wait. This was the
        // window-resize hang.
        ctx.Out(SwapChainNodeConfig::IMAGE_INDEX, UINT32_MAX);
        // ALSO stop the rest of this frame centrally: relying on every downstream consumer to guard
        // the sentinel individually proved fragile (six of ten guards were missing or placed after
        // the first per-image indexing — the maximize/fullscreen segfault). The abort skips them
        // wholesale; the per-node guards stay as second-layer defense.
        if (auto* graph = GetOwningGraph()) {
            graph->AbortCurrentFrame();
        }
        return;
    }

    // Output the acquired image index
    ctx.Out(SwapChainNodeConfig::IMAGE_INDEX, currentImageIndex);

    // Output the current frame's image view
    VkImageView currentFrameImageView = swapChainWrapper->scPublicVars.colorBuffers[currentImageIndex].view;
    ctx.Out(SwapChainNodeConfig::CURRENT_FRAME_IMAGE_VIEW, currentFrameImageView);

    NODE_LOG_INFO("Frame " + std::to_string(currentFrame) + ": acquired image " + std::to_string(currentImageIndex)
                  + ", frameIdx=" + std::to_string(currentFrameIndex)
                  + ", acquireSem[" + std::to_string(currentFrameIndex) + "]=0x" + std::to_string(reinterpret_cast<uint64_t>(acquireSemaphore))
                  + ", renderCompleteSem[" + std::to_string(currentImageIndex) + "]=0x" + std::to_string(reinterpret_cast<uint64_t>(renderCompleteSemaphores[currentImageIndex])));

    currentFrame++;
}

void SwapChainNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[SwapChainNode::CleanupImpl] Called");

    // FR-3: destroy per-IMAGE sync resources owned here, alongside the swapchain destroy below
    // (same safe point the existing swapchain teardown relies on).
    DestroyPerImageSyncResources();

    if (!swapChainWrapper) {
        return;
    }

    auto* dev = GetDevice();
    VkDevice device = (dev != nullptr) ? dev->device : VK_NULL_HANDLE;

    // The VkSurfaceKHR is PERSISTENT across recompiles, mirroring WindowNode's window+surface
    // (see WindowNode.cpp CleanupImpl). Destroying/recreating the surface every recompile (the old
    // unconditional Destroy() below) was pure waste -- the surface doesn't depend on extent. Tear
    // it down only on final teardown. This holds for DeviceLost too: VkSurfaceKHR is INSTANCE-scoped
    // (created against VkInstance, not VkDevice), so it survives a device recreation untouched.
    //
    // The swapchain HANDLE, however, is DEVICE-scoped and must NOT be treated like Recompile on
    // DeviceLost. On an ordinary Recompile the handle survives because the SAME device recreates it
    // (CreateSwapchainAndViews() passes the still-live handle as oldSwapchain to vkCreateSwapchainKHR
    // and destroys it only after the new one is created, letting the driver recycle/hand over
    // presentation state). On DeviceLost, RenderGraph::RecoverFromDeviceLoss() has DeviceNode create
    // an entirely NEW VulkanDevice (RenderGraph.cpp) -- the old swapchain handle belongs to the OLD,
    // about-to-be-destroyed device. Passing it as oldSwapchain into the NEW device's
    // fpCreateSwapchainKHR/fpDestroySwapchainKHR (resolved via the new device's dispatch table) is
    // exactly the KI-004 class of bug (a resource carrying stale device state across recovery) and
    // segfaults deep in the driver (KI-013) -- so DeviceLost must destroy the swapchain handle now,
    // against the OLD (still valid, merely lost) device, same as FinalTeardown would, while still
    // keeping the surface alive like a Recompile.
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[SwapChainNode::CleanupImpl] Recompile - destroying image views only, keeping surface + swapchain (for oldSwapchain reuse)");
        swapChainWrapper->DestroyImageViewsOnly(device);
        return;
    }

    if (ctx.reason == CleanupReason::DeviceLost) {
        NODE_LOG_INFO("[SwapChainNode::CleanupImpl] DeviceLost - destroying swapchain + image views (device-scoped; cannot survive a device recreation), keeping surface");
        swapChainWrapper->DestroySwapChain(device);
        return;
    }

    NODE_LOG_INFO("[SwapChainNode::CleanupImpl] Final teardown - destroying swapchain + surface");

    // Get VkInstance for surface destruction
    VkInstance instance = VK_NULL_HANDLE;
    try {
        // Cleanup-time access only - use GetInput directly
        Resource* res = NodeInstance::GetInput(SwapChainNodeConfig::INSTANCE.index, 0);
        if (res) {
            instance = res->GetHandle<VkInstance>();
        }
    } catch (...) {
        // Instance might not be available during shutdown - that's ok
    }

    // Destroy all Vulkan resources (wrapper loads extension pointers automatically if needed)
    swapChainWrapper->Destroy(device, instance);

    // Delete wrapper object
    delete swapChainWrapper;
    swapChainWrapper = nullptr;
}

VkSwapchainKHR SwapChainNode::GetSwapchain() const {
    if (swapChainWrapper) {
        return swapChainWrapper->scPublicVars.swapChain;
    }
    return VK_NULL_HANDLE;
}

SwapChainPublicVariables* SwapChainNode::GetSwapchainPublic() const {
    if (swapChainWrapper) return &swapChainWrapper->scPublicVars;
    return nullptr;
}

const std::vector<VkImageView>& SwapChainNode::GetColorImageViews() const {
    static std::vector<VkImageView> emptyViews;
    
    if (!swapChainWrapper) {
        return emptyViews;
    }

    // Extract image views from swapchain buffers
    static thread_local std::vector<VkImageView> views;
    views.clear();
    
    for (const auto& buffer : swapChainWrapper->scPublicVars.colorBuffers) {
        views.push_back(buffer.view);
    }
    
    return views;
}

uint32_t SwapChainNode::GetImageCount() const {
    if (swapChainWrapper) {
        return swapChainWrapper->scPublicVars.swapChainImageCount;
    }
    return 0;
}

VkFormat SwapChainNode::GetFormat() const {
    if (swapChainWrapper) {
        return swapChainWrapper->scPublicVars.Format;
    }
    return VK_FORMAT_UNDEFINED;
}

void SwapChainNode::SetSwapChainWrapper(VulkanSwapChain* swapchain) {
    swapChainWrapper = swapchain;
}

uint32_t SwapChainNode::AcquireNextImage(VkSemaphore presentCompleteSemaphore) {
    if (!swapChainWrapper) {
        std::string errorMsg = "SwapChainNode: swapchain wrapper not set";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    auto* devicePtr = GetDevice();
    VkResult result = VIXEN_FAULT_FILTER(GetOwningGraph(), Acquire,
        swapChainWrapper->fpAcquireNextImageKHR(
            devicePtr->device,
            swapChainWrapper->scPublicVars.swapChain,
            UINT64_MAX, // Timeout
            presentCompleteSemaphore,
            VK_NULL_HANDLE, // Fence
            &currentImageIndex
        ));

    // VK_ERROR_OUT_OF_DATE_KHR: the acquire genuinely failed -- currentImageIndex is not valid,
    // there is nothing to render/present this frame. Abort it and recreate.
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        NODE_LOG_INFO("SwapChainNode: Swapchain out of date, marking for recreation");

        // Publish render pause event for swapchain recreation. RenderGraph::RenderFrame() checks
        // renderPaused BEFORE this frame's nodes execute and returns early when set, so pausing
        // here is what actually makes "abort this frame" take effect -- MUST NOT be published for
        // the VK_SUBOPTIMAL_KHR case below, or that frame gets silently skipped too despite having
        // a valid, already-signaled acquire (the bug this split fixes).
        if (GetMessageBus()) {
            GetMessageBus()->Publish(
                std::make_unique<EventTypes::RenderPauseEvent>(
                    instanceId,
                    EventTypes::RenderPauseEvent::Reason::SwapChainRecreation,
                    EventTypes::RenderPauseEvent::Action::PAUSE_START
                )
            );
        }

        // Mark node as needing recompilation - will be handled in next Update()
        MarkNeedsRecompile();

        // Return invalid index to skip this frame
        // Recompilation will happen in the next Update() cycle
        return UINT32_MAX;
    }
    // VK_SUBOPTIMAL_KHR: per spec, the acquire SUCCEEDED -- currentImageIndex is valid and
    // presentCompleteSemaphore IS signaled; the driver is only hinting the surface no longer
    // matches ideal presentation parameters (seen routinely on Mesa Dozen/WSL2's Vulkan-over-D3D12
    // path, rarely on llvmpipe). The old code lumped this in with OUT_OF_DATE, discarding a
    // genuinely-acquired image and its signaled semaphore without ever presenting it -- on the
    // NEXT acquire, the validation layer (correctly) flags a later frame's swapchain image as "not
    // acquired since the last present" the moment it's transitioned
    // (UNASSIGNED-non-acquired-swapchain-image-used), because the leaked acquire desynced the
    // acquire/present pairing the whole rest of the API tracks. Render and present this frame
    // normally; only schedule the recreation for later (no pause -- see the OUT_OF_DATE case
    // above for why publishing one here would silently skip this frame anyway).
    else if (result == VK_SUBOPTIMAL_KHR) {
        // A same-extent recreation can never clear SUBOPTIMAL -- on Dozen/WSL2-class drivers that
        // keep reporting it after recreating at the unchanged extent, unconditionally recompiling
        // here turns into a swapchain recreation + transitive recompile cascade every single frame
        // (Widescreen-Perf-Sweep-Findings-2026-07.md rank 3). Only recreate when the surface's
        // currentExtent has actually diverged from the live swapchain's extent.
        VkExtent2D surfaceExtent = swapChainWrapper->QueryCurrentSurfaceExtent(*GetDevice()->gpu);
        const VkExtent2D& liveExtent = swapChainWrapper->scPublicVars.Extent;
        if (surfaceExtent.width == liveExtent.width && surfaceExtent.height == liveExtent.height) {
            NODE_LOG_DEBUG("SwapChainNode: Swapchain suboptimal but extent unchanged ("
                          + std::to_string(liveExtent.width) + "x" + std::to_string(liveExtent.height)
                          + ") - rendering this frame, skipping recreation");
        } else {
            NODE_LOG_INFO("[SwapChainNode] SUBOPTIMAL with extent change "
                          + std::to_string(liveExtent.width) + "x" + std::to_string(liveExtent.height)
                          + " -> " + std::to_string(surfaceExtent.width) + "x" + std::to_string(surfaceExtent.height)
                          + " -- recreating");
            MarkNeedsRecompile();
        }
        return currentImageIndex;
    }
    else if (result != VK_SUCCESS) {
        std::string errorMsg = "SwapChainNode: failed to acquire swapchain image";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    return currentImageIndex;
}

void SwapChainNode::Recreate(uint32_t newWidth, uint32_t newHeight) {
    if (!swapChainWrapper) {
        std::string errorMsg = "SwapChainNode: swapchain wrapper not set";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    width = newWidth;
    height = newHeight;

    // Destroy and recreate swapchain
    swapChainWrapper->DestroySwapChain(GetDevice()->device);
    swapChainWrapper->SetSwapChainExtent(newWidth, newHeight);

    // Note: Swapchain recreation would need full orchestration
    // This should be coordinated with the render graph execution
}

// ====== Compile Phase Helper Methods ======

void SwapChainNode::ValidateCompileInputs(GLFWwindow* window, VkInstance instance) {
    NODE_LOG_DEBUG("[SwapChainNode] Validating compile inputs...");

    if (width == 0 || height == 0) {
        std::string errorMsg = "SwapChainNode: width and height must be greater than 0 (got " +
                               std::to_string(width) + "x" + std::to_string(height) + ")";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    if (window == nullptr) {
        std::string errorMsg = "SwapChainNode: window (GLFWwindow*) is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    if (instance == VK_NULL_HANDLE) {
        std::string errorMsg = "SwapChainNode: VkInstance is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    if (swapChainWrapper == nullptr) {
        std::string errorMsg = "SwapChainNode: swapchain wrapper not set - call SetSwapChainWrapper() before Compile()";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    NODE_LOG_DEBUG("[SwapChainNode] Input validation passed");
}

void SwapChainNode::LoadExtensionsAndCreateSurface(VkInstance instance, GLFWwindow* window) {
    NODE_LOG_INFO("[SwapChainNode] Loading swapchain extensions...");
    NODE_LOG_DEBUG("[SwapChainNode] Instance handle: 0x" + std::to_string(reinterpret_cast<uint64_t>(instance)));

    VkResult result = swapChainWrapper->CreateSwapChainExtensions(instance, GetDevice()->device);
    if (result != VK_SUCCESS) {
        std::string errorMsg = "SwapChainNode: Failed to load swapchain extension function pointers";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    NODE_LOG_INFO("[SwapChainNode] Extension function pointers loaded successfully");

    // The surface is PERSISTENT across recompiles (CleanupImpl keeps it alive on
    // CleanupReason::Recompile -- see CleanupImpl). Only create it once; a later recompile reuses
    // the same VkSurfaceKHR, matching WindowNode's window+surface persistence.
    if (swapChainWrapper->scPublicVars.surface != VK_NULL_HANDLE) {
        NODE_LOG_INFO("[SwapChainNode] Reusing persistent surface across recompile");
        return;
    }

    // Create the surface (cross-platform via GLFW)
    result = swapChainWrapper->CreateSurface(instance, window);
    if (result != VK_SUCCESS) {
        std::string errorMsg = "SwapChainNode: Failed to create VkSurfaceKHR";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    NODE_LOG_INFO("[SwapChainNode] Surface created successfully");
}

void SwapChainNode::SetupFormatsAndCapabilities(uint32_t graphicsQueueIndex) {
    NODE_LOG_INFO("[SwapChainNode] Setting up formats and capabilities...");

    // Get supported surface formats
    swapChainWrapper->GetSupportedFormats(*GetDevice()->gpu);

    // Query surface capabilities and present modes. A zero-extent surface (window minimized / not yet
    // sized) now returns an error instead of the old VulkanSwapChain exit(-1); throw it so the graph's
    // deferred-recompile path retries this node on a later frame rather than the process dying.
    if (auto capsStatus = swapChainWrapper->GetSurfaceCapabilitiesAndPresentMode(*GetDevice()->gpu, width, height);
        !capsStatus.has_value()) {
        throw std::runtime_error("[SwapChainNode] Surface capabilities unavailable: " + capsStatus.error().toString());
    }

    // Select optimal present mode
    swapChainWrapper->ManagePresentMode();

    NODE_LOG_INFO("[SwapChainNode] Formats and capabilities configured");
}

void SwapChainNode::CreateSwapchainAndViews() {
    NODE_LOG_INFO("[SwapChainNode] Creating swapchain and image views...");

    // If this is a recompilation, the OLD swapchain handle is still alive (CleanupImpl only
    // destroyed the per-image views on Recompile -- see CleanupImpl) -- pass it as oldSwapchain so
    // the driver can reuse/hand over presentation state instead of a cold recreation. The old handle
    // is destroyed by CreateSwapChainColorImages itself, only after the new one succeeds.
    VkSwapchainKHR oldSwapchain = swapChainWrapper->scPublicVars.swapChain;
    if (oldSwapchain != VK_NULL_HANDLE) {
        NODE_LOG_INFO("[SwapChainNode] Recreating with oldSwapchain reuse");
    }

    // Create the swapchain with configured settings
    swapChainWrapper->CreateSwapChainColorImages(GetDevice()->device, oldSwapchain);

    // Create image views for each swapchain image
    // Note: We pass VK_NULL_HANDLE for command buffer since image views don't need it
    VkCommandBuffer dummyCmd = VK_NULL_HANDLE;
    swapChainWrapper->CreateColorImageView(GetDevice()->device, dummyCmd);

    // Verify colorBuffers were populated
    NODE_LOG_INFO("[SwapChainNode] ColorBuffers populated: " +
                  std::to_string(swapChainWrapper->scPublicVars.colorBuffers.size()) + " buffers");

    // Verify swapchain was created successfully
    if (swapChainWrapper->scPublicVars.swapChain == VK_NULL_HANDLE) {
        std::string errorMsg = "SwapChainNode: Swapchain handle is null after creation";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    uint32_t imageCount = swapChainWrapper->scPublicVars.swapChainImageCount;
    if (imageCount == 0) {
        std::string errorMsg = "SwapChainNode: No swapchain images were created";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Storm discriminator: every ACTUAL (re)creation logs here at INFO, regardless of what triggered
    // this CompileImpl (initial setup, real resize, or a SUBOPTIMAL guard that decided to proceed).
    // A resize probe should show exactly one of these per genuine size change. scPublicVars.Extent
    // is already authoritative here -- set by GetSurfaceCapabilitiesAndPresentMode, which runs
    // (via SetupFormatsAndCapabilities) before this method.
    NODE_LOG_INFO("[SwapChainNode] swapchain (re)created "
                  + std::to_string(swapChainWrapper->scPublicVars.Extent.width) + "x"
                  + std::to_string(swapChainWrapper->scPublicVars.Extent.height));

    // NOTE: the public Extent is set authoritatively during capability resolution
    // (GetSurfaceCapabilitiesAndPresentMode) to match the actual swapchain image extent. We
    // must NOT overwrite it with the requested window size here — on some surfaces (e.g. WSLg
    // software Vulkan) currentExtent differs from the requested size, and desyncing the public
    // Extent from the image extent leaves the unrendered image remainder as garbage strips.

    NODE_LOG_INFO("[SwapChainNode] Swapchain created with " + std::to_string(imageCount) + " images");
}

void SwapChainNode::PublishCompileOutputs(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[SwapChainNode] Publishing compile outputs...");

    // Output 1: Swapchain handle
    ctx.Out(SwapChainNodeConfig::SWAPCHAIN_HANDLE, swapChainWrapper->scPublicVars.swapChain);

    // Output 2: Pointer to public swapchain variables
    ctx.Out(SwapChainNodeConfig::SWAPCHAIN_PUBLIC, &swapChainWrapper->scPublicVars);

    // FR-3: per-IMAGE sync arrays owned here, sized to the actual swapchain image count.
    ctx.Out(SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, renderCompleteSemaphores);
    ctx.Out(SwapChainNodeConfig::PRESENT_FENCES_ARRAY, presentFences);

    NODE_LOG_DEBUG("[SwapChainNode] Outputs published successfully");
}

void SwapChainNode::CreatePerImageSyncResources() {
    DestroyPerImageSyncResources();  // idempotent: never leak if called without a prior cleanup

    VkDevice device = GetDevice()->device;
    const uint32_t imageCount = swapChainWrapper->scPublicVars.swapChainImageCount;

    // Per-IMAGE render-complete semaphores: signaled by the render submit, waited by present.
    // One per swapchain image, per the Vulkan swapchain-semaphore-reuse guidance.
    renderCompleteSemaphores.resize(imageCount);
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < imageCount; ++i) {
        if (vkCreateSemaphore(device, &semInfo, nullptr, &renderCompleteSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("SwapChainNode: failed to create renderComplete semaphore for image " + std::to_string(i));
        }
    }

    // Per-IMAGE present fences (VK_EXT_swapchain_maintenance1). Left empty if unavailable;
    // SwapChainNode/PresentNode skip fence logic when the array is empty.
    if (GetDevice()->HasCapability("SwapchainMaintenance1")) {
        presentFences.resize(imageCount);
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // start signaled (no wait on first use)
        for (uint32_t i = 0; i < imageCount; ++i) {
            if (vkCreateFence(device, &fenceInfo, nullptr, &presentFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("SwapChainNode: failed to create present fence for image " + std::to_string(i));
            }
        }
    }

    // Per-image in-flight fence tracking: one non-owning slot per image, reset to VK_NULL_HANDLE
    // (image not yet used). Sized here so it always matches the actual swapchain image count.
    imagesInFlight.assign(imageCount, VK_NULL_HANDLE);

    NODE_LOG_INFO("[SwapChainNode] Created " + std::to_string(renderCompleteSemaphores.size())
                  + " renderComplete semaphores + " + std::to_string(presentFences.size())
                  + " present fences (swapchain image count = " + std::to_string(imageCount) + ")");
}

void SwapChainNode::DestroyPerImageSyncResources() {
    auto* dev = GetDevice();
    if (dev != nullptr && dev->device != VK_NULL_HANDLE) {
        VkDevice device = dev->device;
        for (auto& s : renderCompleteSemaphores) {
            if (s != VK_NULL_HANDLE) { vkDestroySemaphore(device, s, nullptr); s = VK_NULL_HANDLE; }
        }
        for (auto& f : presentFences) {
            if (f != VK_NULL_HANDLE) { vkDestroyFence(device, f, nullptr); f = VK_NULL_HANDLE; }
        }
    }
    renderCompleteSemaphores.clear();
    presentFences.clear();
    // Non-owning: the fences belong to FrameSyncNode, so only drop our references (never destroy).
    imagesInFlight.clear();
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::SwapChainNodeType);

// ====== Fail scenarios (compiled out of real builds — see Fail-Scenario-Simulation-Design-2026-07) ======
// Contracts use ScenarioContext::Fail/Skip, NEVER gtest macros (this is an engine TU).
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
namespace FS = Vixen::RenderGraph::FailScenario;
VIXEN_FAIL_SCENARIOS_DECLARE(Vixen::RenderGraph::SwapChainNodeType,
    // KI-003 (RESOLVED by main 9d95bd75 "fix(render): fullscreen/maximize segfault — central frame
    // abort on out-of-date acquire", merged 2026-07-03): forcing Acquire to OUT_OF_DATE/SUBOPTIMAL
    // used to crash GeometryRenderNode via an unguarded renderCompleteSemaphores[imageIndex] index.
    // RenderGraph::AbortCurrentFrame() now stops the sequential Execute loop the instant SwapChainNode
    // publishes the UINT32_MAX sentinel, so every downstream per-image consumer is skipped wholesale
    // this frame. These scenarios are the permanent regression gate for that fix — no knownIssueId.
    VIXEN_SCENARIO(AcquireOutOfDate,
        FS::VkTransient{ .site = FS::FaultSite::Acquire, .result = VK_ERROR_OUT_OF_DATE_KHR },
        [](FS::ScenarioContext& c) {
            // Recovery contract: the deferred-recompile path recreates the swapchain and
            // rendering continues (global criteria already assert progress); the node must
            // not be stuck skipping frames — image index becomes valid again.
            auto* sc = c.SwapChain();
            if (!sc) { c.Fail("no SwapChain node reachable from harness"); return; }
            if (sc->GetCurrentImageIndex() == UINT32_MAX)
                c.Fail("swapchain never recovered from OUT_OF_DATE (image index still invalid)");
        }),
    VIXEN_SCENARIO(AcquireSuboptimal,
        FS::VkTransient{ .site = FS::FaultSite::Acquire, .result = VK_SUBOPTIMAL_KHR },
        [](FS::ScenarioContext& c) {
            auto* sc = c.SwapChain();
            if (!sc) { c.Fail("no SwapChain node reachable from harness"); return; }
            if (sc->GetCurrentImageIndex() == UINT32_MAX)
                c.Fail("swapchain stuck on invalid image index after SUBOPTIMAL");
        })
);
#endif
