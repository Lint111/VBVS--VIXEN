// Built + run by Milestone 3 (Task 7 CMake); this milestone verifies via standalone compile.
#include <gtest/gtest.h>
#include "BindingStore.h"
#include "generated/AppFlow.g.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

static BindingStore makeStore() {
    BindingStore s;
    s.RegisterActions(AppFlowContainerView::actions());
    return s;
}

TEST(BindingStore, ResolvesSelectorToBoundAction) {
    auto s = makeStore();
    std::string warn;
    BindingStore::BindingSpec spec{"#layer-0-toggle", FlowActionId::ToggleLayer, "click",
                                   {{"layerIndex", "dom:attr:data-layer"}}};
    EXPECT_TRUE(s.AddBinding(spec, warn));
    EXPECT_TRUE(warn.empty());
    BoundAction out;
    ASSERT_TRUE(s.TryGetForSelector("#layer-0-toggle", out));
    EXPECT_EQ(out.action, FlowActionId::ToggleLayer);
    EXPECT_EQ(out.on, "click");
    ASSERT_EQ(out.params.size(), 1u);
    EXPECT_EQ(out.params[0].first, "layerIndex");
}

TEST(BindingStore, UnknownParamWarnsAndSkips) {
    auto s = makeStore();
    std::string warn;
    BindingStore::BindingSpec spec{"#x", FlowActionId::ToggleLayer, "click",
                                   {{"notAParam", "dom:attr:foo"}}};
    EXPECT_FALSE(s.AddBinding(spec, warn));
    EXPECT_NE(warn.find("unknown param"), std::string::npos);
    BoundAction out;
    EXPECT_FALSE(s.TryGetForSelector("#x", out));   // inert — never landed
}

TEST(BindingStore, FirstWinKeepsExisting) {
    auto s = makeStore();
    std::string warn;
    BindingStore::BindingSpec a{"#sel", FlowActionId::ToggleLayer, "click", {}};
    BindingStore::BindingSpec b{"#sel", FlowActionId::ToggleLayer, "dblclick", {}};
    EXPECT_TRUE(s.AddBinding(a, warn));
    EXPECT_FALSE(s.AddBinding(b, warn));            // selector taken → first-win, no overwrite
    BoundAction out; ASSERT_TRUE(s.TryGetForSelector("#sel", out));
    EXPECT_EQ(out.on, "click");                     // the first binding survives
}
