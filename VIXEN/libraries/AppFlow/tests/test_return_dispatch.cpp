#include <gtest/gtest.h>
#include "AppFlowRuntime.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

// Inc-4 reframe R2: Return is an ordinary registry handler (design §4.3/D14) — Esc
// (via the seeded return-edge) and a back-button selector both resolve to the SAME
// registered Return handler, which calls NavPop() itself. No special framework case.
TEST(ReturnDispatch, EscAndBackButtonBothPop) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    rt.SetGuardResult(FlowGuardId::DocumentValid, true);

    int returns = 0;
    rt.RegisterHandler(FlowActionId::Return, [&](const AppFlowRuntime::Params&) {
        rt.NavPop();
        ++returns;
    });

    rt.SetCurrent(FlowStateId::Editing);
    ASSERT_EQ(rt.NavTo(FlowStateId::Settings), DispatchResult::Ok);
    EXPECT_EQ(rt.DispatchByKey({KeyId::Escape, KeyMod::None}), DispatchResult::Ok);   // Esc -> Return
    EXPECT_EQ(rt.Current(), FlowStateId::Editing);
    EXPECT_EQ(returns, 1);

    ASSERT_EQ(rt.NavTo(FlowStateId::Settings), DispatchResult::Ok);
    EXPECT_EQ(rt.DispatchBySelector("back-button"), DispatchResult::Ok);              // button -> SAME handler
    EXPECT_EQ(rt.Current(), FlowStateId::Editing);
    EXPECT_EQ(returns, 2);

    EXPECT_EQ(rt.Stack().UndoDepth(), 0u);   // Return is nav, not data: no ActionStack entry
}
