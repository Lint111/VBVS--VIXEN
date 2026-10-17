#include <gtest/gtest.h>
#include "Data/ReconstructionHistory.h"

using namespace Vixen::RenderGraph;

TEST(ReconstructionHistory, AllocationAndFailedWritesNeverCreateValidHistory) {
    ReconstructionHistoryState state;
    for (int i = 0; i < 3; ++i) {
        const auto frame = state.BeginFrame(glm::mat4(1.0f), {250, 250}, {500, 500});
        EXPECT_EQ(frame.valid, 0u);
        EXPECT_EQ(frame.reset, 1u);
        EXPECT_EQ(frame.writeIndex, 0u);
    }
    state.CommitHistoryFrame();
    state.CommitHistoryFrame(); // A duplicate acknowledgement must not advance twice.
    const auto next = state.BeginFrame(glm::mat4(1.0f), {250, 250}, {500, 500});
    EXPECT_EQ(next.valid, 1u);
    EXPECT_EQ(next.reset, 0u);
    EXPECT_EQ(next.frameIndex, 1u);
    EXPECT_EQ(next.writeIndex, 1u);
}

TEST(ReconstructionHistory, ExplicitCutsAndViewportSwitchesResetOnlyTheirOwner) {
    ReconstructionHistoryState first, second;
    for (auto* state : {&first, &second}) {
        state->BeginFrame(glm::mat4(1.0f), {250, 250}, {500, 500});
        state->CommitHistoryFrame();
    }
    first.Invalidate(ReconstructionHistoryReset::CameraCut);
    auto frame = first.BeginFrame(glm::mat4(1.0f), {250, 250}, {500, 500});
    EXPECT_EQ(frame.reset, 1u);
    EXPECT_EQ(frame.frameIndex, 0u);
    EXPECT_EQ(frame.resetReasons, static_cast<uint32_t>(ReconstructionHistoryReset::CameraCut));
    EXPECT_EQ(second.BeginFrame(glm::mat4(1.0f), {250, 250}, {500, 500}).valid, 1u);
    first.CommitHistoryFrame();
    first.Invalidate(ReconstructionHistoryReset::ViewportSwitch);
    frame = first.BeginFrame(glm::mat4(1.0f), {250, 250}, {500, 500});
    EXPECT_EQ(frame.valid, 0u);
    EXPECT_EQ(frame.resetReasons, static_cast<uint32_t>(ReconstructionHistoryReset::ViewportSwitch));
}

TEST(ReconstructionHistory, ProjectionAndEitherExtentChangeInvalidateHistory) {
    ReconstructionHistoryState state;
    glm::mat4 projection(1.0f);
    state.BeginFrame(projection, {250, 250}, {500, 500});
    state.CommitHistoryFrame();
    // View/camera motion is absent from this key: it must not erase temporal history.
    EXPECT_EQ(state.BeginFrame(projection, {250, 250}, {500, 500}).reset, 0u);
    state.CommitHistoryFrame();
    projection[0][0] = 2.0f;
    EXPECT_EQ(state.BeginFrame(projection, {250, 250}, {500, 500}).resetReasons,
              static_cast<uint32_t>(ReconstructionHistoryReset::ProjectionChanged));
    state.CommitHistoryFrame();
    EXPECT_EQ(state.BeginFrame(projection, {251, 250}, {500, 500}).valid, 0u);
    state.CommitHistoryFrame();
    EXPECT_EQ(state.BeginFrame(projection, {251, 250}, {501, 500}).valid, 0u);
}
