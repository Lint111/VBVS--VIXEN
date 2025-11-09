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
#include "../../include/Nodes/WindowNode.h"
#include "../../include/Data/Nodes/WindowNodeConfig.h"

using namespace Vixen::RenderGraph;

// Define globals required by DeviceNode
std::vector<const char*> deviceExtensionNames;
std::vector<const char*> layerNames;

class WindowNodeTest : public ::testing::Test {};

// Configuration Tests
TEST_F(WindowNodeTest, ConfigHasZeroInputs) {
    EXPECT_EQ(WindowNodeConfig::INPUT_COUNT, 0);
}

TEST_F(WindowNodeTest, ConfigHasOneOutput) {
    EXPECT_EQ(WindowNodeConfig::OUTPUT_COUNT, 1) << "WindowNode outputs SURFACE";
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

/**
 * Integration Test Placeholders (Require Window System):
 * - CreateWindow: GLFW/Win32 window creation
 * - CreateSurface: VkSurfaceKHR creation from window
 * - EventHandling: Window events, input processing
 * - ResizeHandling: Window resize, ShouldClose, IsResizing
 * - CleanupWindow: Proper window/surface destruction
 */
