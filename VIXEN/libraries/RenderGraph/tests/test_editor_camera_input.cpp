#include "CameraInputTranslation.h"
#include <gtest/gtest.h>

// R765/E12: raw mouse/axis input becomes AppFlow camera dispatches (the editor's host translation). Synthetic
// InputState, no window or device.
using namespace Vixen::Editor;
using Vixen::AppFlow::Generated::FlowActionId;
using Vixen::RenderGraph::InputAxis;
using Vixen::RenderGraph::InputState;

namespace {
constexpr float kSens = 0.0015f;
constexpr float kMove = 20.0f;
void SetAxis(InputState& s, InputAxis a, float v) { s.axes[static_cast<std::size_t>(a)] = v; }
}  // namespace

TEST(EditorCameraInput, RightMouseDragOrbitsInRadians) {
    InputState s;
    s.mouseButtons[1] = true;
    s.mouseDelta = {40.0f, 10.0f};
    const auto d = CameraDispatchesFromInput(s, false, kSens, kMove);
    ASSERT_EQ(d.size(), 1u);
    EXPECT_EQ(d[0].action, FlowActionId::OrbitCamera);
    EXPECT_FLOAT_EQ(d[0].a, 40.0f * kSens);
    EXPECT_FLOAT_EQ(d[0].b, -10.0f * kSens);  // screen-down is pitch-down
}

TEST(EditorCameraInput, MouseMotionWithoutTheRightButtonDoesNothing) {
    InputState s;
    s.mouseButtons[0] = true;  // left drag is selection/scrub, never an orbit
    s.mouseDelta = {40.0f, 10.0f};
    EXPECT_TRUE(CameraDispatchesFromInput(s, false, kSens, kMove).empty());
}

TEST(EditorCameraInput, WheelZoomsByNotchesTimesSpeedAndRespectsTheWheelSwitch) {
    InputState s;
    s.wheelDelta = {0.0f, 3.0f};
    s.wheelZoomSpeed = 2.0f;
    auto d = CameraDispatchesFromInput(s, false, kSens, kMove);
    ASSERT_EQ(d.size(), 1u);
    EXPECT_EQ(d[0].action, FlowActionId::ZoomCamera);
    EXPECT_FLOAT_EQ(d[0].a, 6.0f);
    s.wheelZoom = false;
    EXPECT_TRUE(CameraDispatchesFromInput(s, false, kSens, kMove).empty());
}

TEST(EditorCameraInput, HeldKeyAxesPanZoomAndLookScaledByClampedFrameTime) {
    InputState s;
    s.deltaTime = 1.0f;  // a stall: clamped to 0.1 s like CameraNode always did
    SetAxis(s, InputAxis::MoveForward, -1.0f);   // S
    SetAxis(s, InputAxis::MoveHorizontal, 1.0f); // D
    SetAxis(s, InputAxis::MoveUp, -1.0f);        // Q
    SetAxis(s, InputAxis::LookHorizontal, 1.0f); // Right arrow
    const auto d = CameraDispatchesFromInput(s, false, kSens, kMove);
    ASSERT_EQ(d.size(), 3u);
    EXPECT_EQ(d[0].action, FlowActionId::ZoomCamera);
    EXPECT_FLOAT_EQ(d[0].a, -kCameraKeyZoomSpeed * 0.1f);  // plain S zooms out; it no longer saves
    EXPECT_EQ(d[1].action, FlowActionId::PanCamera);
    EXPECT_FLOAT_EQ(d[1].a, kMove * 0.1f);
    EXPECT_FLOAT_EQ(d[1].b, -kMove * 0.1f);
    EXPECT_EQ(d[2].action, FlowActionId::OrbitCamera);
    EXPECT_FLOAT_EQ(d[2].a, kCameraArrowLookSpeed * 0.1f * kSens);
    EXPECT_FLOAT_EQ(d[2].b, 0.0f);
}

TEST(EditorCameraInput, CtrlSuppressesTheHeldKeyAxesButNotTheMouse) {
    InputState s;
    s.deltaTime = 0.016f;
    SetAxis(s, InputAxis::MoveForward, -1.0f);  // Ctrl+S: S's zoom axis must not fire
    s.mouseButtons[1] = true;
    s.mouseDelta = {1.0f, 0.0f};
    const auto d = CameraDispatchesFromInput(s, true, kSens, kMove);
    ASSERT_EQ(d.size(), 1u);
    EXPECT_EQ(d[0].action, FlowActionId::OrbitCamera);
}

TEST(EditorCameraInput, IdleInputDispatchesNothing) {
    InputState s;
    s.deltaTime = 0.016f;
    EXPECT_TRUE(CameraDispatchesFromInput(s, false, kSens, kMove).empty());
}
