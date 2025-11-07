#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/WindowNodeConfig.h"
#include <memory>

#ifdef _WIN32
#include <Windows.h>
#endif

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
 * @brief Node instance for window creation
 *
 * Uses TypedNode<WindowNodeConfig> for auto-generated type-safe storage.
 *
 * Parameters:
 * - width (uint32_t): Window width
 * - height (uint32_t): Window height
 *
 * Outputs (auto-generated from WindowNodeConfig):
 * - SURFACE: VkSurfaceKHR (index 0, required)
 */
class WindowNode : public TypedNode<WindowNodeConfig> {
public:
    WindowNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~WindowNode() override = default;

    // Accessors
#ifdef _WIN32
    HWND GetWindow() const { return window; }
#endif

    // State queries
    bool ShouldClose() const { return shouldClose; }
    bool IsResizing() const { return isResizing; }
    bool WasResized() const { return wasResized; }
    void ClearResizeFlag() { wasResized = false; }

protected:
	using TypedSetupContext = typename Base::TypedSetupContext;
	using TypedCompileContext = typename Base::TypedCompileContext;
	using TypedExecuteContext = typename Base::TypedExecuteContext;
	using TypedCleanupContext = typename Base::TypedCleanupContext;

	// Template method pattern - override *Impl() methods
	void SetupImpl(TypedSetupContext& ctx) override;
	void CompileImpl(TypedCompileContext& ctx) override;
	void ExecuteImpl(TypedExecuteContext& ctx) override;
	void CleanupImpl(TypedCleanupContext& ctx) override;

private:
#ifdef _WIN32
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

    // Window event queue for deferred processing in Execute()
    struct WindowEvent {
        enum class Type { Resize, Close, Minimize, Maximize, Restore, Focus, Unfocus };
        Type type;
        uint32_t width = 0;   // For Resize events
        uint32_t height = 0;  // For Resize events
    };
    std::vector<WindowEvent> pendingEvents;
    std::mutex eventMutex;  // Protect event queue from Win32 callback thread

    uint32_t width = 0;
    uint32_t height = 0;

#ifdef _WIN32
    HINSTANCE hInstance = nullptr;
    HWND window = nullptr;
#endif

    PFN_vkDestroySurfaceKHR fpDestroySurfaceKHR = nullptr;

    // Window state
    bool shouldClose = false;
    bool isResizing = false;
    bool wasResized = false;

    // Phase F: Slot index this window corresponds to (for multi-window support)
    uint32_t slotIndex = 0;
};

} // namespace Vixen::RenderGraph
