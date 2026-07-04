#include "Headers.h"
#include "Nodes/WindowNode.h"
#include "Core/NodeRegistration.h"
#include "Core/FailScenario.h"
#include "Nodes/SwapChainNode.h"  // fail-scenario contracts below call SwapChainNode member accessors
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "Message.h"
#include "EventTypes/RenderGraphEvents.h"
#include <cstdlib>  // std::getenv (VIXEN_HIDDEN_WINDOW, fail-scenario runner)

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL); Vulkan-only below
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Vixen::RenderGraph {

// ====== WindowNodeType ======

std::unique_ptr<NodeInstance> WindowNodeType::CreateInstance(const std::string& instanceName) const {
    return std::make_unique<WindowNode>(instanceName, const_cast<WindowNodeType*>(this));
}

// ====== WindowNode ======

WindowNode::WindowNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<WindowNodeConfig>(instanceName, nodeType)
{
}

WindowNode* WindowNode::FromGlfw(GLFWwindow* w) {
    return reinterpret_cast<WindowNode*>(glfwGetWindowUserPointer(w));
}

void WindowNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[WindowNode] Setup START");

    // GLFW is idempotent: glfwInit() does nothing if already initialised.
    if (!glfwInit()) {
        const char* err = nullptr;
        glfwGetError(&err);
        NODE_LOG_ERROR(std::string("[WindowNode] glfwInit failed: ") + (err ? err : "unknown"));
        throw std::runtime_error("WindowNode: glfwInit failed");
    }
    NODE_LOG_INFO("[WindowNode] GLFW initialised");
}

void WindowNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[WindowNode] Compile START");

    // Get VkInstance from input slot (proper dependency injection)
    vkInstance = ctx.In(WindowNodeConfig::INSTANCE);
    if (vkInstance == VK_NULL_HANDLE) {
        NODE_LOG_ERROR("[WindowNode] ERROR: VkInstance input is VK_NULL_HANDLE!");
        throw std::runtime_error("WindowNode: VkInstance not provided via input slot");
    }

    // The OS window + its surface are PERSISTENT across recompiles (CleanupReason::Recompile keeps
    // them -- see CleanupImpl): only the swapchain follows a recompile, not the window. Create them on
    // the first compile only; a later recompile reuses the same handles and just re-publishes them.
    // width/height track live resize events (ExecuteImpl), so seed them from parameters only on create.
    if (window == nullptr) {
        width = GetParameterValue<uint32_t>(WindowNodeConfig::PARAM_WIDTH, 800);
        height = GetParameterValue<uint32_t>(WindowNodeConfig::PARAM_HEIGHT, 600);

        NODE_LOG_INFO("[WindowNode] Creating window " + std::to_string(width) + "x" + std::to_string(height));

        // Vulkan rendering: tell GLFW not to create an OpenGL context.
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
        // Fail-scenario runner: create the window hidden so the sweep runs unattended.
        if (const char* hid = std::getenv("VIXEN_HIDDEN_WINDOW"); hid && hid[0] == '1')
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#endif

        window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height),
                                  "Vixen Render Graph", nullptr, nullptr);
        if (!window) {
            const char* err = nullptr;
            glfwGetError(&err);
            std::string msg = std::string("WindowNode: glfwCreateWindow failed: ") + (err ? err : "unknown");
            NODE_LOG_ERROR(msg);
            throw std::runtime_error(msg);
        }

        // Route GLFW callbacks back to this instance (replaces the Win32 WndProc + GWLP_USERDATA).
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, &WindowNode::OnFramebufferSize);
        glfwSetWindowCloseCallback(window, &WindowNode::OnWindowClose);
        glfwSetWindowFocusCallback(window, &WindowNode::OnWindowFocus);
        glfwSetWindowIconifyCallback(window, &WindowNode::OnWindowIconify);

        NODE_LOG_INFO("[WindowNode] Window created");

        // Create VkSurfaceKHR (cross-platform; GLFW picks the right platform surface internally).
        VkResult result = glfwCreateWindowSurface(vkInstance, window, nullptr, &surface);
        if (result != VK_SUCCESS) {
            NODE_LOG_ERROR("[WindowNode] ERROR: glfwCreateWindowSurface failed: " + std::to_string(result));
            throw std::runtime_error("WindowNode: Failed to create surface");
        }

        fpDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(vkInstance, "vkDestroySurfaceKHR");
    } else {
        NODE_LOG_INFO("[WindowNode] Reusing persistent window + surface across recompile");
    }

    // Always (re)publish outputs so downstream nodes (SwapChainNode) read the current handles.
    ctx.Out(WindowNodeConfig::SURFACE, surface);
    ctx.Out(WindowNodeConfig::WINDOW, window);
    ctx.Out(WindowNodeConfig::WIDTH_OUT, width);
    ctx.Out(WindowNodeConfig::HEIGHT_OUT, height);

    NODE_LOG_INFO("[WindowNode] Outputs published (width=" + std::to_string(width) +
                  ", height=" + std::to_string(height) + ")");
}

