#include "ScenarioHarness.h"
#include <gtest/gtest.h>

TEST(FailScenarioSweep, BootWarmupTeardown) {
    if (!AppHarness::DisplayAvailable()) GTEST_SKIP() << "no DISPLAY/WAYLAND_DISPLAY (GLFW needs a display server)";
    AppHarness h;
    ASSERT_TRUE(h.Boot()) << h.BootFailureReason();
    EXPECT_TRUE(h.RunFrames(30));                      // warmup: 30 frames, watchdog-bounded
    EXPECT_EQ(h.ValidationErrors(), 0u);
}   // ~AppHarness tears down cleanly (DeInitialize)

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
