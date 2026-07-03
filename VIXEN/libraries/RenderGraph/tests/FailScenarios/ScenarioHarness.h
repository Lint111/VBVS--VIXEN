#pragma once
#include "Core/FailScenario.h"
#include "Core/RenderGraph.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/WindowNode.h"
#include "VulkanGraphApplication.h"
#include <gtest/gtest.h>  // ADD_FAILURE() used inline in ScenarioContextImpl::Fail()
#include <chrono>
#include <string>

using namespace Vixen::RenderGraph;
using namespace Vixen::RenderGraph::FailScenario;

// Boots the REAL default application graph (VixenApp) with a hidden window and drives its
// Update/Render loop exactly as main.cpp does. One AppHarness per scenario (boot-per-scenario
// isolation — spec §3.5).
class AppHarness {
public:
    AppHarness();                       // sets VIXEN_HIDDEN_WINDOW=1 + extension/layer globals
    ~AppHarness();                      // DeInitialize via app dtor
    bool Boot();                        // Initialize + Prepare; false (with reason) on failure
    const std::string& BootFailureReason() const { return bootFailure_; }
    // Runs n frames (Update+Render). Returns false if Render() reports stop OR the wall-clock
    // watchdog (default 60 s) expires — a hang IS a failure (historical resize deadlock class).
    bool RunFrames(uint32_t n, std::chrono::seconds watchdog = std::chrono::seconds(60));
    VulkanGraphApplication& App() { return *app_; }
    Vixen::RenderGraph::RenderGraph& Graph() { return *app_->GetRenderGraph(); }
    GLFWwindow* Window() { return app_->GetWindowHandle(); }
    SwapChainNode* SwapChain();         // first instance whose type name == "SwapChain", else nullptr
    WindowNode*   WindowNodePtr();      // first instance whose type name == "Window", else nullptr
    NodeInstance* FindByTypeName(const std::string& typeName);  // first instance of that type, else nullptr
    uint32_t ValidationErrors() const;  // FailScenario::ValidationErrorCount()
    static bool DisplayAvailable();     // DISPLAY or WAYLAND_DISPLAY set
private:
    std::unique_ptr<VulkanGraphApplication> app_;
    std::string bootFailure_;
};

// Thrown by Skip(); the runner catches it and converts to GTEST_SKIP.
struct SkipScenario { std::string reason; };

class ScenarioContextImpl final : public FailScenario::ScenarioContext {
public:
    explicit ScenarioContextImpl(AppHarness& h) : h_(h) {}
    bool RunFrames(uint32_t n) override { return h_.RunFrames(n); }
    void ArmFault(FaultSite s, VkResult r) override { h_.Graph().GetFaultInjector()->ArmOnce(s, r); }
    // Applies a stimulus via REAL GLFW window ops (resize/iconify/maximize/restore) so the real
    // callbacks fire; returns false if the WM visibly refused (callers Skip — never fake-pass).
    bool ApplyStimulus(const WindowStimulus& ws) override;
    SwapChainNode* SwapChain() override { return h_.SwapChain(); }
    GLFWwindow* Window() override { return h_.Window(); }
    Vixen::RenderGraph::RenderGraph* Graph() override { return &h_.Graph(); }
    uint32_t ValidationErrors() const override { return h_.ValidationErrors(); }
    void Fail(const std::string& msg) override { ADD_FAILURE() << msg; }
    [[noreturn]] void Skip(const std::string& msg) override { throw SkipScenario{msg}; }
private:
    AppHarness& h_;
};