void WindowNode::RecordPendingResize(uint32_t w, uint32_t h) {
    hasPendingResize_ = true;
    pendingResizeWidth_ = w;
    pendingResizeHeight_ = h;
    lastResizeEventTime_ = glfwGetTime();
}

bool WindowNode::PendingResizeIsSettled() const {
    return hasPendingResize_ && (glfwGetTime() - lastResizeEventTime_) >= kResizeDebounceSeconds;
}

void WindowNode::ExecuteImpl(TypedExecuteContext& ctx) {
    slotIndex = ctx.taskIndex;

    // Input is pumped once per frame by the application main loop: glfwPollEvents() is the global OS
    // message pump and must run on the main thread every iteration -- even while rendering is paused,
    // which RenderFrame() short-circuits before reaching node Execute -- so it cannot live here. That
    // single pump fires this node's GLFW callbacks and fills the event queue we drain below; here we
    // only read the resulting should-close flag.
    if (window && glfwWindowShouldClose(window)) {
        shouldClose = true;
    }

    // Drain + publish (shared with the always-runs ProcessPendingEvents(), see the header comment).
    // Resize is handled separately below because republishing its graph-slot outputs needs ctx, which
    // only exists during a real node Execute() -- i.e. exactly the case renderPaused skips, so a resize
    // queued while paused is instead re-applied by CompileImpl on the next (unpaused) recompile.
    std::vector<WindowEvent> eventsToProcess;
    {
        std::lock_guard<std::recursive_mutex> lock(eventMutex);
        eventsToProcess.swap(pendingEvents);
    }

    for (const auto& event : eventsToProcess) {
        if (event.type == WindowEvent::Type::Resize) {
            // Debounce (trailing edge): record the latest size but don't act yet -- a live drag fires
            // roughly one of these per tick, and acting on every one used to trigger a full
            // SwapChainNode recreation + transitive recompile of ~10 nodes per event (16 waves
            // observed for a single "slow" drag), starving the render loop enough that the compositor
            // showed stale/ghosted frames and input lagged. Applied below, once settled.
            if (event.width != width || event.height != height) {
                RecordPendingResize(event.width, event.height);
            }
            continue;
        }
        PublishNonResizeEvent(event);
    }

    // Apply a settled pending resize (may fire on a tick with no new event this frame -- that's the
    // whole point of a trailing-edge debounce: react once activity has actually stopped).
    if (PendingResizeIsSettled()) {
        width = pendingResizeWidth_;
        height = pendingResizeHeight_;
        wasResized = true;
        hasPendingResize_ = false;

        ctx.Out(WindowNodeConfig::WIDTH_OUT, width);
        ctx.Out(WindowNodeConfig::HEIGHT_OUT, height);

        // Mark self dirty too (mirrors ProcessPendingEvents): SwapChainNode independently marks
        // itself dirty via its own WindowResizedMessage subscription, but PickIdTargetNode/
        // VoxelSelectionProviderNode are dependents of THIS node (not SwapChainNode) for their
        // WIDTH/HEIGHT/VIEWPORT_WIDTH/VIEWPORT_HEIGHT inputs. Without this, they never enter the
        // resize recompile wave: the pick-ID ring stays stale at the old extent while
        // VoxelSelectionProviderNode reads the new extent live every frame, so the next click's
        // readback copy samples outside the ring image's bounds (undefined behavior, observed as a
        // crash on lavapipe).
        MarkNeedsRecompile();

        if (GetMessageBus()) {
            GetMessageBus()->Publish(
                std::make_unique<EventTypes::WindowResizedMessage>(instanceId, width, height)
            );
        }

        NODE_LOG_INFO("[WindowNode] Processed resize: " + std::to_string(width) + "x" + std::to_string(height));
    }
}

