#include "Nodes/WindowNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "VulkanApplicationBase.h"
#include <iostream>
#include "Core/NodeLogging.h"
#include "EventBus/Message.h"
#include "EventTypes/RenderGraphEvents.h"

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

void WindowNode::SetupImpl(Context& ctx) {
    NODE_LOG_INFO("[WindowNode] Setup START - Testing incremental compilation");

#ifdef _WIN32
    // Get module handle
    hInstance = GetModuleHandle(nullptr);

    // Register window class
    WNDCLASSEXW winInfo = {};
    winInfo.cbSize = sizeof(WNDCLASSEXW);
    winInfo.style = CS_HREDRAW | CS_VREDRAW;
    winInfo.lpfnWndProc = WindowNode::WndProc;  // Use custom window procedure
    winInfo.cbClsExtra = 0;
    winInfo.cbWndExtra = 0;
    winInfo.hInstance = hInstance;
    winInfo.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    winInfo.hCursor = LoadCursor(nullptr, IDC_ARROW);
    winInfo.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    winInfo.lpszMenuName = nullptr;
    winInfo.lpszClassName = L"VixenGraphWindow";
    winInfo.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&winInfo)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
                NODE_LOG_ERROR("[WindowNode] ERROR: Failed to register window class. GetLastError = " + std::to_string(error));
            throw std::runtime_error("WindowNode: Failed to register window class");
        }
    }

    NODE_LOG_INFO("[WindowNode] Window class registered");
#endif
}

