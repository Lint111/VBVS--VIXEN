#include "BindingStore.h"
#include "generated/AppFlow.g.h"
#include <gtest/gtest.h>
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(BindingPattern, ExtractsTypedParamFromSelector) {
    BindingStore s;
    s.RegisterActions(std::span<const AppFlowActionDecl>(kActionDecls, std::size(kActionDecls)));
    AppFlowElementTrigger trig{"layer-{index}-toggle", FlowActionId::ToggleLayer, "layerIndex", "click"};
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