void WindowNode::ProcessPendingEvents() {
    // Always-runs counterpart to the drain half of ExecuteImpl (see the header comment for why this
    // needs to exist separately: RenderFrame() skips node Execute() entirely while renderPaused).
    if (window && glfwWindowShouldClose(window)) {
        shouldClose = true;
    }

    std::vector<WindowEvent> eventsToProcess;
    {
        std::lock_guard<std::recursive_mutex> lock(eventMutex);
        eventsToProcess.swap(pendingEvents);
    }

    for (const auto& event : eventsToProcess) {
        if (event.type == WindowEvent::Type::Resize) {
            // Same trailing-edge debounce as ExecuteImpl -- record only, apply once settled below.
            if (event.width != width || event.height != height) {
                RecordPendingResize(event.width, event.height);
            }
            continue;
        }
        PublishNonResizeEvent(event);
    }

    // No ctx here (this runs outside node Execute()) -- update state + bus-publish the resize so
    // SwapChainNode still hears about it and marks itself dirty. The graph-slot outputs
    // (WIDTH_OUT/HEIGHT_OUT) can only be republished by CompileImpl, so mark THIS node for recompile
    // too: WindowNode recompiles first in execution order, CompileImpl republishes the slots
    // (window/surface persist across recompile by design), and the dependent-marking cascade
    // refreshes SwapChainNode + the pick/viewport consumers. While paused the recompile defers and
    // runs on the restore Update -- exactly the desired timing.
    if (PendingResizeIsSettled()) {
        width = pendingResizeWidth_;
        height = pendingResizeHeight_;
        wasResized = true;
        hasPendingResize_ = false;
        MarkNeedsRecompile();

        if (GetMessageBus()) {
            GetMessageBus()->Publish(
                std::make_unique<EventTypes::WindowResizedMessage>(instanceId, width, height)
            );
        }

        NODE_LOG_INFO("[WindowNode] Processed resize (paused path): " + std::to_string(width) + "x" + std::to_string(height));
    }
}

void WindowNode::PublishNonResizeEvent(const WindowEvent& event) {
    switch (event.type) {
        case WindowEvent::Type::Close:
            shouldClose = true;
            if (GetMessageBus()) {
                GetMessageBus()->Publish(
                    std::make_unique<EventBus::WindowCloseEvent>(instanceId)
                );
            }
            break;

        case WindowEvent::Type::Minimize:
        case WindowEvent::Type::Maximize:
        case WindowEvent::Type::Restore:
        case WindowEvent::Type::Focus:
        case WindowEvent::Type::Unfocus:
            if (GetMessageBus()) {
                EventBus::WindowStateChangeEvent::State state;
                switch (event.type) {
                    case WindowEvent::Type::Minimize: state = EventBus::WindowStateChangeEvent::State::Minimized; break;
                    case WindowEvent::Type::Maximize: state = EventBus::WindowStateChangeEvent::State::Maximized; break;
                    case WindowEvent::Type::Restore: state = EventBus::WindowStateChangeEvent::State::Restored; break;
                    case WindowEvent::Type::Focus: state = EventBus::WindowStateChangeEvent::State::Focused; break;
                    case WindowEvent::Type::Unfocus: state = EventBus::WindowStateChangeEvent::State::Unfocused; break;
                    default: return;
                }
                GetMessageBus()->Publish(
                    std::make_unique<EventBus::WindowStateChangeEvent>(instanceId, state)
                );
            }
            break;

        default:
            break;  // Resize is handled by the caller, not here.
    }
}

// ====== GLFW callbacks (replace the Win32 WndProc) ======

void WindowNode::OnFramebufferSize(GLFWwindow* w, int width, int height) {
    WindowNode* self = FromGlfw(w);
    if (!self || width <= 0 || height <= 0) return;  // 0-size = minimised; ignore
    auto uw = static_cast<uint32_t>(width), uh = static_cast<uint32_t>(height);
    if (uw == self->width && uh == self->height) return;  // only on a real change
    std::lock_guard<std::recursive_mutex> lock(self->eventMutex);
    self->pendingEvents.push_back({WindowEvent::Type::Resize, uw, uh});
}

void WindowNode::OnWindowClose(GLFWwindow* w) {
    WindowNode* self = FromGlfw(w);
    if (!self) return;
    std::lock_guard<std::recursive_mutex> lock(self->eventMutex);
    self->pendingEvents.push_back({WindowEvent::Type::Close});
}

void WindowNode::OnWindowFocus(GLFWwindow* w, int focused) {
    WindowNode* self = FromGlfw(w);
    if (!self) return;
    std::lock_guard<std::recursive_mutex> lock(self->eventMutex);
    self->pendingEvents.push_back({focused ? WindowEvent::Type::Focus : WindowEvent::Type::Unfocus});
}

