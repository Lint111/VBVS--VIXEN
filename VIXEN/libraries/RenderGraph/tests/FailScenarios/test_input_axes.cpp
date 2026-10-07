#include <array>
#include <initializer_list>

#include <gtest/gtest.h>

#include "Nodes/InputNode.h"

using namespace Vixen::RenderGraph;
namespace EventBus = Vixen::EventBus;

namespace {

class InputNodeHarness {
public:
    InputNodeHarness() : node_("input_axes_test", &type_) {}

    InputNode& Node() { return node_; }

    const InputState& AdvanceFrame() {
        node_.ProcessPendingInput();
        node_.Execute();
        return node_.GetInputState();
    }

private:
    InputNodeType type_;
    InputNode node_;
};

constexpr std::array<InputAxis, 5> kAxes{
    InputAxis::MoveHorizontal,
    InputAxis::MoveForward,
    InputAxis::MoveUp,
    InputAxis::LookHorizontal,
    InputAxis::LookVertical,
};

void ExpectAxesBounded(const InputState& state) {
    for (InputAxis axis : kAxes) {
        EXPECT_GE(state.GetAxis(axis), -1.0f);
        EXPECT_LE(state.GetAxis(axis), 1.0f);
    }
}

void ExpectVec2(const glm::vec2& actual, float x, float y) {
    EXPECT_FLOAT_EQ(actual.x, x);
    EXPECT_FLOAT_EQ(actual.y, y);
}

}  // namespace

TEST(InputNodeAxes, OppositeKeysCancelAndValuesStayBounded) {
    InputNodeHarness input;
    auto& node = input.Node();

    for (EventBus::KeyCode key : {
             EventBus::KeyCode::A, EventBus::KeyCode::D,
             EventBus::KeyCode::S, EventBus::KeyCode::W,
             EventBus::KeyCode::Q, EventBus::KeyCode::E,
             EventBus::KeyCode::Left, EventBus::KeyCode::Right,
             EventBus::KeyCode::Down, EventBus::KeyCode::Up}) {
        node.InjectKey(key, true);
    }

    const InputState& cancelled = input.AdvanceFrame();
    for (InputAxis axis : kAxes) {
        EXPECT_FLOAT_EQ(cancelled.GetAxis(axis), 0.0f);
    }
    ExpectAxesBounded(cancelled);

    for (EventBus::KeyCode key : {
             EventBus::KeyCode::A, EventBus::KeyCode::S,
             EventBus::KeyCode::Q, EventBus::KeyCode::Left,
             EventBus::KeyCode::Down}) {
        node.InjectKey(key, false);
    }

    const InputState& positive = input.AdvanceFrame();
    for (InputAxis axis : kAxes) {
        EXPECT_FLOAT_EQ(positive.GetAxis(axis), 1.0f);
    }
    ExpectAxesBounded(positive);

    for (EventBus::KeyCode key : {
             EventBus::KeyCode::D, EventBus::KeyCode::W,
             EventBus::KeyCode::E, EventBus::KeyCode::Right,
             EventBus::KeyCode::Up}) {
        node.InjectKey(key, false);
    }
    for (EventBus::KeyCode key : {
             EventBus::KeyCode::A, EventBus::KeyCode::S,
             EventBus::KeyCode::Q, EventBus::KeyCode::Left,
             EventBus::KeyCode::Down}) {
        node.InjectKey(key, true);
    }

    const InputState& negative = input.AdvanceFrame();
    for (InputAxis axis : kAxes) {
        EXPECT_FLOAT_EQ(negative.GetAxis(axis), -1.0f);
    }
    ExpectAxesBounded(negative);
}

TEST(InputNodeAxes, ReleasingKeyClearsItsAxisOnNextFrame) {
    InputNodeHarness input;
    input.Node().InjectKey(EventBus::KeyCode::W, true);
    EXPECT_FLOAT_EQ(input.AdvanceFrame().GetAxisVertical(), 1.0f);

    input.Node().InjectKey(EventBus::KeyCode::W, false);
    const InputState& released = input.AdvanceFrame();
    EXPECT_FLOAT_EQ(released.GetAxisVertical(), 0.0f);
    EXPECT_TRUE(released.IsKeyReleased(EventBus::KeyCode::W));
}

TEST(InputNodeAxes, MouseDeltaIsRawAndResetsAtFrameBoundary) {
    InputNodeHarness input;
    input.Node().InjectCursorPos(100.0, 100.0);
    ExpectVec2(input.AdvanceFrame().mouseDelta, 0.0f, 0.0f);

    input.Node().InjectCursorPos(105.0, 98.0);
    ExpectVec2(input.AdvanceFrame().mouseDelta, 5.0f, -2.0f);

    ExpectVec2(input.AdvanceFrame().mouseDelta, 0.0f, 0.0f);
}

TEST(InputNodeAxes, FocusLossClearsAxesAndQueuedMouseMotion) {
    InputNodeHarness input;
    auto& node = input.Node();

    node.InjectCursorPos(10.0, 10.0);
    input.AdvanceFrame();
    node.InjectCursorPos(15.0, 8.0);
    for (EventBus::KeyCode key : {
             EventBus::KeyCode::D, EventBus::KeyCode::W,
             EventBus::KeyCode::E, EventBus::KeyCode::Right,
             EventBus::KeyCode::Up}) {
        node.InjectKey(key, true);
    }

    const InputState& active = input.AdvanceFrame();
    for (InputAxis axis : kAxes) {
        EXPECT_FLOAT_EQ(active.GetAxis(axis), 1.0f);
    }
    ExpectVec2(active.mouseDelta, 5.0f, -2.0f);

    node.InjectKey(EventBus::KeyCode::A, true);
    node.InjectCursorPos(500.0, 500.0);
    node.InjectFocusLoss();
    const InputState& cleared = input.AdvanceFrame();

    for (InputAxis axis : kAxes) {
        EXPECT_FLOAT_EQ(cleared.GetAxis(axis), 0.0f);
    }
    ExpectVec2(cleared.mouseDelta, 0.0f, 0.0f);
    EXPECT_FALSE(cleared.IsKeyDown(EventBus::KeyCode::W));
    ExpectAxesBounded(cleared);
}
