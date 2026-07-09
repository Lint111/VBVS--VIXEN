// Dynamic registration: one test per declared scenario (gtest RegisterTest).
#include "ScenarioHarness.h"
#include "Nodes/InputNode.h"
#include "Nodes/PickIdTargetNode.h"
#include "Nodes/SwapChainNode.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL); Vulkan-only below
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

TEST(FailScenarioSweep, BootWarmupTeardown) {
    if (!AppHarness::DisplayAvailable()) GTEST_SKIP() << "no DISPLAY/WAYLAND_DISPLAY (GLFW needs a display server)";
    AppHarness h;
    ASSERT_TRUE(h.Boot()) << h.BootFailureReason();
    EXPECT_TRUE(h.RunFrames(30));                      // warmup: 30 frames, watchdog-bounded
    EXPECT_EQ(h.ValidationErrors(), 0u);
}   // ~AppHarness tears down cleanly (DeInitialize)

// Regression for the click-after-live-resize crash: WindowNode::ExecuteImpl's resize branch
// (window NOT paused/minimized — the common interactive-resize case) republishes WIDTH_OUT/
// HEIGHT_OUT and the WindowResizedMessage but never called MarkNeedsRecompile() on itself, unlike
// its ProcessPendingEvents sibling. PickIdTargetNode and VoxelSelectionProviderNode are dependents
// of WindowNode (not SwapChainNode), so they never entered the resize recompile wave: the pick-ID
// ring stayed stale at the old resolution while VoxelSelectionProviderNode read the new, larger
// VIEWPORT_WIDTH/HEIGHT live every frame. The next click's vkCmdCopyImageToBuffer at
// (width/2, height/2) then samples outside the stale image's bounds — undefined behavior, observed
// as a real segfault interactively on lavapipe, but not guaranteed to fault every run (depends on
// allocator/page layout) — so this asserts directly on the ring extent, not on a crash.
TEST(FailScenarioSweep, LiveResizeRecompilesPickIdRing) {
    if (!AppHarness::DisplayAvailable()) GTEST_SKIP() << "no DISPLAY/WAYLAND_DISPLAY (GLFW needs a display server)";
    AppHarness h;
    ASSERT_TRUE(h.Boot()) << h.BootFailureReason();
    ASSERT_TRUE(h.RunFrames(30)) << "warmup did not complete";

    auto* pickIdTarget = static_cast<PickIdTargetNode*>(h.FindByTypeName("PickIdTarget"));
    ASSERT_NE(pickIdTarget, nullptr);
    const uint32_t ringWidthBefore = pickIdTarget->RingWidthForTest();
    const uint32_t ringHeightBefore = pickIdTarget->RingHeightForTest();

    // Live (unpaused) resize -- the exact path that skips MarkNeedsRecompile(). Not Maximize/
    // Minimize/Restore: those route through ProcessPendingEvents, which already calls it.
    GLFWwindow* w = h.Window();
    ASSERT_NE(w, nullptr);
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(w, &fw, &fh);
    glfwSetWindowSize(w, fw * 2, fh * 2);
    // The resize debounce (WindowNode.cpp) holds a settled size for kResizeDebounceSeconds before
    // applying it, so a real wall-clock wait is needed here, not just more ticks.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_TRUE(h.RunFrames(5)) << "resize did not process";

    int fw2 = 0, fh2 = 0;
    glfwGetFramebufferSize(w, &fw2, &fh2);
    ASSERT_TRUE(fw2 != fw || fh2 != fh) << "resize did not actually change the framebuffer extent";

    EXPECT_EQ(pickIdTarget->RingWidthForTest(), static_cast<uint32_t>(fw2))
        << "pick-ID ring did not follow the live resize (still " << ringWidthBefore
        << "x" << ringHeightBefore << ") -- the next click reads outside its bounds";
    EXPECT_EQ(pickIdTarget->RingHeightForTest(), static_cast<uint32_t>(fh2));

    // A click still must not crash the process regardless (defense in depth).
    auto* input = static_cast<InputNode*>(h.FindByTypeName("Input"));
    ASSERT_NE(input, nullptr);
    FailScenario::ResetValidationErrorCount();
    input->InjectMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    input->InjectMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    EXPECT_TRUE(h.RunFrames(30)) << "rendering did not continue after a post-resize click "
                                    "(hang or stop) -- a crash fails this whole test process";
}