void WindowNode::OnWindowIconify(GLFWwindow* w, int iconified) {
    WindowNode* self = FromGlfw(w);
    if (!self) return;
    std::lock_guard<std::recursive_mutex> lock(self->eventMutex);
    self->pendingEvents.push_back({iconified ? WindowEvent::Type::Minimize : WindowEvent::Type::Restore});
}

#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
void WindowNode::InjectWindowEvent(WindowEvent::Type type, uint32_t w, uint32_t h) {
    std::lock_guard<std::recursive_mutex> lock(eventMutex);
    pendingEvents.push_back(WindowEvent{ type, w, h });
}
size_t WindowNode::PendingEventCountForTest() const {
    std::lock_guard<std::recursive_mutex> lock(eventMutex);
    return pendingEvents.size();
}
#endif

void WindowNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Window + surface are PERSISTENT: a recompile (e.g. swapchain recreation) must NOT destroy them --
    // only the swapchain follows recompiles. Tear them down solely on final application teardown.
    if (ctx.reason != CleanupReason::FinalTeardown) {
        NODE_LOG_INFO("[WindowNode] Cleanup (recompile) - keeping persistent window + surface");
        return;
    }

    NODE_LOG_INFO("[WindowNode] Cleanup (final teardown) - destroying surface + window");

    if (surface != VK_NULL_HANDLE && fpDestroySurfaceKHR && vkInstance != VK_NULL_HANDLE) {
        fpDestroySurfaceKHR(vkInstance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    // Keep the published SURFACE output handle consistent with the now-destroyed surface.
    if (Resource* surfaceRes = NodeInstance::GetOutput(WindowNodeConfig::SURFACE.index, 0)) {
        surfaceRes->SetHandle<VkSurfaceKHR>(VK_NULL_HANDLE);
    }

    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::WindowNodeType);

#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
namespace FS = Vixen::RenderGraph::FailScenario;
VIXEN_FAIL_SCENARIOS_DECLARE(Vixen::RenderGraph::WindowNodeType,
    VIXEN_SCENARIO(ResizeLargeExtentJump,   // deterministic fullscreen-class repro (extent jump)
        FS::WindowStimulus{ .kind = FS::WindowStimulus::Kind::ResizeTo, .width = 1920, .height = 1080 },
        [](FS::ScenarioContext& c) {
            auto* sc = c.SwapChain();
            if (!sc) { c.Fail("no SwapChain node reachable from harness"); return; }
            if (sc->GetWidth() != 1920u || sc->GetHeight() != 1080u)
                c.Fail("swapchain never recreated at the new extent: got " +
                       std::to_string(sc->GetWidth()) + "x" + std::to_string(sc->GetHeight()) +
                       ", expected 1920x1080");
        }),
    VIXEN_SCENARIO(MaximizeLikeFullscreenButton,   // the literal user gesture (WM-dependent)
        FS::WindowStimulus{ .kind = FS::WindowStimulus::Kind::Maximize },
        [](FS::ScenarioContext& c) {
            // Extent after maximize is WM-decided: assert swapchain matches the REAL framebuffer.
            int fw = 0, fh = 0;
            glfwGetFramebufferSize(c.Window(), &fw, &fh);
            auto* sc = c.SwapChain();
            if (!sc) { c.Fail("no SwapChain node reachable from harness"); return; }
            if (sc->GetWidth() != static_cast<uint32_t>(fw) || sc->GetHeight() != static_cast<uint32_t>(fh))
                c.Fail("swapchain extent " + std::to_string(sc->GetWidth()) + "x" +
                       std::to_string(sc->GetHeight()) + " does not match maximized framebuffer " +
                       std::to_string(fw) + "x" + std::to_string(fh));
        }),
    VIXEN_SCENARIO(MinimizeThenRestore,
        FS::WindowStimulus{ .kind = FS::WindowStimulus::Kind::Minimize },
        [](FS::ScenarioContext& c) {
            // Minimized: must not crash or hang (global criteria cover the frames while iconified).
            // Then restore and require rendering to resume.
            if (!c.ApplyStimulus(FS::WindowStimulus{ .kind = FS::WindowStimulus::Kind::Restore }))
                c.Skip("WM refused restore on the hidden window");
            if (!c.RunFrames(30)) c.Fail("rendering did not resume after restore");
        })
);
#endif
