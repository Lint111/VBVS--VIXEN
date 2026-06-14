#include "Headers.h"
#include "Nodes/WindowNode.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "Message.h"
#include "EventTypes/RenderGraphEvents.h"

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

    // --- the rest is platform-neutral: drain the queued events and publish to the MessageBus ---
    std::vector<WindowEvent> eventsToProcess;
    {
        std::lock_guard<std::recursive_mutex> lock(eventMutex);
        eventsToProcess.swap(pendingEvents);
    }

    for (const auto& event : eventsToProcess) {
        switch (event.type) {
            case WindowEvent::Type::Resize:
                if (event.width != width || event.height != height) {
                    width = event.width;
                    height = event.height;
                    wasResized = true;

                    ctx.Out(WindowNodeConfig::WIDTH_OUT, event.width);
                    ctx.Out(WindowNodeConfig::HEIGHT_OUT, event.height);

                    if (GetMessageBus()) {
                        GetMessageBus()->Publish(
                            std::make_unique<EventTypes::WindowResizedMessage>(
                                instanceId,
                                event.width,
                                event.height
                            )
                        );
                    }

                    NODE_LOG_INFO("[WindowNode] Processed resize: " + std::to_string(event.width) + "x" + std::to_string(event.height));
                }
                break;

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
                        default: continue;
                    }
                    GetMessageBus()->Publish(
                        std::make_unique<EventBus::WindowStateChangeEvent>(instanceId, state)
                    );
                }
                break;
        }
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
