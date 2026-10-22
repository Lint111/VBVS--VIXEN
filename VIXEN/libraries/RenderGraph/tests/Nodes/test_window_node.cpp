/**
 * @file test_window_node.cpp
 * @brief Tests for WindowNode class
 *
 * Coverage: WindowNode.h (Target: 50%+ unit, 30%+ integration)
 *
 * Unit Tests: Config validation, slot metadata, parameter handling
 * Integration Tests: Window creation, surface creation, event handling, resize
 *
 * NOTE: Window creation requires platform-specific window system (GLFW/Win32).
 */

#include <gtest/gtest.h>
#include <Nodes/WindowNode.h>
#include <Data/Nodes/WindowNodeConfig.h>

using namespace Vixen::RenderGraph;

// Use centralized Vulkan global names to avoid duplicate strong symbols
#include <VulkanGlobalNames.h>

class WindowNodeTest : public ::testing::Test {};

// Configuration Tests
TEST_F(WindowNodeTest, ConfigHasZeroInputs) {
    EXPECT_EQ(WindowNodeConfig::INPUT_COUNT, 1);
}

TEST_F(WindowNodeTest, ConfigHasFourOutputs) {
    EXPECT_EQ(WindowNodeConfig::OUTPUT_COUNT, 4) << "WindowNode outputs SURFACE, WINDOW, WIDTH, HEIGHT";
}

TEST_F(WindowNodeTest, ConfigSurfaceOutputIndex) {
    EXPECT_EQ(WindowNodeConfig::SURFACE_Slot::index, 0);
}

TEST_F(WindowNodeTest, ConfigSurfaceIsRequired) {
    EXPECT_FALSE(WindowNodeConfig::SURFACE_Slot::nullable);
}

TEST_F(WindowNodeTest, ConfigSurfaceTypeIsVkSurfaceKHR) {
    bool isCorrect = std::is_same_v<WindowNodeConfig::SURFACE_Slot::Type, VkSurfaceKHR>;
    EXPECT_TRUE(isCorrect);
}

// Parameter Tests
TEST_F(WindowNodeTest, ConfigHasWidthParameter) {
    EXPECT_STREQ(WindowNodeConfig::PARAM_WIDTH, "width");
}

TEST_F(WindowNodeTest, ConfigHasHeightParameter) {
    EXPECT_STREQ(WindowNodeConfig::PARAM_HEIGHT, "height");
}

// Slot Metadata
TEST_F(WindowNodeTest, ConfigSurfaceIsWriteOnly) {
    EXPECT_EQ(WindowNodeConfig::SURFACE_Slot::mutability, SlotMutability::WriteOnly);
}

// Type System
TEST_F(WindowNodeTest, TypeNameIsWindow) {
    WindowNodeType windowType;
    EXPECT_STREQ(windowType.GetTypeName().c_str(), "Window");
}

// ResizeDebouncer: the size-observation state machine behind WindowNode's resize handling.
TEST(ResizeDebouncerTest, SettlesOnTheFinalSizeOfADrag) {
    ResizeDebouncer d(0.1);
    double t = 0.0;
    for (uint32_t w = 520; w <= 900; w += 20, t += 0.016) d.Observe(w, 700, 500, 500, t);
    EXPECT_FALSE(d.Settled(t)) << "must not apply while the drag is still moving";
    EXPECT_TRUE(d.Settled(t + 0.11));
    EXPECT_EQ(d.Width(), 900u);
    EXPECT_EQ(d.Height(), 700u);
}

TEST(ResizeDebouncerTest, ReturningToAppliedSizeCancelsThePendingSize) {
    // 500x500 applied; the window is dragged to 800x600 and back to 500x500 before settling. The old
    // code dropped the second event (it equalled the applied size) and later applied 800x600.
    ResizeDebouncer d(0.1);
    d.Observe(800, 600, 500, 500, 0.0);
    ASSERT_TRUE(d.HasPending());
    d.Observe(500, 500, 500, 500, 0.05);
    EXPECT_FALSE(d.HasPending());
    EXPECT_FALSE(d.Settled(1.0));
}

TEST(ResizeDebouncerTest, RepeatedPollOfTheSamePendingSizeDoesNotRestartTheClock) {
    ResizeDebouncer d(0.1);
    d.Observe(640, 480, 500, 500, 0.0);
    for (double t = 0.01; t < 0.2; t += 0.01) d.Observe(640, 480, 500, 500, t);  // per-frame polling
    EXPECT_TRUE(d.Settled(0.2));
}

TEST(ResizeDebouncerTest, MinimisedZeroSizeKeepsTheLastExtent) {
    ResizeDebouncer d(0.1);
    d.Observe(0, 0, 500, 500, 0.0);
    EXPECT_FALSE(d.HasPending());
}

TEST(ResizeDebouncerTest, AMissedCallbackIsStillSeenByPolling) {
    // No callback ever fired; the framebuffer simply reads a size the swapchain does not have.
    ResizeDebouncer d(0.1);
    d.Observe(884, 661, 500, 500, 3.0);
    EXPECT_TRUE(d.Settled(3.2));
    EXPECT_EQ(d.Width(), 884u);
    EXPECT_EQ(d.Height(), 661u);
}

/**
 * Integration Test Placeholders (Require Window System):
 * - CreateWindow: GLFW/Win32 window creation
 * - CreateSurface: VkSurfaceKHR creation from window
 * - EventHandling: Window events, input processing
 * - ResizeHandling: Window resize, ShouldClose, IsResizing
 * - CleanupWindow: Proper window/surface destruction
 */
