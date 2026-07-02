#include "Core/FailScenario.h"
#include "Core/NodeType.h"
#include "Core/NodeInstance.h"  // FakeNodeType::CreateInstance returns unique_ptr<NodeInstance> by value;
                                 // the type must be complete here for the destructor to instantiate.
#include "Nodes/WindowNode.h"
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

TEST(FaultInjector, ArmOnceFiresExactlyOncePerSite) {
    FaultInjector fi;
    EXPECT_EQ(fi.Filter(FaultSite::Acquire, VK_SUCCESS), VK_SUCCESS);       // unarmed: passthrough
    fi.ArmOnce(FaultSite::Acquire, VK_ERROR_OUT_OF_DATE_KHR);
    EXPECT_TRUE(fi.IsArmed(FaultSite::Acquire));
    EXPECT_FALSE(fi.IsArmed(FaultSite::Present));                           // per-site isolation
    EXPECT_EQ(fi.Filter(FaultSite::Present, VK_SUCCESS), VK_SUCCESS);       // other site unaffected
    EXPECT_EQ(fi.Filter(FaultSite::Acquire, VK_SUCCESS), VK_ERROR_OUT_OF_DATE_KHR);
    EXPECT_EQ(fi.Filter(FaultSite::Acquire, VK_SUCCESS), VK_SUCCESS);       // once only
}

TEST(WindowSeam, InjectQueuesEventsThreadSafely) {
    Vixen::RenderGraph::WindowNodeType type;
    auto node = type.CreateInstance("test_window");
    auto* wn = static_cast<Vixen::RenderGraph::WindowNode*>(node.get());
    using WE = Vixen::RenderGraph::WindowNode::WindowEvent;
    wn->InjectWindowEvent(WE::Type::Resize, 1920, 1080);
    wn->InjectWindowEvent(WE::Type::Maximize);
    EXPECT_EQ(wn->PendingEventCountForTest(), 2u);
}
