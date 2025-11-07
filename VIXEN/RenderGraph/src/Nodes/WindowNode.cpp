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

void WindowNode::SetupImpl(TypedSetupContext& ctx) {
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

void WindowNode::CompileImpl(TypedCompileContext& ctx) {
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

void WindowNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Phase F: Store slot index for use in message handlers
    slotIndex = ctx.taskIndex;

    // Process Windows messages (fills event queue)
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

    // Process queued events with proper Context access
    std::vector<WindowEvent> eventsToProcess;
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        eventsToProcess.swap(pendingEvents);  // Take ownership of events
    }

    for (const auto& event : eventsToProcess) {
        switch (event.type) {
            case WindowEvent::Type::Resize:
                width = event.width;
                height = event.height;
                wasResized = true;

                // Update outputs using Context (proper phase-aware access)
                ctx.Out(WindowNodeConfig::WIDTH_OUT, event.width);
                ctx.Out(WindowNodeConfig::HEIGHT_OUT, event.height);

                // Publish event
                if (GetMessageBus()) {
                    GetMessageBus()->Publish(
                        std::make_unique<EventTypes::WindowResizedMessage>(
                            instanceId,
                            event.width,
                            event.height
                        )
                    );
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

#ifdef _WIN32
LRESULT CALLBACK WindowNode::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Get WindowNode instance from window user data
    WindowNode* windowNode = reinterpret_cast<WindowNode*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CLOSE:
            NODE_LOG_INFO_OBJ(windowNode, "[WindowNode] WM_CLOSE received - queuing close event");
            if (windowNode) {
                // Queue close event for processing in Execute()
                std::lock_guard<std::mutex> lock(windowNode->eventMutex);
                windowNode->pendingEvents.push_back({WindowNode::WindowEvent::Type::Close});
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

                // Queue resize event for processing in Execute()
                std::cout << "[WindowNode::WM_EXITSIZEMOVE] Queuing resize event: " << actualWidth << "x" << actualHeight << std::endl;
                std::lock_guard<std::mutex> lock(windowNode->eventMutex);
                windowNode->pendingEvents.push_back({WindowNode::WindowEvent::Type::Resize, actualWidth, actualHeight});
            }
            return 0;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED && windowNode) {
                UINT newWidth = LOWORD(lParam);
                UINT newHeight = HIWORD(lParam);

                // If not actively resizing (e.g., maximize/restore), queue resize event
                if (!windowNode->isResizing && (newWidth != windowNode->width || newHeight != windowNode->height)) {
                    NODE_LOG_INFO_OBJ(windowNode, "[WindowNode] WM_SIZE - queuing resize event: " + std::to_string(newWidth) + "x" + std::to_string(newHeight));

                    // Queue event for processing in Execute() phase with proper Context
                    std::lock_guard<std::mutex> lock(windowNode->eventMutex);
                    windowNode->pendingEvents.push_back({WindowNode::WindowEvent::Type::Resize, newWidth, newHeight});
                }
            }
            return 0;

        case WM_SYSCOMMAND:
            if (windowNode) {
                std::lock_guard<std::mutex> lock(windowNode->eventMutex);
                if (wParam == SC_MINIMIZE) {
                    windowNode->pendingEvents.push_back({WindowNode::WindowEvent::Type::Minimize});
                } else if (wParam == SC_MAXIMIZE) {
                    windowNode->pendingEvents.push_back({WindowNode::WindowEvent::Type::Maximize});
                } else if (wParam == SC_RESTORE) {
                    windowNode->pendingEvents.push_back({WindowNode::WindowEvent::Type::Restore});
                }
            }
            break;

        case WM_ACTIVATE:
            if (windowNode) {
                bool focused = (LOWORD(wParam) != WA_INACTIVE);
                std::lock_guard<std::mutex> lock(windowNode->eventMutex);
                windowNode->pendingEvents.push_back({
                    focused ? WindowNode::WindowEvent::Type::Focus : WindowNode::WindowEvent::Type::Unfocus
                });
            }
            break;

        default:
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
#endif

void WindowNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[WindowNode] Cleanup");

#ifdef _WIN32
    // Destroy surface - access output directly via NodeInstance (CleanupContext has no I/O)
    Resource* surfaceRes = NodeInstance::GetOutput(WindowNodeConfig::SURFACE.index, 0);
    if (surfaceRes) {
        VkSurfaceKHR surface = surfaceRes->GetHandle<VkSurfaceKHR>();
        if (surface != VK_NULL_HANDLE && fpDestroySurfaceKHR) {
            extern VkInstance g_VulkanInstance;
            fpDestroySurfaceKHR(g_VulkanInstance, surface, nullptr);
            surfaceRes->SetHandle<VkSurfaceKHR>(VK_NULL_HANDLE);  // Clear typed storage
        }
    }

    // Destroy window
    if (window) {
        DestroyWindow(window);
        window = nullptr;
    }
#endif
}

} // namespace Vixen::RenderGraph
