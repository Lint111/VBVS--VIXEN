#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/WindowNodeConfig.h"
#include <memory>
#include <vector>
#include <mutex>

struct GLFWwindow;  // GLFW/glfw3.h is included in the .cpp; the header only needs the handle type.

namespace Vixen::RenderGraph {

/**
 * @brief Node type for window management
 * Type ID: 111
 */
class WindowNodeType : public TypedNodeType<WindowNodeConfig> {
public:
    WindowNodeType(const std::string& typeName = "Window")
        : TypedNodeType<WindowNodeConfig>(typeName) {}
    virtual ~WindowNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Node instance for window creation (cross-platform via GLFW).
 *
 * Uses TypedNode<WindowNodeConfig> for auto-generated type-safe storage. GLFW provides the window,
 * the Vulkan surface (glfwCreateWindowSurface) and input on every platform, so there is no
 * platform-specific code here (GLFW uses Win32 on Windows, X11/Wayland on Linux internally).
 *
 * Parameters: width (uint32_t), height (uint32_t).
 * Outputs (from WindowNodeConfig): SURFACE (VkSurfaceKHR), WINDOW (GLFWwindow*), WIDTH, HEIGHT.
 */
class WindowNode : public TypedNode<WindowNodeConfig> {
public:
    using Base = TypedNode<WindowNodeConfig>;

    WindowNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~WindowNode() override = default;

    // Accessor (cross-platform GLFW handle)
    GLFWwindow* GetWindow() const { return window; }

    // State queries
    bool ShouldClose() const { return shouldClose; }
    bool IsResizing() const { return isResizing; }
    bool WasResized() const { return wasResized; }
    void ClearResizeFlag() { wasResized = false; }

    // Window event queue for deferred processing in Execute(). Public: type visibility only
    // (moved out of private: for the fail-scenario stimulus seam below) — no state exposed.
    struct WindowEvent {
        enum class Type { Resize, Close, Minimize, Maximize, Restore, Focus, Unfocus };
        Type type;
        uint32_t width = 0;   // For Resize events
        uint32_t height = 0;  // For Resize events
    };

#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
    // Fail-scenario seam: enqueue a synthetic event exactly where the GLFW callbacks do; the next
    // ExecuteImpl drains it through the production path (width/height update → MessageBus publish).
    void InjectWindowEvent(WindowEvent::Type type, uint32_t w = 0, uint32_t h = 0);
    size_t PendingEventCountForTest() const;
#endif

    // Drain pendingEvents and publish the resulting bus messages (Close/Resize/Minimize/Maximize/
    // Restore/Focus/Unfocus), independent of node Execute(). RenderGraph::RenderFrame() skips node
    // Execute() entirely while renderPaused (see ExecuteImpl's comment) — including this node's own —
    // so a Restore/Maximize event queued by glfwPollEvents() while minimized would otherwise never
    // reach HandleWindowStateChange, and renderPaused could never clear (permanent freeze on
    // minimize). The host calls this once per frame from Update(), which always runs regardless of
    // pause state. ExecuteImpl still calls it too (via the shared drain), so no behavior change on
    // the normal unpaused path; draining an already-empty queue is a no-op either way.
    void ProcessPendingEvents();

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl(TypedSetupContext& ctx) override;
	void CompileImpl(TypedCompileContext& ctx) override;
	void ExecuteImpl(TypedExecuteContext& ctx) override;
	void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // GLFW event callbacks (replace the Win32 WndProc). They queue WindowEvents for Execute().
    static void OnFramebufferSize(GLFWwindow* w, int width, int height);
    static void OnWindowClose(GLFWwindow* w);
    static void OnWindowFocus(GLFWwindow* w, int focused);
    static void OnWindowIconify(GLFWwindow* w, int iconified);
    static WindowNode* FromGlfw(GLFWwindow* w);

    // Window event queue for deferred processing in Execute(). WindowEvent itself is declared
    // public: above (fail-scenario stimulus seam needs the type visible; no state exposed here).
    std::vector<WindowEvent> pendingEvents;
    mutable std::recursive_mutex eventMutex;  // Protect event queue; mutable for logically-const queries

    // Shared tail of ExecuteImpl / ProcessPendingEvents: publish everything except Resize (which each
    // caller handles itself, since only ExecuteImpl has a ctx to republish graph-slot outputs through).
    void PublishNonResizeEvent(const WindowEvent& event);

    uint32_t width = 0;
    uint32_t height = 0;

    GLFWwindow* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;  // Created with the window; both persist across recompiles, destroyed only at final teardown

    VkInstance vkInstance = VK_NULL_HANDLE;  // Cached from input slot for surface cleanup
    PFN_vkDestroySurfaceKHR fpDestroySurfaceKHR = nullptr;

    // Window state
    bool shouldClose = false;
    bool isResizing = false;
    bool wasResized = false;

    // Phase F: Slot index this window corresponds to (for multi-window support)
    uint32_t slotIndex = 0;
};

} // namespace Vixen::RenderGraph
