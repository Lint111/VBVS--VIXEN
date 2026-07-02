// Dynamic registration: one test per declared scenario (gtest RegisterTest).
#include "ScenarioHarness.h"
#include <gtest/gtest.h>

TEST(FailScenarioSweep, BootWarmupTeardown) {
    if (!AppHarness::DisplayAvailable()) GTEST_SKIP() << "no DISPLAY/WAYLAND_DISPLAY (GLFW needs a display server)";
    AppHarness h;
    ASSERT_TRUE(h.Boot()) << h.BootFailureReason();
    EXPECT_TRUE(h.RunFrames(30));                      // warmup: 30 frames, watchdog-bounded
    EXPECT_EQ(h.ValidationErrors(), 0u);
}   // ~AppHarness tears down cleanly (DeInitialize)

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