void WindowNode::CompileImpl(Context& ctx) {
    std::cout << "[WindowNode::Compile] START" << std::endl;
    
    // Get parameters using typed names from config
    width = GetParameterValue<uint32_t>(WindowNodeConfig::PARAM_WIDTH, 800);
    height = GetParameterValue<uint32_t>(WindowNodeConfig::PARAM_HEIGHT, 600);

    std::cout << "[WindowNode::Compile] Creating window " << width << "x" << height << std::endl;
    NODE_LOG_INFO("[WindowNode] Creating window " + std::to_string(width) + "x" + std::to_string(height));

#ifdef _WIN32
    // Create window
    RECT wr = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    window = CreateWindowExW(
        0,
        L"VixenGraphWindow",
        L"Vixen Render Graph",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left,
        wr.bottom - wr.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!window) {
        DWORD error = GetLastError();
        std::string errorMsg = "WindowNode: Failed to create window. GetLastError = " + std::to_string(error);
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Store 'this' pointer in window user data for WndProc access
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    NODE_LOG_INFO("[WindowNode] Window created and shown");

    // Create VkSurfaceKHR and store in typed output
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.pNext = nullptr;
    surfaceCreateInfo.hinstance = hInstance;
    surfaceCreateInfo.hwnd = window;

    // Get instance from device (hack: we need VkInstance but only have device)
    // We'll get it from global app
    VulkanApplicationBase* app = static_cast<VulkanApplicationBase*>(
        reinterpret_cast<void*>(GetWindowLongPtrW(window, GWLP_USERDATA))
    );

    // HACK: Use external variables - instance should be passed properly
    extern VkInstance g_VulkanInstance;
    VkInstance instance = g_VulkanInstance;

    if (instance == VK_NULL_HANDLE) {
        NODE_LOG_ERROR("[WindowNode] ERROR: g_VulkanInstance is VK_NULL_HANDLE!");
        throw std::runtime_error("WindowNode: VkInstance not initialized");
    }

    NODE_LOG_INFO("[WindowNode] Creating Win32 surface...");

    VkResult result = vkCreateWin32SurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
    if (result != VK_SUCCESS) {
        NODE_LOG_ERROR("[WindowNode] ERROR: Failed to create Win32 surface: " + std::to_string(result));
        throw std::runtime_error("WindowNode: Failed to create surface");
    }

    // Get destroy function
    fpDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");

    // Store all outputs in type-safe slots (NEW VARIANT API)
    ctx.Out(WindowNodeConfig::SURFACE, surface);
    ctx.Out(WindowNodeConfig::HWND_OUT, window);
    ctx.Out(WindowNodeConfig::HINSTANCE_OUT, hInstance);
    ctx.Out(WindowNodeConfig::WIDTH_OUT, width);
    ctx.Out(WindowNodeConfig::HEIGHT_OUT, height);

    NODE_LOG_INFO("[WindowNode] Surface created and all window data stored in outputs");
#endif
}

void WindowNode::ExecuteImpl(Context& ctx) {
    // Phase F: Store slot index for use in message handlers
    slotIndex = ctx.taskIndex;

    // Process Windows messages
#ifdef _WIN32
    MSG msg;
    while (PeekMessageW(&msg, window, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            shouldClose = true;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
#endif
}

#ifdef _WIN32
LRESULT CALLBACK WindowNode::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Get WindowNode instance from window user data
    WindowNode* windowNode = reinterpret_cast<WindowNode*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CLOSE:
            NODE_LOG_INFO_OBJ(windowNode, "[WindowNode] WM_CLOSE received - initiating graceful shutdown");
            if (windowNode) {
                windowNode->shouldClose = true;
                // Publish window close REQUESTED event to allow graceful shutdown
                // Systems can subscribe to this event to save state, cleanup resources, etc.
                if (windowNode->GetMessageBus()) {
                    windowNode->GetMessageBus()->Publish(
                        std::make_unique<EventBus::WindowCloseEvent>(windowNode->instanceId)
                    );
                }
                // Destroy window immediately to trigger WM_DESTROY -> PostQuitMessage
                DestroyWindow(hWnd);
            }
            return 0;

        case WM_DESTROY:
            NODE_LOG_INFO_OBJ(windowNode, "[WindowNode] WM_DESTROY received");
            PostQuitMessage(0);
            return 0;

        case WM_ENTERSIZEMOVE:
            // User started resizing
            NODE_LOG_INFO_OBJ(windowNode, "[WindowNode] WM_ENTERSIZEMOVE - resize started");
            if (windowNode) {
                windowNode->isResizing = true;
            }
            return 0;

        case WM_EXITSIZEMOVE:
            // User finished resizing
            NODE_LOG_INFO_OBJ(windowNode, "[WindowNode] WM_EXITSIZEMOVE - resize finished");
            if (windowNode) {
                windowNode->isResizing = false;
                windowNode->wasResized = true;

                // Get ACTUAL client area dimensions (not cached member variables!)
                RECT clientRect;
                GetClientRect(hWnd, &clientRect);
                UINT actualWidth = clientRect.right - clientRect.left;
                UINT actualHeight = clientRect.bottom - clientRect.top;

                std::cout << "[WindowNode::WM_EXITSIZEMOVE] GetClientRect returned: " << actualWidth << "x" << actualHeight << std::endl;
                std::cout << "[WindowNode::WM_EXITSIZEMOVE] Old cached dimensions: " << windowNode->width << "x" << windowNode->height << std::endl;

                // Update member variables with actual dimensions
                windowNode->width = actualWidth;
                windowNode->height = actualHeight;

                // Update outputs with actual dimensions (use window's slot index)
                std::cout << "[WindowNode::WM_EXITSIZEMOVE] Calling SetOutput() with: " << actualWidth << "x" << actualHeight
                          << " at slot index " << windowNode->slotIndex << std::endl;
                windowNode->SetOutput(WindowNodeConfig::WIDTH_OUT, windowNode->slotIndex, actualWidth);
                windowNode->SetOutput(WindowNodeConfig::HEIGHT_OUT, windowNode->slotIndex, actualHeight);

                // Verify outputs were updated
                uint32_t readBackWidth = windowNode->GetOut(WindowNodeConfig::WIDTH_OUT);
                uint32_t readBackHeight = windowNode->GetOut(WindowNodeConfig::HEIGHT_OUT);
                std::cout << "[WindowNode::WM_EXITSIZEMOVE] Read back from outputs: width=" << readBackWidth
                          << ", height=" << readBackHeight << std::endl;

                // Publish resize event with ACTUAL dimensions
                if (windowNode->GetMessageBus()) {
                    std::cout << "[WindowNode::WM_EXITSIZEMOVE] Publishing WindowResizedMessage with "
                              << actualWidth << "x" << actualHeight << std::endl;
                    windowNode->GetMessageBus()->Publish(
                        std::make_unique<EventTypes::WindowResizedMessage>(
                            windowNode->instanceId,
                            actualWidth,
                            actualHeight
                        )
                    );
                }
            }
            return 0;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED && windowNode) {
                UINT newWidth = LOWORD(lParam);
                UINT newHeight = HIWORD(lParam);

                // If not actively resizing (e.g., maximize/restore), trigger immediate resize
                if (!windowNode->isResizing && (newWidth != windowNode->width || newHeight != windowNode->height)) {
                    NODE_LOG_INFO_OBJ(windowNode, "[WindowNode] WM_SIZE - window resized to " + std::to_string(newWidth) + "x" + std::to_string(newHeight));
                    windowNode->width = newWidth;
                    windowNode->height = newHeight;
                    windowNode->wasResized = true;

                    // Update outputs with new dimensions so downstream nodes read new values (use window's slot index)
                    std::cout << "[WindowNode::WM_SIZE] BEFORE SetOutput() calls - width=" << newWidth
                              << ", height=" << newHeight << " at slot index " << windowNode->slotIndex << std::endl;
                    windowNode->SetOutput(WindowNodeConfig::WIDTH_OUT, windowNode->slotIndex, newWidth);
                    windowNode->SetOutput(WindowNodeConfig::HEIGHT_OUT, windowNode->slotIndex, newHeight);
                    std::cout << "[WindowNode::WM_SIZE] AFTER SetOutput() calls - verifying..." << std::endl;

                    // Verify outputs were updated
                    uint32_t readBackWidth = windowNode->GetOut(WindowNodeConfig::WIDTH_OUT);
                    uint32_t readBackHeight = windowNode->GetOut(WindowNodeConfig::HEIGHT_OUT);
                    std::cout << "[WindowNode::WM_SIZE] Read back: width=" << readBackWidth
                              << ", height=" << readBackHeight << std::endl;

                    // Publish resize event
                    if (windowNode->GetMessageBus()) {
                        std::cout << "[WindowNode::WM_SIZE] Publishing WindowResizedMessage with "
                                  << newWidth << "x" << newHeight << std::endl;
                        windowNode->GetMessageBus()->Publish(
                            std::make_unique<EventTypes::WindowResizedMessage>(
                                windowNode->instanceId,
                                newWidth,
                                newHeight
                            )
                        );
                    }
                }
            }
            return 0;

        case WM_SYSCOMMAND:
            if (wParam == SC_MINIMIZE && windowNode && windowNode->GetMessageBus()) {
                windowNode->GetMessageBus()->Publish(
                    std::make_unique<EventBus::WindowStateChangeEvent>(
                        windowNode->instanceId,
                        EventBus::WindowStateChangeEvent::State::Minimized
                    )
                );
            } else if (wParam == SC_MAXIMIZE && windowNode && windowNode->GetMessageBus()) {
                windowNode->GetMessageBus()->Publish(
                    std::make_unique<EventBus::WindowStateChangeEvent>(
                        windowNode->instanceId,
                        EventBus::WindowStateChangeEvent::State::Maximized
                    )
                );
            } else if (wParam == SC_RESTORE && windowNode && windowNode->GetMessageBus()) {
                windowNode->GetMessageBus()->Publish(
                    std::make_unique<EventBus::WindowStateChangeEvent>(
                        windowNode->instanceId,
                        EventBus::WindowStateChangeEvent::State::Restored
                    )
                );
            }
            break;

        case WM_ACTIVATE:
            if (windowNode && windowNode->GetMessageBus()) {
                bool focused = (LOWORD(wParam) != WA_INACTIVE);
                windowNode->GetMessageBus()->Publish(
                    std::make_unique<EventBus::WindowStateChangeEvent>(
                        windowNode->instanceId,
                        focused ? EventBus::WindowStateChangeEvent::State::Focused
                               : EventBus::WindowStateChangeEvent::State::Unfocused
                    )
                );
            }
            break;

        default:
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
#endif

void WindowNode::CleanupImpl() {
    NODE_LOG_INFO("[WindowNode] Cleanup");

#ifdef _WIN32
    // Destroy surface using named slot from config (NEW VARIANT API)
    VkSurfaceKHR surface = GetOut(WindowNodeConfig::SURFACE);
    if (surface != VK_NULL_HANDLE && fpDestroySurfaceKHR) {
        extern VkInstance g_VulkanInstance;
        fpDestroySurfaceKHR(g_VulkanInstance, surface, nullptr);
        SetOutput(WindowNodeConfig::SURFACE, 0, VK_NULL_HANDLE);  // Clear typed storage
    }

    // Destroy window
    if (window) {
        DestroyWindow(window);
        window = nullptr;
    }
#endif
}

} // namespace Vixen::RenderGraph
