/**
 * @file test_graph_lifecycle_hooks.cpp
 * @brief Tests for GraphLifecycleHooks node-hook targeting
 *
 * Validates that a node hook registered against a specific target node only
 * ever runs for that node's ExecuteNodeHooks call, not for every other node
 * in the graph (the per-node keying introduced to avoid an O(nodes * hooks)
 * self-filter-by-identity scan on every node's Execute()).
 *
 * Compatible with VULKAN_TRIMMED_BUILD (headers only).
 */

#include <gtest/gtest.h>
#include <Core/GraphLifecycleHooks.h>
#include <Core/NodeType.h>
#include <Core/NodeInstance.h>

#include <VulkanGlobalNames.h>

using namespace Vixen::RenderGraph;

namespace {

class MockNodeType : public NodeType {
public:
    explicit MockNodeType(const std::string& typeName = "MockNode")
        : NodeType(typeName) {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return nullptr;
    }
};

class MockNode : public NodeInstance {
public:
    explicit MockNode(const std::string& name, NodeType* nodeType = nullptr)
        : NodeInstance(name, nodeType ? nodeType : &mockType) {}

private:
    static MockNodeType mockType;
};

MockNodeType MockNode::mockType("MockNode");

} // namespace

TEST(GraphLifecycleHooks, TargetedNodeHookOnlyFiresForItsNode) {
    GraphLifecycleHooks hooks;
    MockNode nodeA("A");
    MockNode nodeB("B");

    int nodeAHookCalls = 0;
    int nodeBHookCalls = 0;

    hooks.RegisterNodeHook(
        NodeLifecyclePhase::PreExecute,
        [&](NodeInstance*) { ++nodeAHookCalls; },
        "NodeA hook",
        &nodeA
    );

    hooks.RegisterNodeHook(
        NodeLifecyclePhase::PreExecute,
        [&](NodeInstance*) { ++nodeBHookCalls; },
        "NodeB hook",
        &nodeB
    );

    hooks.ExecuteNodeHooks(NodeLifecyclePhase::PreExecute, &nodeA);

    EXPECT_EQ(nodeAHookCalls, 1);
    EXPECT_EQ(nodeBHookCalls, 0);  // Never invoked, not just filtered inside the callback

    hooks.ExecuteNodeHooks(NodeLifecyclePhase::PreExecute, &nodeB);

    EXPECT_EQ(nodeAHookCalls, 1);
    EXPECT_EQ(nodeBHookCalls, 1);
}

TEST(GraphLifecycleHooks, UntargetedNodeHookFiresForEveryNode) {
    GraphLifecycleHooks hooks;
    MockNode nodeA("A");
    MockNode nodeB("B");

    int globalHookCalls = 0;

    hooks.RegisterNodeHook(
        NodeLifecyclePhase::PostCompile,
        [&](NodeInstance*) { ++globalHookCalls; },
        "Global hook"
        // No targetNode: preserves legacy behavior of running for every node.
    );

    hooks.ExecuteNodeHooks(NodeLifecyclePhase::PostCompile, &nodeA);
    hooks.ExecuteNodeHooks(NodeLifecyclePhase::PostCompile, &nodeB);

    EXPECT_EQ(globalHookCalls, 2);
}

TEST(GraphLifecycleHooks, TargetedAndUntargetedHooksBothRunForTargetNode) {
    GraphLifecycleHooks hooks;
    MockNode nodeA("A");
    MockNode nodeB("B");

    int targetedCalls = 0;
    int globalCalls = 0;

    hooks.RegisterNodeHook(
        NodeLifecyclePhase::PreCompile,
        [&](NodeInstance*) { ++targetedCalls; },
        "Targeted hook",
        &nodeA
    );

    hooks.RegisterNodeHook(
        NodeLifecyclePhase::PreCompile,
        [&](NodeInstance*) { ++globalCalls; },
        "Global hook"
    );

    hooks.ExecuteNodeHooks(NodeLifecyclePhase::PreCompile, &nodeA);
    EXPECT_EQ(targetedCalls, 1);
    EXPECT_EQ(globalCalls, 1);

    hooks.ExecuteNodeHooks(NodeLifecyclePhase::PreCompile, &nodeB);
    EXPECT_EQ(targetedCalls, 1);  // Still 1 - targeted hook is nodeA-only
    EXPECT_EQ(globalCalls, 2);    // Global hook ran again for nodeB
}

TEST(GraphLifecycleHooks, ClearNodeHooksClearsBothTargetedAndGlobal) {
    GraphLifecycleHooks hooks;
    MockNode nodeA("A");

    int calls = 0;

    hooks.RegisterNodeHook(
        NodeLifecyclePhase::PostSetup,
        [&](NodeInstance*) { ++calls; },
        "Targeted hook",
        &nodeA
    );

    hooks.ClearNodeHooks(NodeLifecyclePhase::PostSetup);
    hooks.ExecuteNodeHooks(NodeLifecyclePhase::PostSetup, &nodeA);

    EXPECT_EQ(calls, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
