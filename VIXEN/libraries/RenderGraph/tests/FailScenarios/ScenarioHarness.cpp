#include "ScenarioHarness.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"
#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL); Vulkan-only below, same as InstanceNode.cpp
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstdlib>

AppHarness::AppHarness() {
#ifdef _WIN32
    _putenv_s("VIXEN_HIDDEN_WINDOW", "1");
#else
    setenv("VIXEN_HIDDEN_WINDOW", "1", 1);
#endif
    // Mirror main.cpp:18-43 (the InstanceNode/DeviceNode read these globals), but gate the
    // validation layer on availability (TestVkValidation pattern) instead of a compile def.
    deviceExtensionNames = { VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                             VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
                             VK_KHR_MAINTENANCE_6_EXTENSION_NAME };
    instanceExtensionNames = { VK_KHR_SURFACE_EXTENSION_NAME,
                               VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
                               VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME };
    layerNames = EnabledValidationLayers();
    if (!layerNames.empty()) instanceExtensionNames.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
}

AppHarness::~AppHarness() = default;  // ~VulkanGraphApplication -> DeInitialize

bool AppHarness::DisplayAvailable() {
    return std::getenv("DISPLAY") != nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr;
}

bool AppHarness::Boot() {
    if (!DisplayAvailable()) { bootFailure_ = "no DISPLAY/WAYLAND_DISPLAY (GLFW needs a display server)"; return false; }
    app_ = std::make_unique<VulkanGraphApplication>();
    app_->Initialize();
    app_->Prepare();
    if (!app_->IsPrepared()) { bootFailure_ = "Prepare failed: " + app_->GetLastError(); return false; }
    return true;
}

bool AppHarness::RunFrames(uint32_t n, std::chrono::seconds watchdog) {
    const auto deadline = std::chrono::steady_clock::now() + watchdog;
    for (uint32_t i = 0; i < n; ++i) {
        if (std::chrono::steady_clock::now() > deadline) return false;   // hang = fail
        app_->Update();
        if (!app_->Render()) return false;                               // loop stopped
    }
    return true;
}

NodeInstance* AppHarness::FindByTypeName(const std::string& typeName) {
    auto& graph = Graph();
    const size_t count = graph.GetNodeCount();
    for (uint32_t i = 0; i < count; ++i) {
        NodeInstance* inst = graph.GetInstance(NodeHandle{i});
        if (inst && inst->GetNodeType() && inst->GetNodeType()->GetTypeName() == typeName) return inst;
    }
    return nullptr;
}

SwapChainNode* AppHarness::SwapChain() {
    return static_cast<SwapChainNode*>(FindByTypeName("SwapChain"));
}

WindowNode* AppHarness::WindowNodePtr() {
    return static_cast<WindowNode*>(FindByTypeName("Window"));
}

uint32_t AppHarness::ValidationErrors() const {
    return FailScenario::ValidationErrorCount();
}

bool ScenarioContextImpl::ApplyStimulus(const WindowStimulus& ws) {
    GLFWwindow* w = h_.Window();
    if (!w) return false;
    using K = WindowStimulus::Kind;
    switch (ws.kind) {
        case K::ResizeTo: glfwSetWindowSize(w, (int)ws.width, (int)ws.height); break;
        case K::Maximize: glfwMaximizeWindow(w); break;
        case K::Minimize: glfwIconifyWindow(w);  break;
        case K::Restore:  glfwRestoreWindow(w);  break;
    }
    glfwPollEvents();  // let the WM round-trip; callbacks enqueue into WindowNode
    // Honesty check: did the op actually take effect?
    switch (ws.kind) {
        case K::ResizeTo: { int fw = 0, fh = 0; glfwGetFramebufferSize(w, &fw, &fh);
                            return fw == (int)ws.width && fh == (int)ws.height; }
        case K::Maximize: return glfwGetWindowAttrib(w, GLFW_MAXIMIZED) == GLFW_TRUE;
        case K::Minimize: return glfwGetWindowAttrib(w, GLFW_ICONIFIED) == GLFW_TRUE;
        case K::Restore:  return glfwGetWindowAttrib(w, GLFW_ICONIFIED) == GLFW_FALSE;
    }
    return false;
}
