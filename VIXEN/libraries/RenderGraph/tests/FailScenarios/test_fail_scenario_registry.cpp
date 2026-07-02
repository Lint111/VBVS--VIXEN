#include "Core/FailScenario.h"
#include "Core/NodeType.h"
#include "Core/NodeInstance.h"  // FakeNodeType::CreateInstance returns unique_ptr<NodeInstance> by value;
                                 // the type must be complete here for the destructor to instantiate.
#include <gtest/gtest.h>

using namespace Vixen::RenderGraph;
using namespace Vixen::RenderGraph::FailScenario;

// A minimal NodeType so the macro's NodeTypeClass().GetTypeName() works without Vulkan.
namespace { struct FakeNodeType : NodeType { FakeNodeType() : NodeType("FakeTestNode") {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string&) const override { return nullptr; } }; }

VIXEN_FAIL_SCENARIOS_DECLARE(FakeNodeType,
    VIXEN_SCENARIO(FakeAcquireFault,
        VkTransient{ .site = FaultSite::Acquire, .result = VK_ERROR_OUT_OF_DATE_KHR },
        [](ScenarioContext&) {}),
    VIXEN_SCENARIO(FakeResize,
        WindowStimulus{ .kind = WindowStimulus::Kind::ResizeTo, .width = 1920, .height = 1080 },
        [](ScenarioContext&) {})
);

TEST(FailScenarioRegistry, MacroRegistersTypedScenariosUnderNodeTypeName) {
    ReplayScenarioRegistrars();
    const auto* decls = ScenarioRegistry::Instance().Find("FakeTestNode");
    ASSERT_NE(decls, nullptr);
    ASSERT_EQ(decls->size(), 2u);
    EXPECT_EQ((*decls)[0].id, "FakeAcquireFault");
    const auto& vt = std::get<VkTransient>((*decls)[0].stimulus);
    EXPECT_EQ(vt.site, FaultSite::Acquire);
    EXPECT_EQ(vt.result, VK_ERROR_OUT_OF_DATE_KHR);
    const auto& ws = std::get<WindowStimulus>((*decls)[1].stimulus);
    EXPECT_EQ(ws.width, 1920u);
    EXPECT_EQ((*decls)[1].knownIssueId, nullptr);
}

TEST(FailScenarioRegistry, ReplayIsIdempotent) {
    ReplayScenarioRegistrars();
    ReplayScenarioRegistrars();
    EXPECT_EQ(ScenarioRegistry::Instance().Find("FakeTestNode")->size(), 2u);
}

TEST(FailScenarioRegistry, FindUnknownTypeReturnsNull) {
    ReplayScenarioRegistrars();
    EXPECT_EQ(ScenarioRegistry::Instance().Find("NoSuchNode"), nullptr);
}