// Regression for the resize-storm instability report: a live drag-resize under WSLg fires roughly
// one WindowResizedMessage per tick, and with no debounce each one triggers a full SwapChainNode
// recreation (surface work, per-image sync rebuild, transitive recompile of ~10 nodes) -- 16 waves
// were observed for a single "slow" drag gesture, starving the render loop enough that WSLg's
// compositor showed stale/ghosted frames and input (including ESC) lagged behind. This injects a
// burst of distinct resize sizes across many ticks (simulating a fast drag) and asserts SwapChainNode
// does not recompile once per event.
TEST(FailScenarioSweep, ResizeBurstDoesNotRecompileOncePerEvent) {
    if (!AppHarness::DisplayAvailable()) GTEST_SKIP() << "no DISPLAY/WAYLAND_DISPLAY (GLFW needs a display server)";
    AppHarness h;
    ASSERT_TRUE(h.Boot()) << h.BootFailureReason();
    ASSERT_TRUE(h.RunFrames(30)) << "warmup did not complete";

    auto* window = h.WindowNodePtr();
    ASSERT_NE(window, nullptr);
    auto* swapChain = static_cast<SwapChainNode*>(h.FindByTypeName("SwapChain"));
    ASSERT_NE(swapChain, nullptr);
    const uint32_t compilesBefore = swapChain->CompileCountForTest();

    // 20 distinct sizes, one per tick -- mirrors the ~1-2 resize events/tick observed during an
    // actual drag (test_fail_scenario_registry.cpp already proves InjectWindowEvent's queuing).
    using WE = Vixen::RenderGraph::WindowNode::WindowEvent;
    for (uint32_t i = 0; i < 20; ++i) {
        window->InjectWindowEvent(WE::Type::Resize, 500 + i * 4, 500 + i * 4);
        ASSERT_TRUE(h.RunFrames(1)) << "tick " << i << " did not process";
    }
    // Let the debounce window (real wall-clock time, glfwGetTime()-based) actually elapse -- a tight
    // headless RunFrames loop completes far faster than 100ms, so this needs a real sleep, not more
    // ticks.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_TRUE(h.RunFrames(5)) << "post-burst settle did not complete";

    const uint32_t compilesAfter = swapChain->CompileCountForTest();
    EXPECT_LE(compilesAfter - compilesBefore, 3u)
        << "20 resize events in a burst caused " << (compilesAfter - compilesBefore)
        << " SwapChainNode recompiles -- expected a debounce to collapse them to a small, "
           "bounded number, not one-per-event";

    // The final size must still take effect (debounce must not drop the resize entirely).
    EXPECT_EQ(swapChain->GetWidth(), 500u + 19u * 4u);
    EXPECT_EQ(swapChain->GetHeight(), 500u + 19u * 4u);
}

namespace {

class ScenarioCase : public ::testing::Test {
public:
    ScenarioCase(std::string nodeType, ScenarioDecl decl)
        : nodeType_(std::move(nodeType)), decl_(std::move(decl)) {}
    void TestBody() override {
        if (!AppHarness::DisplayAvailable()) GTEST_SKIP() << "no display server";
        AppHarness h;
        ASSERT_TRUE(h.Boot()) << h.BootFailureReason();
        if (!h.FindByTypeName(nodeType_))
            GTEST_SKIP() << "node type '" << nodeType_ << "' not present in the assembled graph";
        ASSERT_TRUE(h.RunFrames(30)) << "warmup did not complete";           // criterion 3 (pre)
        FailScenario::ResetValidationErrorCount();
        ScenarioContextImpl ctx(h);

        try {
            if (auto* vt = std::get_if<VkTransient>(&decl_.stimulus)) {
                ctx.ArmFault(vt->site, vt->result);
            } else {
                const auto& ws = std::get<WindowStimulus>(decl_.stimulus);
                if (!ctx.ApplyStimulus(ws))
                    ctx.Skip("window manager refused stimulus '" + decl_.id +
                             "' on a hidden window — cannot exercise honestly");
            }

            if (decl_.knownIssueId) {
                // Known-issue mode: reproduce and REPORT, never gate. (A crash still fails this one
                // ctest case only — gtest_discover_tests runs each case as its own process.)
                const bool progressed = h.RunFrames(30);
                GTEST_SKIP() << "known issue " << decl_.knownIssueId << " — observed: progressed="
                             << progressed << ", validationErrors=" << h.ValidationErrors();
            }

            EXPECT_TRUE(h.RunFrames(30))                                      // inject → observe
                << "rendering did not continue 30 frames post-injection (hang or stop)";
            EXPECT_EQ(h.ValidationErrors(), 0u) << "Vulkan validation errors post-injection"; // criterion 2
            if (decl_.contract) decl_.contract(ctx);                          // criterion 4
        } catch (const SkipScenario& s) {
            GTEST_SKIP() << s.reason;
        }
        // criterion 1 (no crash / nothing escapes host boundary) is implicit: we are still here and
        // Render() returned statuses; a crash fails the whole test process loudly.
    }
private:
    std::string nodeType_;
    ScenarioDecl decl_;
};

void RegisterAllScenarioCases() {
    ReplayScenarioRegistrars();
    ScenarioRegistry::Instance().ForEach([](const std::string& nodeType, const ScenarioDecl& d) {
        ::testing::RegisterTest(
            ("FailScenarioSweep_" + nodeType).c_str(), d.id.c_str(), nullptr, nullptr,
            __FILE__, __LINE__,
            [nodeType, d]() -> ::testing::Test* { return new ScenarioCase(nodeType, d); });
    });
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    RegisterAllScenarioCases();
    return RUN_ALL_TESTS();
}
