#include <gtest/gtest.h>

#include "Core/RenderGraph.h"
#include "Core/NodeTypeRegistry.h"
#include "Core/VariadicTypedNode.h"
#include "Data/Core/ResourceConfig.h"
#include "CapabilityGraph.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;

namespace {

// The test graph uses an opaque buffer value so the test exercises the real slot
// type machinery without constructing Vulkan resources or devices.
CONSTEXPR_NODE_CONFIG(SourceSlots, 0, 1, SlotArrayMode::Single) {
    OUTPUT_SLOT(SourceOutput, const VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);
};

CONSTEXPR_NODE_CONFIG(TransformSlots, 1, 1, SlotArrayMode::Single) {
    INPUT_SLOT(TransformInput, const VkBuffer, 0, SlotNullability::Required,
               SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    OUTPUT_SLOT(TransformOutput, const VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);
};

CONSTEXPR_NODE_CONFIG(SinkSlots, 1, 0, SlotArrayMode::Single) {
    INPUT_SLOT(SinkInput, const VkBuffer, 0, SlotNullability::Required,
               SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);
};

CONSTEXPR_NODE_CONFIG(ChainPorts, 1, 1, SlotArrayMode::Single) {
    INPUT_SLOT(ChainInput, const VkBuffer, 0, SlotNullability::Required,
               SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    OUTPUT_SLOT(ChainOutput, const VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);
};

CONSTEXPR_NODE_CONFIG(EmptyPorts, 0, 0, SlotArrayMode::Single) {};

CONSTEXPR_NODE_CONFIG(VariadicInputPorts, 1, 0, SlotArrayMode::Single) {
    INPUT_SLOT(VariadicInput, const VkBuffer, 0, SlotNullability::Required,
               SlotRole::Dependency | SlotRole::Execute,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
};

CONSTEXPR_NODE_CONFIG(PassThroughPorts, 1, 1, SlotArrayMode::Single) {
    INPUT_SLOT(PassInput, const VkBuffer, 0, SlotNullability::Required,
               SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    OUTPUT_SLOT(PassOutput, const VkBuffer, 0, SlotNullability::Required,
                SlotMutability::WriteOnly);
};

CONSTEXPR_NODE_CONFIG(ProviderPorts, 0, 1, SlotArrayMode::Single) {
    OUTPUT_SLOT(IndirectLighting, const VkBuffer, 0,
                SlotNullability::Required, SlotMutability::WriteOnly);

    ProviderPorts() {
        HandleDescriptor desc{"VkBuffer"};
        INIT_OUTPUT_DESC(IndirectLighting, "indirect_lighting",
                         ResourceLifetime::Transient, desc);
    }
};

class TestNode final : public NodeInstance {
public:
    TestNode(const std::string& name, NodeType* type)
        : NodeInstance(name, type) {}
};

class TestNodeType final : public NodeType {
public:
    TestNodeType(std::string name, size_t inputCount, size_t outputCount,
                 bool optionalInputs = false)
        : NodeType(std::move(name)) {
        inputSchema.resize(inputCount);
        outputSchema.resize(outputCount);
        for (size_t index = 0; index < inputCount; ++index) {
            inputSchema[index].name = "input" + std::to_string(index);
            inputSchema[index].type = ResourceType::Buffer;
            inputSchema[index].nullable = optionalInputs;
            inputSchema[index].declaredType = std::type_index(typeid(const VkBuffer));
            inputSchema[index].role = optionalInputs ? SlotRole::Execute
                                                     : SlotRole::Dependency;
        }
        for (size_t index = 0; index < outputCount; ++index) {
            outputSchema[index].name = "output" + std::to_string(index);
            outputSchema[index].type = ResourceType::Buffer;
            outputSchema[index].nullable = false;
            outputSchema[index].declaredType = std::type_index(typeid(const VkBuffer));
            outputSchema[index].role = SlotRole::Output;
        }
    }

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<TestNode>(name, const_cast<TestNodeType*>(this));
    }
};

class TestVariadicSink final : public VariadicTypedNode<EmptyPorts> {
public:
    TestVariadicSink(const std::string& name, NodeType* type)
        : VariadicTypedNode(name, type) {}

    void RegisterBufferInput() {
        VariadicSlotInfo slot;
        slot.resourceType = ResourceType::Buffer;
        slot.slotName = "buffer_input";
        RegisterVariadicSlot(slot);
        SetVariadicInputConstraints(1, 1);
    }

protected:
    void VariadicExecuteImpl(VariadicExecuteContext&) override {}
};

class TestVariadicSinkType final : public NodeType {
public:
    TestVariadicSinkType() : NodeType("VariadicSink") {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<TestVariadicSink>(name,
                                                   const_cast<TestVariadicSinkType*>(this));
    }
};

struct ChainParams {};

struct PassThroughParams {};

class VariadicInputSubGraph final : public SubGraphType<VariadicInputSubGraph> {
public:
    using PortConfig = VariadicInputPorts;
    using Params = ChainParams;

    void Build(GraphScope& scope, const Params&) {
        const NodeHandle sink = scope.AddNode("VariadicSink", "sink");
        scope.GetInstance<TestVariadicSink>(sink)->RegisterBufferInput();
        scope.BindInputIndex(VariadicInputPorts::VariadicInput, sink, 0);
    }
};

class RegisteredPassThroughSubGraph final
    : public SubGraphType<RegisteredPassThroughSubGraph> {
public:
    static constexpr std::string_view TYPE_NAME = "Test.RegisteredPassThrough";
    static constexpr std::string_view CAPABILITY_NAME = "";
    using PortConfig = PassThroughPorts;
    using Params = PassThroughParams;

    void Build(GraphScope& scope, const Params&) {
        scope.BindOutput(PassThroughPorts::PassOutput, PassThroughPorts::PassInput);
    }
};

class ChainSubGraph final : public SubGraphType<ChainSubGraph> {
public:
    using PortConfig = ChainPorts;
    using Params = ChainParams;

    void Build(GraphScope& scope, const Params&) {
        const NodeHandle transform = scope.AddNode("Transform", "transform");
        scope.BindInput(ChainPorts::ChainInput, transform, TransformSlots::TransformInput);
        scope.BindOutput(ChainPorts::ChainOutput, transform, TransformSlots::TransformOutput);
    }
};

class UnboundSubGraph final : public SubGraphType<UnboundSubGraph> {
public:
    using PortConfig = ChainPorts;
    using Params = ChainParams;

    void Build(GraphScope& scope, const Params&) {
        const NodeHandle transform = scope.AddNode("Transform", "Transform");
        scope.BindOutput(ChainPorts::ChainOutput, transform, TransformSlots::TransformOutput);
    }
};

class DoubleOutputSubGraph final : public SubGraphType<DoubleOutputSubGraph> {
public:
    using PortConfig = ChainPorts;
    using Params = ChainParams;

    void Build(GraphScope& scope, const Params&) {
        const NodeHandle transform = scope.AddNode("Transform", "Transform");
        scope.BindInput(ChainPorts::ChainInput, transform, TransformSlots::TransformInput);
        scope.BindOutput(ChainPorts::ChainOutput, transform, TransformSlots::TransformOutput);
        scope.BindOutput(ChainPorts::ChainOutput, transform, TransformSlots::TransformOutput);
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

class RegisteredProviderSubGraph final : public SubGraphType<RegisteredProviderSubGraph> {
public:
    static constexpr std::string_view TYPE_NAME = "TestIndirectLightingProvider";
    static constexpr std::string_view CAPABILITY_NAME = "Test:IndirectLighting";
    using PortConfig = ProviderPorts;
    using Params = ChainParams;

    void Build(GraphScope& scope, const Params&) {
        const NodeHandle source = scope.AddNode("Source", "source");
        scope.BindOutput(ProviderPorts::IndirectLighting, source, SourceSlots::SourceOutput);
    }
};

class GraphFixture {
public:
    GraphFixture()
        : graph(&registry) {
        registry.RegisterNodeType(std::make_unique<TestNodeType>("Source", 0, 1));
        registry.RegisterNodeType(std::make_unique<TestNodeType>("Transform", 1, 1));
        registry.RegisterNodeType(std::make_unique<TestNodeType>("Sink", 1, 0));
        registry.RegisterNodeType(std::make_unique<TestNodeType>("OptionalSink", 1, 0, true));
        registry.RegisterNodeType(std::make_unique<TestVariadicSinkType>());
    }

    NodeHandle AddSource(const std::string& name) { return graph.AddNode("Source", name); }
    NodeHandle AddTransform(const std::string& name) { return graph.AddNode("Transform", name); }
    NodeHandle AddSink(const std::string& name) { return graph.AddNode("Sink", name); }
    NodeHandle AddOptionalSink(const std::string& name) { return graph.AddNode("OptionalSink", name); }

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
    grouped.graph.Connect(groupedSource, SourceSlots::SourceOutput, group, ChainPorts::ChainInput);
    grouped.graph.Connect(group, ChainPorts::ChainOutput, groupedSink, SinkSlots::SinkInput);
    ASSERT_NO_THROW(grouped.graph.Compile());

    const GraphTaskPlan& handPlan = hand.graph.GetExecutionTaskPlan();
    const GraphTaskPlan& groupedPlan = grouped.graph.GetExecutionTaskPlan();
    EXPECT_EQ(groupedPlan.tasks.size(), handPlan.tasks.size());
    EXPECT_EQ(groupedPlan.dependencyGraph.GetEdgeCount(),
              handPlan.dependencyGraph.GetEdgeCount());
    EXPECT_EQ(WaveNames(groupedPlan), WaveNames(handPlan));
    ASSERT_EQ(group.GetMembers().size(), 1u);
    EXPECT_EQ(grouped.graph.GetInstance(group.GetMembers()[0])->GetInstanceName(),
              "group/transform");
}

TEST(SubGraphNodesTest, RegisteredNodeGroupInstantiatesAndForwardsTheSameResourceEdge) {
    GraphFixture fixture;
    ASSERT_TRUE(fixture.registry.Register<RegisteredPassThroughSubGraph>());
    const NodeHandle source = fixture.AddSource("source");
    const NodeHandle sink = fixture.AddSink("sink");

    const SubGraphHandle passThrough = fixture.graph.InstantiateRegistered(
        "Test.RegisteredPassThrough", "registered", std::any(PassThroughParams{}));
    ASSERT_TRUE(passThrough.IsValid());
    EXPECT_TRUE(passThrough.GetMembers().empty());
    const NodeType* registeredType = fixture.registry.GetNodeType("Test.RegisteredPassThrough");
    ASSERT_NE(registeredType, nullptr);
    EXPECT_EQ(registeredType->GetInputCount(), 1u);
    EXPECT_EQ(registeredType->GetOutputCount(), 1u);

    fixture.graph.Connect(source, SourceSlots::SourceOutput,
                          passThrough, PassThroughPorts::PassInput);
    fixture.graph.Connect(passThrough, PassThroughPorts::PassOutput,
                          sink, SinkSlots::SinkInput);

    const GraphTopology& topology = fixture.graph.GetTopology();
    ASSERT_EQ(topology.GetEdgeCount(), 1u);
    const auto edge = topology.GetEdges().front();
    EXPECT_EQ(edge.source->GetInstanceName(), "source");
    EXPECT_EQ(edge.target->GetInstanceName(), "sink");
}

TEST(SubGraphNodesTest, TypedGroupInputConnectsToItsVariadicMemberSlot) {
    GraphFixture fixture;
    const NodeHandle source = fixture.AddSource("source");
    const SubGraphHandle group = fixture.graph.Instantiate<VariadicInputSubGraph>("variadic_group");

    ASSERT_TRUE(group.IsValid());
    ASSERT_EQ(group.GetMembers().size(), 1u);
    EXPECT_NO_THROW(fixture.graph.Connect(source, SourceSlots::SourceOutput,
                                          group, VariadicInputPorts::VariadicInput));

    const auto* sink = dynamic_cast<const IVariadicNode*>(
        fixture.graph.GetInstance(group.GetMembers().front()));
    ASSERT_NE(sink, nullptr);
    ASSERT_NE(sink->GetVariadicSlotInfo(0), nullptr);
    EXPECT_NE(sink->GetVariadicSlotInfo(0)->resource, nullptr);
    ASSERT_EQ(fixture.graph.GetTopology().GetEdgeCount(), 1u);
    EXPECT_TRUE(fixture.graph.GetTopology().GetEdges().front().isVariadic);
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

TEST(SubGraphNodesTest, RegisteredGroupUsesRegistrySchemaAndCapabilitySelectedWiring) {
    GraphFixture fixture;
    ASSERT_TRUE(fixture.registry.Register<RegisteredProviderSubGraph>());
    EXPECT_TRUE(fixture.registry.Has<RegisteredProviderSubGraph>());

    NodeType* registered = fixture.registry.GetNodeType("TestIndirectLightingProvider");
    ASSERT_NE(registered, nullptr);
    EXPECT_EQ(fixture.registry.Get<RegisteredProviderSubGraph>(), registered);
    ASSERT_EQ(registered->GetInputCount(), 0u);
    ASSERT_EQ(registered->GetOutputCount(), 1u);
    EXPECT_EQ(registered->GetOutputDescriptor(0)->name, "indirect_lighting");
    EXPECT_EQ(registered->GetOutputDescriptor(0)->declaredType,
              std::type_index(typeid(const VkBuffer)));
    EXPECT_EQ(registered->GetOutputDescriptor(0)->role, SlotRole::Output);

    Vixen::CapabilityGraph capabilities;
    capabilities.RegisterCapability(
        std::make_shared<Vixen::CompositeCapability>("Test:IndirectLighting"));
    const NodeHandle sink = fixture.AddOptionalSink("shade");
    const SubGraphProviderSelection provider = fixture.graph.InstantiateProviderForInput(
        capabilities, sink, 0, "selected_indirect_lighting");
    ASSERT_TRUE(provider.group.IsValid());
    ASSERT_EQ(provider.group.GetMembers().size(), 1u);
    fixture.graph.Connect(provider.group, provider.outputPort, sink, 0);
    EXPECT_NE(fixture.graph.GetInstance(sink)->GetInput(0, 0), nullptr);
    EXPECT_EQ(fixture.graph.GetNodeCount(), 2u);
}

TEST(SubGraphNodesTest, RegisteredGroupCanBeExpandedByRegistryName) {
    GraphFixture fixture;
    ASSERT_TRUE(fixture.registry.Register<RegisteredProviderSubGraph>());

    const SubGraphHandle group = fixture.graph.Instantiate(
        "TestIndirectLightingProvider", "provider_by_name");
    ASSERT_TRUE(group.IsValid());
    ASSERT_EQ(group.GetMembers().size(), 1u);
    EXPECT_EQ(fixture.graph.GetInstance(group.GetMembers()[0])->GetInstanceName(),
              "provider_by_name/source");
}

TEST(SubGraphNodesTest, OptionalInputStaysUnconnectedWhenProviderCapabilityIsAbsent) {
    GraphFixture fixture;
    ASSERT_TRUE(fixture.registry.Register<RegisteredProviderSubGraph>());
    Vixen::CapabilityGraph capabilities;
    const NodeHandle sink = fixture.AddOptionalSink("shade");

    const SubGraphProviderSelection provider = fixture.graph.InstantiateProviderForInput(
        capabilities, sink, 0, "unselected_indirect_lighting");
    EXPECT_FALSE(provider.group.IsValid());
    EXPECT_EQ(fixture.graph.GetNodeCount(), 1u);
    EXPECT_EQ(fixture.graph.GetInstance(sink)->GetInput(0, 0), nullptr);
    EXPECT_NO_THROW(fixture.graph.Compile());
}

} // namespace

namespace {
CONSTEXPR_NODE_CONFIG(RelaxedOutputPorts, 0, 1, SlotArrayMode::Single) {
    OUTPUT_SLOT(VISUAL, const VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);
    RelaxedOutputPorts() {
        INIT_OUTPUT_DESC(VISUAL, "jittered_visibility", ResourceLifetime::Transient, HandleDescriptor{"VkBuffer"});
        outputs[VISUAL.index].visualRelaxationReason = "R494 temporal primary-ray sampling";
    }
};
CONSTEXPR_NODE_CONFIG(VisualInputPorts, 1, 0, SlotArrayMode::Single) {
    INPUT_SLOT(VISUAL, const VkBuffer, 0, SlotNullability::Required, SlotRole::Execute,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    VisualInputPorts() {
        INIT_INPUT_DESC(VISUAL, "visual_input", ResourceLifetime::Transient, HandleDescriptor{"VkBuffer"});
        inputs[VISUAL.index].acceptsVisualRelaxation = true;
    }
};
class RelaxedOutputGroup : public SubGraphType<RelaxedOutputGroup> {
public:
    using PortConfig = RelaxedOutputPorts;
    using Params = ChainParams;
    void Build(GraphScope& scope, const Params&) {
        auto source = scope.AddNode("Source", "sample");
        scope.BindOutput(PortConfig::VISUAL, source, SourceSlots::SourceOutput);
    }
};
class VisualInputGroup : public SubGraphType<VisualInputGroup> {
public:
    using PortConfig = VisualInputPorts;
    using Params = ChainParams;
    void Build(GraphScope& scope, const Params&) {
        auto sink = scope.AddNode("OptionalSink", "visual_sink");
        scope.BindInput(PortConfig::VISUAL, sink, SinkSlots::SinkInput);
    }
};
class LaunderedOutputGroup : public SubGraphType<LaunderedOutputGroup> {
public:
    using PortConfig = ProviderPorts;
    using Params = ChainParams;
    void Build(GraphScope& scope, const Params&) {
        auto nested = scope.Instantiate<RelaxedOutputGroup>("nested", Params{});
        scope.BindOutput(ProviderPorts::IndirectLighting, nested, RelaxedOutputPorts::VISUAL);
    }
};
}

TEST(SubGraphNodesTest, RelaxedSamplingCannotFeedADeterministicBoundaryOrRawAlias) {
    GraphFixture fixture;
    auto source = fixture.graph.Instantiate<RelaxedOutputGroup>("sampling");
    EXPECT_FALSE(source.GetOutputSchema()[0].visualRelaxationReason.empty());
    EXPECT_THROW(fixture.graph.Connect(source, 0u, fixture.AddSink("picking"), 0u), std::runtime_error);
    EXPECT_THROW(source.ResolveOutput(0u), std::runtime_error);
    auto visual = fixture.graph.Instantiate<VisualInputGroup>("lighting");
    EXPECT_NO_THROW(fixture.graph.Connect(source, 0u, visual, 0u));
}

TEST(SubGraphNodesTest, NestingCannotStripASamplingRelaxation) {
    GraphFixture fixture;
    EXPECT_THROW(fixture.graph.Instantiate<LaunderedOutputGroup>("laundered"), std::runtime_error);
}
