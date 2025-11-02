#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Nodes/WindowNodeConfig.h"
#include <memory>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Vixen::RenderGraph {

/**
 * @brief Node type for window management
 * Type ID: 111
 */
class WindowNodeType : public NodeType {
public:
    WindowNodeType(const std::string& typeName = "Window");
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
    virtual ~WindowNode();

    // Accessors
#ifdef _WIN32
    HWND GetWindow() const { return window; }
#endif
    VkSurfaceKHR GetSurface() const {
        // Type-safe access using named slot from config (NEW VARIANT API)
        return GetOut(WindowNodeConfig::SURFACE);
    }

    // State queries
    bool ShouldClose() const { return shouldClose; }
    bool IsResizing() const { return isResizing; }
    bool WasResized() const { return wasResized; }
    void ClearResizeFlag() { wasResized = false; }

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl() override;
	void CompileImpl() override;
	void ExecuteImpl(TaskContext& ctx) override;
	void CleanupImpl() override;

private:
#ifdef _WIN32
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

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
};

} // namespace Vixen::RenderGraph
