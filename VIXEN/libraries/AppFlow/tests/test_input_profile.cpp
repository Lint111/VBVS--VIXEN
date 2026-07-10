#include "InputProfile.h"
#include <gtest/gtest.h>
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(InputProfile, TightestScopeWins) {
    InputProfile p;
    p.Bind(FlowScope::Global, FlowStateId{}, {KeyId::Z, KeyMod::Ctrl}, FlowActionId::Undo);
    p.Bind(FlowScope::State, FlowStateId::Settings, {KeyId::Z, KeyMod::Ctrl}, FlowActionId::UndoSettingChange);

    FlowActionId out{};
    ASSERT_TRUE(p.Resolve({KeyId::Z, KeyMod::Ctrl}, FlowStateId::Editing, out));
    EXPECT_EQ(out, FlowActionId::Undo);                 // no Settings override in Editing -> global
    ASSERT_TRUE(p.Resolve({KeyId::Z, KeyMod::Ctrl}, FlowStateId::Settings, out));
    EXPECT_EQ(out, FlowActionId::UndoSettingChange);    // tighter State scope wins
}

TEST(InputProfile, UnboundChordReturnsFalse) {
    InputProfile p;
    FlowActionId out{};
    EXPECT_FALSE(p.Resolve({KeyId::A, KeyMod::None}, FlowStateId::Editing, out));
}
