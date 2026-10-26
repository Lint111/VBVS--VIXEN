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
