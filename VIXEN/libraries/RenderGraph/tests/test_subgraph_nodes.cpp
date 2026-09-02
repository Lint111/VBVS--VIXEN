#include <gtest/gtest.h>

#include "Core/RenderGraph.h"
#include "Core/NodeTypeRegistry.h"
#include "Data/Core/ResourceConfig.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;

namespace {

// The test graph uses an opaque buffer value so the test exercises the real slot
// type machinery without constructing Vulkan resources or devices.
CONSTEXPR_NODE_CONFIG(SourceSlots, 0, 1, SlotArrayMode::Single) {
    OUTPUT_SLOT(OUT, const VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);
};

CONSTEXPR_NODE_CONFIG(TransformSlots, 1, 1, SlotArrayMode::Single) {
    INPUT_SLOT(IN, const VkBuffer, 0, SlotNullability::Required,
               SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    OUTPUT_SLOT(OUT, const VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);
};

CONSTEXPR_NODE_CONFIG(SinkSlots, 1, 0, SlotArrayMode::Single) {
    INPUT_SLOT(IN, const VkBuffer, 0, SlotNullability::Required,
               SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);
};

CONSTEXPR_NODE_CONFIG(ChainPorts, 1, 1, SlotArrayMode::Single) {
    INPUT_SLOT(IN, const VkBuffer, 0, SlotNullability::Required,
               SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    OUTPUT_SLOT(OUT, const VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);
};

CONSTEXPR_NODE_CONFIG(EmptyPorts, 0, 0, SlotArrayMode::Single) {};

class TestNode final : public NodeInstance {
public:
    TestNode(const std::string& name, NodeType* type)
        : NodeInstance(name, type) {}
};

class TestNodeType final : public NodeType {
public:
    TestNodeType(std::string name, size_t inputCount, size_t outputCount)
        : NodeType(std::move(name)) {
        inputSchema.resize(inputCount);
        outputSchema.resize(outputCount);
        for (size_t index = 0; index < inputCount; ++index) {
            inputSchema[index].name = "input" + std::to_string(index);
            inputSchema[index].type = ResourceType::Buffer;
            inputSchema[index].nullable = false;
        }
        for (size_t index = 0; index < outputCount; ++index) {
            outputSchema[index].name = "output" + std::to_string(index);
            outputSchema[index].type = ResourceType::Buffer;
            outputSchema[index].nullable = false;
        }
    }

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<TestNode>(name, const_cast<TestNodeType*>(this));
    }
};

struct ChainParams {};

class ChainSubGraph final : public SubGraphType<ChainSubGraph> {
public:
    using PortConfig = ChainPorts;
    using Params = ChainParams;

    void Build(GraphScope& scope, const Params&) {
        const NodeHandle transform = scope.AddNode("Transform", "Transform");
        scope.BindInput(ChainPorts::IN, transform, TransformSlots::IN);
        scope.BindOutput(ChainPorts::OUT, transform, TransformSlots::OUT);
    }
};

class UnboundSubGraph final : public SubGraphType<UnboundSubGraph> {
public:
    using PortConfig = ChainPorts;
    using Params = ChainParams;

    void Build(GraphScope& scope, const Params&) {
        const NodeHandle transform = scope.AddNode("Transform", "Transform");
        scope.BindOutput(ChainPorts::OUT, transform, TransformSlots::OUT);
    }
};

class DoubleOutputSubGraph final : public SubGraphType<DoubleOutputSubGraph> {
public:
    using PortConfig = ChainPorts;
    using Params = ChainParams;

    void Build(GraphScope& scope, const Params&) {
        const NodeHandle transform = scope.AddNode("Transform", "Transform");
        scope.BindInput(ChainPorts::IN, transform, TransformSlots::IN);
        scope.BindOutput(ChainPorts::OUT, transform, TransformSlots::OUT);
        scope.BindOutput(ChainPorts::OUT, transform, TransformSlots::OUT);
    }
};

class RecursiveSubGraph final : public SubGraphType<RecursiveSubGraph> {
public:
    using PortConfig = EmptyPorts;
    using Params = ChainParams;

    void Build(GraphScope& scope, const Params& params) {
        scope.Instantiate<RecursiveSubGraph>("recursive", params);
    }
};

template<int Depth>
class DepthSubGraph final : public SubGraphType<DepthSubGraph<Depth>> {
public:
    using PortConfig = EmptyPorts;
    using Params = ChainParams;

    void Build(GraphScope& scope, const Params& params) {
        if constexpr (Depth > 0) {
            scope.Instantiate<DepthSubGraph<Depth - 1>>("nested", params);
        }
    }
};

class GraphFixture {
public:
    GraphFixture()
        : graph(&registry) {
        registry.RegisterNodeType(std::make_unique<TestNodeType>("Source", 0, 1));
        registry.RegisterNodeType(std::make_unique<TestNodeType>("Transform", 1, 1));
        registry.RegisterNodeType(std::make_unique<TestNodeType>("Sink", 1, 0));
    }

    NodeHandle AddSource(const std::string& name) { return graph.AddNode("Source", name); }
    NodeHandle AddTransform(const std::string& name) { return graph.AddNode("Transform", name); }
    NodeHandle AddSink(const std::string& name) { return graph.AddNode("Sink", name); }

    NodeTypeRegistry registry;
    RenderGraph graph;
};

std::vector<std::string> Names(const GraphTopology& topology) {
    std::vector<std::string> names;
    for (NodeInstance* node : topology.GetNodes()) {
        names.push_back(node->GetInstanceName());
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::vector<std::string>> WaveNames(const GraphTaskPlan& plan) {
    std::vector<std::vector<std::string>> waves;
    for (const auto& wave : plan.waves) {
        std::vector<std::string> names;
        for (const auto& taskId : wave) {
            const NodeInstance* node = plan.FindNode(taskId);
            if (!node) {
                ADD_FAILURE() << "Task plan contains an unknown task";
                continue;
            }
            std::string name = node->GetInstanceName();
            constexpr std::string_view prefix = "group/";
            if (name.starts_with(prefix)) name.erase(0, prefix.size());
            names.push_back(std::move(name));
        }
        std::sort(names.begin(), names.end());
        waves.push_back(std::move(names));
    }
    return waves;
}

TEST(SubGraphNodesTest, ScopePrefixesNamesRecordsMembersAndPreservesEdges) {
    GraphFixture scoped;
    GraphScope scope(scoped.graph, "cluster");
    const NodeHandle source = scope.AddNode("Source", "source");
    const NodeHandle transform = scope.AddNode("Transform", "transform");
    const NodeHandle sink = scope.AddNode("Sink", "sink");
    scope.Connect(source, 0, transform, 0);
    scope.Connect(transform, 0, sink, 0);

    GraphFixture hand;
    hand.AddSource("cluster/source");
    hand.AddTransform("cluster/transform");
    hand.AddSink("cluster/sink");
    hand.graph.ConnectNodes({0}, 0, {1}, 0);
    hand.graph.ConnectNodes({1}, 0, {2}, 0);

    EXPECT_EQ(scoped.graph.GetNodeCount(), hand.graph.GetNodeCount());
    EXPECT_EQ(scoped.graph.GetTopology().GetEdgeCount(), hand.graph.GetTopology().GetEdgeCount());
    EXPECT_EQ(Names(scoped.graph.GetTopology()), Names(hand.graph.GetTopology()));
    ASSERT_EQ(scope.GetMembers().size(), 3u);
    EXPECT_EQ(scoped.graph.GetInstance(scope.GetMembers()[1])->GetInstanceName(),
              "cluster/transform");
}

TEST(SubGraphNodesTest, FlattenedSubGraphProducesTheHandWiredTaskPlan) {
    GraphFixture hand;
    const NodeHandle handSource = hand.AddSource("source");
    const NodeHandle handTransform = hand.AddTransform("transform");
    const NodeHandle handSink = hand.AddSink("sink");
    hand.graph.ConnectNodes(handSource, 0, handTransform, 0);
    hand.graph.ConnectNodes(handTransform, 0, handSink, 0);
    ASSERT_NO_THROW(hand.graph.Compile());

    GraphFixture grouped;
    const NodeHandle groupedSource = grouped.AddSource("source");
    const NodeHandle groupedSink = grouped.AddSink("sink");
    const SubGraphHandle group = grouped.graph.Instantiate<ChainSubGraph>("group");
    ASSERT_TRUE(group.IsValid());
    grouped.graph.Connect(groupedSource, SourceSlots::OUT, group, ChainPorts::IN);
    grouped.graph.Connect(group, ChainPorts::OUT, groupedSink, SinkSlots::IN);
    ASSERT_NO_THROW(grouped.graph.Compile());

    const GraphTaskPlan& handPlan = hand.graph.GetExecutionTaskPlan();
    const GraphTaskPlan& groupedPlan = grouped.graph.GetExecutionTaskPlan();
    EXPECT_EQ(groupedPlan.tasks.size(), handPlan.tasks.size());
    EXPECT_EQ(groupedPlan.dependencyGraph.GetEdgeCount(),
              handPlan.dependencyGraph.GetEdgeCount());
    EXPECT_EQ(WaveNames(groupedPlan), WaveNames(handPlan));
    ASSERT_EQ(group.GetMembers().size(), 1u);
    EXPECT_EQ(grouped.graph.GetInstance(group.GetMembers()[0])->GetInstanceName(),
              "group/Transform");
}

TEST(SubGraphNodesTest, ExpansionRejectsUnboundAndDoublyBoundPorts) {
    GraphFixture fixture;
    EXPECT_THROW(fixture.graph.Instantiate<UnboundSubGraph>("unbound"), std::runtime_error);
    EXPECT_EQ(fixture.graph.GetNodeCount(), 0u);
    EXPECT_THROW(fixture.graph.Instantiate<DoubleOutputSubGraph>("double"), std::runtime_error);
    EXPECT_EQ(fixture.graph.GetNodeCount(), 0u);
}

TEST(SubGraphNodesTest, ScopedNameCollisionStillUsesRenderGraphDiagnostic) {
    GraphFixture fixture;
    GraphScope scope(fixture.graph, "cluster");
    scope.AddNode("Transform", "same");
    EXPECT_THROW(scope.AddNode("Transform", "same"), std::runtime_error);
}

TEST(SubGraphNodesTest, ExpansionRejectsTypeCyclesAndDepthBeyondEight) {
    GraphFixture fixture;
    EXPECT_THROW(fixture.graph.Instantiate<RecursiveSubGraph>("recursive"), std::runtime_error);
    EXPECT_EQ(fixture.graph.GetNodeCount(), 0u);
    EXPECT_THROW(fixture.graph.Instantiate<DepthSubGraph<9>>("deep"), std::runtime_error);
    EXPECT_EQ(fixture.graph.GetNodeCount(), 0u);
}

} // namespace
