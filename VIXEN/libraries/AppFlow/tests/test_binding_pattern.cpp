#include "BindingStore.h"
#include "generated/AppFlow.g.h"
#include <gtest/gtest.h>
#include <tuple>
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(BindingPattern, ExtractsTypedParamFromSelector) {
    BindingStore s;
    s.RegisterActions(std::span<const AppFlowActionDecl>(kActionDecls, std::size(kActionDecls)));
    AppFlowElementTrigger trig{FlowElementTriggerId::ToggleLayerTrigger,
                               "layer-{index}-toggle", FlowActionId::ToggleLayer,
                               "layerIndex", "click"};
    s.AddElementTrigger(trig);

    BoundAction out;
    ASSERT_TRUE(s.TryGetForSelector("layer-2-toggle", out));
    EXPECT_EQ(out.action, FlowActionId::ToggleLayer);
    ASSERT_EQ(out.params.size(), 1u);
    EXPECT_EQ(out.params[0].first, "layerIndex");
    EXPECT_EQ(out.params[0].second, "2");            // extracted value

    BoundAction miss;
    EXPECT_FALSE(s.TryGetForSelector("not-a-layer", miss));
}

TEST(BindingPattern, GeneratedProgramTriggerDoesNotMatchBroaderReorderPattern) {
    BindingStore store;
    store.RegisterActions(AppFlowContainerView::actions());
    for (const auto& trigger : kElementTriggers) store.AddElementTrigger(trigger);

    for (const auto& [selector, action, cause] : {
        std::tuple{"layer-2-program-up", FlowActionId::EditProgramFieldUp, FlowElementTriggerId::EditProgramFieldUpTrigger},
        std::tuple{"layer-2-program-down", FlowActionId::EditProgramFieldDown, FlowElementTriggerId::EditProgramFieldDownTrigger},
        std::tuple{"layer-2-up", FlowActionId::MoveLayerUp, FlowElementTriggerId::MoveLayerUpTrigger}}) {
        BoundAction resolved;
        ASSERT_TRUE(store.TryGetForSelector(selector, resolved));
        EXPECT_EQ(resolved.action, action);
        EXPECT_EQ(resolved.cause.kind, FlowTriggerKind::ElementTrigger);
        EXPECT_EQ(resolved.cause.identity, cause);
        ASSERT_EQ(resolved.params.size(), 1u);
        EXPECT_EQ(resolved.params[0].second, "2");
    }
}

TEST(BindingPattern, InvalidIntegerExtractionIsInert) {
    BindingStore store;
    store.RegisterActions(AppFlowContainerView::actions());
    for (const auto& trigger : kElementTriggers) store.AddElementTrigger(trigger);
    BoundAction resolved;
    EXPECT_FALSE(store.TryGetForSelector("layer-2-other-up", resolved));
    EXPECT_FALSE(store.TryGetForSelector("layer-999999999999999999999-up", resolved));
    EXPECT_FALSE(store.TryGetForSelector("layer-2.5-up", resolved));
}

// R766: the dock chrome's dock-to buttons carry two placeholders ("panel-{panelId}-to-{region}"); the
// first binds the trigger's ParamName, the second the action parameter it names.
TEST(BindingPattern, TwoPlaceholdersBindBothParams) {
    BindingStore store;
    store.RegisterActions(AppFlowContainerView::actions());
    for (const auto& trigger : kElementTriggers) store.AddElementTrigger(trigger);

    BoundAction move;
    ASSERT_TRUE(store.TryGetForSelector("panel-inspector-to-left", move));
    EXPECT_EQ(move.action, FlowActionId::MovePanel);
    ASSERT_EQ(move.params.size(), 2u);
    EXPECT_EQ(move.params[0], (std::pair<std::string, std::string>{"panelId", "inspector"}));
    EXPECT_EQ(move.params[1], (std::pair<std::string, std::string>{"region", "left"}));

    BoundAction close;
    ASSERT_TRUE(store.TryGetForSelector("panel-inspector-close", close));
    EXPECT_EQ(close.action, FlowActionId::ClosePanel);
    ASSERT_EQ(close.params.size(), 1u);
    EXPECT_EQ(close.params[0].second, "inspector");

    BoundAction tab;
    ASSERT_TRUE(store.TryGetForSelector("panel-layers-tab", tab));
    EXPECT_EQ(tab.action, FlowActionId::FocusPanel);

    BoundAction none;
    EXPECT_FALSE(store.TryGetForSelector("panel--to-left", none));      // empty panel id
    EXPECT_FALSE(store.TryGetForSelector("panel-inspector-to-", none)); // empty region
}

// E3: the layer stepper and selection selectors resolve to their own actions, and the older
// "layer-<i>-up" / "-program-up" patterns keep resolving to theirs (the nudge ids end in -up too).
TEST(BindingPattern, LayerNudgeAndSelectSelectorsBindTheirParamsWithoutStealingOlderIds) {
    BindingStore store;
    store.RegisterActions(AppFlowContainerView::actions());
    for (const auto& trigger : kElementTriggers) store.AddElementTrigger(trigger);

    for (const auto* selector : {"layer-1-nudge-blend-up", "layer-1-nudge-px-down", "layer-12-nudge-sz-up"}) {
        BoundAction nudge;
        ASSERT_TRUE(store.TryGetForSelector(selector, nudge)) << selector;
        EXPECT_EQ(nudge.action, FlowActionId::NudgeLayerField);
        EXPECT_EQ(nudge.cause.identity, FlowElementTriggerId::NudgeLayerFieldTrigger);
        ASSERT_EQ(nudge.params.size(), 2u);
        EXPECT_EQ(nudge.params[0].first, "layerIndex");
        EXPECT_EQ(nudge.params[1].first, "field");
    }
    BoundAction nudge;
    ASSERT_TRUE(store.TryGetForSelector("layer-12-nudge-sz-up", nudge));
    EXPECT_EQ(nudge.params[0].second, "12");
    EXPECT_EQ(nudge.params[1].second, "sz-up");

    BoundAction select;
    ASSERT_TRUE(store.TryGetForSelector("layer-3-select", select));
    EXPECT_EQ(select.action, FlowActionId::SelectLayer);
    ASSERT_EQ(select.params.size(), 1u);
    EXPECT_EQ(select.params[0].second, "3");

    BoundAction reopen;
    ASSERT_TRUE(store.TryGetForSelector("reopen-button", reopen));
    EXPECT_EQ(reopen.action, FlowActionId::ReopenSaved);
    EXPECT_TRUE(reopen.params.empty());

    BoundAction move;
    ASSERT_TRUE(store.TryGetForSelector("layer-3-up", move));
    EXPECT_EQ(move.action, FlowActionId::MoveLayerUp);
    BoundAction program;
    ASSERT_TRUE(store.TryGetForSelector("layer-3-program-up", program));
    EXPECT_EQ(program.action, FlowActionId::EditProgramFieldUp);
}
